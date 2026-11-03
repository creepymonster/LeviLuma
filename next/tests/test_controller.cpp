// Host-Tests: Controller-Logik (Gateway-Register mit NVS-Ersatz, Zigbee-Zuordnung und Umrechnung,
// Touch-Kalibrierung) und End-to-End-Ablauf Controller <-> Gateway ueber ein simuliertes ESP-NOW.
// Die Simulation nutzt den echten Gateway-Code (GatewayHandler) und bildet die ESP-NOW-Verschluesselung
// vereinfacht nach: ein Rahmen kommt nur an, wenn beide Seiten denselben LMK verwenden.
#include <math.h>
#include <deque>
#include <vector>
#include "test_util.h"
#include "ctrl_link.h"
#include "gateway_handler.h"
#include "gateway_registry.h"
#include "CalibCore.h"
#include "calib_targets.h"
#include "touch_cal.h"
#include "zb_mapping.h"

static const uint8_t CTRL_ID[6] = {0x60, 0x55, 0xF9, 0x00, 0x00, 0xC5};

struct SimGateway {
  MemKvStore kv;
  LightModel light;
  CalibStore cal{kv, CalibStore::Defaults()};
  PairingTable pairing{kv};
  GatewayHandler *h = nullptr;
  bool hasKey = false;
  uint8_t key[16] = {0};
  bool online = true;
  bool knowsCtrlPeer = false; // nach DISCOVER/Kopplung als Peer eingetragen

  SimGateway(uint8_t last, const uint8_t *k) {
    cal.load();
    pairing.load();
    GatewayHandler::Identity id;
    id.id[0] = 0x34;
    id.id[5] = last;
    snprintf(id.name, sizeof(id.name), "GW-%02X", last);
    hasKey = k != nullptr;
    if (k) {
      memcpy(key, k, 16);
    }
    id.keyConfigured = hasKey;
    id.session = 0x1000 + last;
    h = new GatewayHandler(light, cal, pairing, id);
  }
  ~SimGateway() {
    delete h;
  }
  const uint8_t *id() const {
    return h->identity().id;
  }
  void reboot(uint32_t newSession) {
    delete h;
    GatewayHandler::Identity id;
    memcpy(id.id, idCopy, 6);
    strcpy(id.name, "GW-neu");
    id.keyConfigured = hasKey;
    id.session = newSession;
    light = LightModel();
    pairing.load();
    cal.load();
    h = new GatewayHandler(light, cal, pairing, id);
    knowsCtrlPeer = pairing.contains(CTRL_ID);
  }
  uint8_t idCopy[6];
};

struct Frame {
  uint8_t src[6], dst[6];
  bool bcast;
  bool encrypted;
  uint8_t key[16];
  std::vector<uint8_t> data;
};

struct SimNet : Radio {
  std::vector<SimGateway *> gws;
  std::deque<Frame> air;
  struct Peer {
    uint8_t mac[6];
    bool enc;
    uint8_t lmk[16];
  };
  std::vector<Peer> peers; // Peers des Controllers
  int dropNext = 0;        // die naechsten n Unicast-Rahmen gehen verloren
  int delivered = 0;

  Peer *peer(const uint8_t *mac) {
    for (auto &p : peers) {
      if (!memcmp(p.mac, mac, 6)) {
        return &p;
      }
    }
    return nullptr;
  }
  bool send(const uint8_t mac[6], const uint8_t *d, size_t n) override {
    Peer *p = peer(mac);
    if (!p) {
      return false; // ESP-NOW: Unicast nur an eingetragene Peers
    }
    Frame f;
    memcpy(f.src, CTRL_ID, 6);
    memcpy(f.dst, mac, 6);
    f.bcast = false;
    f.encrypted = p->enc;
    memcpy(f.key, p->lmk, 16);
    f.data.assign(d, d + n);
    air.push_back(f);
    return true;
  }
  bool broadcast(const uint8_t *d, size_t n) override {
    Frame f;
    memcpy(f.src, CTRL_ID, 6);
    memset(f.dst, 0xFF, 6);
    f.bcast = true;
    f.encrypted = false;
    f.data.assign(d, d + n);
    air.push_back(f);
    return true;
  }
  bool setPeer(const uint8_t mac[6], const uint8_t *lmk) override {
    Peer *p = peer(mac);
    if (!p) {
      peers.push_back(Peer());
      p = &peers.back();
      memcpy(p->mac, mac, 6);
    }
    p->enc = lmk != nullptr;
    memset(p->lmk, 0, 16);
    if (lmk) {
      memcpy(p->lmk, lmk, 16);
    }
    return true;
  }
  void delPeer(const uint8_t mac[6]) override {
    for (size_t i = 0; i < peers.size(); i++) {
      if (!memcmp(peers[i].mac, mac, 6)) {
        peers.erase(peers.begin() + i);
        return;
      }
    }
  }

  // Rahmen zustellen: Controller -> Gateways und zurueck
  void run(CtrlLink &link, uint32_t now) {
    for (int guard = 0; guard < 200 && !air.empty(); guard++) {
      Frame f = air.front();
      air.pop_front();
      for (SimGateway *g : gws) {
        if (!g->online || (!f.bcast && memcmp(f.dst, g->id(), 6))) {
          continue;
        }
        if (!f.bcast && dropNext > 0) {
          dropNext--;
          continue;
        }
        // Verschluesselung: Unicast kommt nur an, wenn beide denselben Schluessel nutzen
        if (!f.bcast) {
          bool ok = f.encrypted ? (g->hasKey && g->knowsCtrlPeer && !memcmp(f.key, g->key, 16)) : !g->hasKey;
          if (!ok) {
            continue;
          }
        }
        GatewayHandler::Output out;
        g->h->handle(f.src, f.bcast, f.data.data(), f.data.size(), now, out);
        if (out.ensurePeer) {
          g->knowsCtrlPeer = true;
        }
        if (out.removePeer) {
          g->knowsCtrlPeer = false;
        }
        if (out.replyLen) {
          deliverToCtrl(link, g, out.reply, out.replyLen, out.replyBroadcast, now);
        }
      }
    }
  }
  void deliverToCtrl(CtrlLink &link, SimGateway *g, const uint8_t *d, size_t n, bool bcast, uint32_t now) {
    if (!bcast) {
      Peer *p = peer(g->id());
      bool ok = g->hasKey ? (p && p->enc && !memcmp(p->lmk, g->key, 16)) : true;
      if (!ok) {
        return;
      }
    }
    delivered++;
    link.onFrame(g->id(), bcast, d, n, now);
  }
  void beacon(CtrlLink &link, SimGateway *g, uint32_t now) {
    if (!g->online) {
      return;
    }
    uint8_t b[LVP_MAX_FRAME];
    size_t n = g->h->buildAnnounce(b, sizeof(b), now);
    deliverToCtrl(link, g, b, n, true, now);
  }
  void notify(CtrlLink &link, SimGateway *g, uint32_t now) {
    uint8_t b[LVP_MAX_FRAME];
    size_t n = g->h->buildStateNotify(b, sizeof(b));
    deliverToCtrl(link, g, b, n, false, now);
  }
};

struct Rig {
  MemKvStore kv;
  GatewayRegistry reg{kv};
  SimNet net;
  CtrlLink *link = nullptr;
  uint32_t now = 1000;

  Rig() {
    reg.load();
    CtrlLink::Config c;
    memcpy(c.ownId, CTRL_ID, 6);
    strcpy(c.ownName, "Zentrale");
    c.session = 0xABCD;
    link = new CtrlLink(reg, net, c);
  }
  ~Rig() {
    delete link;
  }
  void step(uint32_t ms = 10) {
    now += ms;
    link->tick(now);
    net.run(*link, now);
  }
  void settle(int n = 30) {
    for (int i = 0; i < n; i++) {
      step();
    }
  }
};

// ---- Register ----

static void test_registry_persistence() {
  MemKvStore kv;
  GatewayRegistry r(kv);
  r.load();
  uint8_t id[6] = {1, 2, 3, 4, 5, 6};
  int i = r.addDiscovered(id, 100);
  CHECK(i >= 0);
  CHECK_EQ(r.find(id), i);
  r.at(i).hasKey = true;
  memset(r.at(i).key, 0xA5, 16);
  CHECK_EQ(r.markPaired(i), LVP_OK);
  CHECK(r.setAlias(i, "Wohnzimmer"));
  r.at(i).zbEndpoint = 12;
  CHECK(r.persist());

  GatewayRegistry r2(kv); // Neustart
  r2.load();
  int j = r2.find(id);
  CHECK(j >= 0);
  CHECK(r2.at(j).paired);
  CHECK(r2.at(j).hasKey);
  CHECK_EQ(r2.at(j).key[15], 0xA5);
  CHECK_EQ(r2.at(j).zbEndpoint, 12);
  CHECK(strcmp(r2.at(j).displayName(), "Wohnzimmer") == 0);

  // Entkoppeln loescht Schluessel und Zuordnung dauerhaft
  CHECK(r2.unpair(j));
  GatewayRegistry r3(kv);
  r3.load();
  CHECK_EQ(r3.find(id), -1);

  // Unbekannte Gateways werden nicht gespeichert und bei Platzmangel verdraengt
  GatewayRegistry r4(kv);
  r4.load();
  uint8_t x[6] = {9, 9, 9, 9, 9, 0};
  for (int k = 0; k < GatewayRegistry::MAX_ENTRIES + 3; k++) {
    x[5] = (uint8_t)k;
    CHECK(r4.addDiscovered(x, 1000 + k) >= 0);
  }
  x[5] = 0;
  CHECK_EQ(r4.find(x), -1); // aeltester verdraengt

  // Beschaedigte Daten
  kv.data[GatewayRegistry::KEY] = {1, 1, 0, 0};
  GatewayRegistry r5(kv);
  r5.load();
  CHECK_EQ(r5.pairedCount(), 0);
}

static void test_registry_paired_limit() {
  MemKvStore kv;
  GatewayRegistry r(kv);
  uint8_t id[6] = {7, 7, 7, 7, 7, 0};
  for (int k = 0; k < GatewayRegistry::MAX_PAIRED; k++) {
    id[5] = (uint8_t)k;
    CHECK_EQ(r.markPaired(r.addDiscovered(id, 0)), LVP_OK);
  }
  id[5] = 200;
  CHECK_EQ(r.markPaired(r.addDiscovered(id, 0)), LVP_ERR_FULL);
  kv.failWrites = true;
  id[5] = 0;
  int i = r.find(id);
  r.at(i).paired = false;
  CHECK_EQ(r.markPaired(i), LVP_ERR_STORAGE);
  CHECK(!r.at(i).paired);
}

// ---- Zigbee-Zuordnung und Umrechnung ----

static void test_zb_endpoint_mapping_stable() {
  MemKvStore kv;
  GatewayRegistry r(kv);
  uint8_t a[6] = {1, 0, 0, 0, 0, 1}, b[6] = {1, 0, 0, 0, 0, 2}, c[6] = {1, 0, 0, 0, 0, 3};
  int ia = r.addDiscovered(a, 0), ib = r.addDiscovered(b, 0), ic = r.addDiscovered(c, 0);
  r.markPaired(ia);
  r.markPaired(ib);
  CHECK_EQ(ZbMap::assign(r, ic), 0); // nicht gekoppelt
  CHECK(ZbMap::assignAll(r));
  CHECK_EQ(r.at(ia).zbEndpoint, ZbMap::FIRST_ENDPOINT);
  CHECK_EQ(r.at(ib).zbEndpoint, ZbMap::FIRST_ENDPOINT + 1);
  uint32_t sig = ZbMap::layoutSignature(r);
  CHECK(!ZbMap::assignAll(r)); // keine Aenderung
  CHECK_EQ(ZbMap::layoutSignature(r), sig);

  // Gateway A entkoppelt: B behaelt seinen Endpoint (stabil), C bekommt den freien
  r.unpair(ia);
  r.markPaired(ic);
  ZbMap::assignAll(r);
  CHECK_EQ(r.at(ib).zbEndpoint, ZbMap::FIRST_ENDPOINT + 1);
  CHECK_EQ(r.at(ic).zbEndpoint, ZbMap::FIRST_ENDPOINT);
  CHECK(ZbMap::layoutSignature(r) != sig);
  CHECK_EQ(ZbMap::findByEndpoint(r, ZbMap::FIRST_ENDPOINT + 1), ib);
  CHECK_EQ(ZbMap::findByEndpoint(r, 99), -1);

  // persistent ueber Neustart
  GatewayRegistry r2(kv);
  r2.load();
  CHECK_EQ(r2.at(r2.find(b)).zbEndpoint, ZbMap::FIRST_ENDPOINT + 1);
  CHECK(ZbMap::release(r2, r2.find(b)));
  CHECK_EQ(r2.at(r2.find(b)).zbEndpoint, ZbMap::OPT_OUT);
  CHECK(!ZbMap::hasEndpoint(r2.at(r2.find(b))));
  CHECK(!ZbMap::assignAll(r2)); // bewusst abgewaehlt: keine automatische Zuordnung
  GatewayRegistry r3(kv);       // auch nach Neustart nicht
  r3.load();
  CHECK(!ZbMap::assignAll(r3));
  CHECK_EQ(ZbMap::assign(r3, r3.find(b)), ZbMap::FIRST_ENDPOINT + 1); // manuell wieder zuordnen
}

static void test_zb_conversions() {
  CHECK_EQ(ZbMap::levelToBrightness(254), 100);
  CHECK_EQ(ZbMap::levelToBrightness(1), 1);
  CHECK_EQ(ZbMap::levelToBrightness(127), 50);
  CHECK_EQ(ZbMap::brightnessToLevel(100), 254);
  CHECK_EQ(ZbMap::brightnessToLevel(1), 3);
  for (int b = 1; b <= 100; b++) { // Hin und zurueck stabil
    CHECK_EQ(ZbMap::levelToBrightness(ZbMap::brightnessToLevel((uint8_t)b)), b);
  }
  uint8_t rgb[3];
  ZbMap::hueSatToRgb(0, 254, rgb);
  CHECK(rgb[0] == 255 && rgb[1] == 0 && rgb[2] == 0);
  ZbMap::hueSatToRgb(0, 0, rgb);
  CHECK(rgb[0] == 255 && rgb[1] == 255 && rgb[2] == 255);
  uint8_t h, s;
  const uint8_t blue[3] = {0, 0, 255};
  ZbMap::rgbToHueSat(blue, &h, &s);
  CHECK(abs(h - 169) <= 1);
  CHECK_EQ(s, 254);

  // xy: Weisspunkt D65 und Primaerfarben
  uint16_t x, y;
  const uint8_t white[3] = {255, 255, 255};
  ZbMap::rgbToXy(white, &x, &y);
  CHECK(fabs(x / 65536.0 - 0.3127) < 0.002 && fabs(y / 65536.0 - 0.3290) < 0.002);
  ZbMap::xyToRgb(x, y, rgb);
  CHECK(rgb[0] >= 253 && rgb[1] >= 253 && rgb[2] >= 253);
  const uint8_t red[3] = {255, 0, 0};
  ZbMap::rgbToXy(red, &x, &y);
  CHECK(fabs(x / 65536.0 - 0.64) < 0.002 && fabs(y / 65536.0 - 0.33) < 0.002);
  ZbMap::xyToRgb(x, y, rgb);
  CHECK(rgb[0] == 255 && rgb[1] <= 3 && rgb[2] <= 3);

  ZbMap::miredToRgb(153, rgb); // ~6500 K: nahezu weiss
  CHECK(rgb[0] == 255 && rgb[1] > 240 && rgb[2] > 240);
  ZbMap::miredToRgb(500, rgb); // 2000 K: warm
  CHECK(rgb[0] == 255 && rgb[2] < 100);
}

// ---- Touch-Kalibrierung ----

static void test_touch_cal() {
  TouchCal c;
  // Rohwerte eines gedrehten und gespiegelten Panels (x<->y getauscht)
  const int32_t raw[3][2] = {{3700, 400}, {3700, 3600}, {400, 3600}};
  const int32_t scr[3][2] = {{20, 20}, {300, 20}, {300, 220}};
  CHECK(c.solve(raw, scr));
  int32_t x, y;
  for (int i = 0; i < 3; i++) {
    c.map(raw[i][0], raw[i][1], &x, &y);
    CHECK(abs(x - scr[i][0]) <= 1 && abs(y - scr[i][1]) <= 1);
  }
  c.map(2050, 2000, &x, &y); // Mitte
  CHECK(abs(x - 160) <= 2 && abs(y - 120) <= 2);
  const int32_t bad[3][2] = {{1, 1}, {2, 2}, {3, 3}};
  CHECK(!c.solve(bad, scr));
  c.setLinear(200, 3900, 200, 3900, 320, 240, false);
  c.map(200, 3900, &x, &y);
  CHECK(x == 0 && y == 240);
}

// ---- End-to-End Controller <-> Gateway ----

static void test_e2e_discovery_pair_control() {
  Rig r;
  SimGateway g(0x01, nullptr);
  memcpy(g.idCopy, g.id(), 6);
  g.pairing.openWindow(0, 600000); // Kopplungsfenster nach dem Start
  r.net.gws.push_back(&g);
  r.link->start(r.now);
  r.settle(); // DISCOVER -> ANNOUNCE
  int idx = r.reg.find(g.id());
  CHECK(idx >= 0);
  if (idx < 0) {
    return;
  }
  CHECK(!r.reg.at(idx).paired);
  CHECK(strcmp(r.reg.at(idx).announcedName, "GW-01") == 0);
  CHECK(r.link->online(idx, r.now));

  CHECK_EQ(r.link->pair(idx, nullptr, r.now), LVP_OK);
  CHECK(r.reg.at(idx).pairPhase == PairPhase::Pending);
  r.settle();
  CHECK(r.reg.at(idx).paired);
  CHECK(r.reg.at(idx).pairPhase == PairPhase::None);
  CHECK(r.reg.at(idx).stateValid); // Zustand direkt nach der Kopplung abgeglichen
  CHECK(r.reg.at(idx).calibValid);
  CHECK(g.pairing.contains(CTRL_ID));

  // Steuerbefehl: Anzeige folgt erst der Bestaetigung
  lvp_set_state_t c = {LVP_SET_POWER | LVP_SET_BRIGHTNESS, 1, 30, 0, 0, 0, 0};
  CHECK_EQ(r.link->setState(idx, c, r.now), LVP_OK);
  CHECK(r.link->commandPending(idx));
  CHECK(r.reg.at(idx).state.power != 1); // noch nicht bestaetigt
  r.settle();
  CHECK(!r.link->commandPending(idx));
  CHECK_EQ(r.reg.at(idx).state.power, 1);
  CHECK_EQ(r.reg.at(idx).state.brightness, 30);
  CHECK_EQ(g.light.snapshot().brightness, 30); // Gateway ist autoritativ
}

static void test_e2e_key_required() {
  const uint8_t key[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  uint8_t wrong[16];
  memcpy(wrong, key, 16);
  wrong[0] ^= 0xFF;
  Rig r;
  SimGateway g(0x02, key);
  r.net.gws.push_back(&g);
  r.link->start(r.now);
  r.settle();
  int idx = r.reg.find(g.id());
  CHECK(idx >= 0);
  CHECK(r.reg.at(idx).caps & LVP_GW_KEY_REQUIRED);

  // Falscher Schluessel: Gateway kann nicht entschluesseln -> Zeitueberschreitung, keine Kopplung
  CHECK_EQ(r.link->pair(idx, wrong, r.now), LVP_OK);
  r.settle(400);
  CHECK(!r.reg.at(idx).paired);
  CHECK(r.reg.at(idx).pairPhase == PairPhase::Failed);
  CHECK(r.reg.at(idx).lastTimeout);
  CHECK(!r.reg.at(idx).hasKey); // vorlaeufiger Schluessel verworfen
  CHECK(!g.pairing.contains(CTRL_ID));

  // Richtiger Schluessel
  CHECK_EQ(r.link->pair(idx, key, r.now), LVP_OK);
  r.settle();
  CHECK(r.reg.at(idx).paired);
  CHECK(r.reg.at(idx).hasKey);
  CHECK(g.pairing.contains(CTRL_ID));

  // Schluessel nach Neustart des Controllers aus dem Speicher
  GatewayRegistry again(r.kv);
  again.load();
  CHECK(again.at(again.find(g.id())).hasKey);
}

static void test_e2e_pairing_window_closed() {
  Rig r;
  SimGateway g(0x03, nullptr);
  r.net.gws.push_back(&g);
  r.link->start(r.now);
  r.settle();
  int idx = r.reg.find(g.id());
  r.link->pair(idx, nullptr, r.now);
  r.settle();
  CHECK(r.reg.at(idx).pairPhase == PairPhase::Failed);
  CHECK_EQ(r.reg.at(idx).lastError, LVP_ERR_PAIRING_CLOSED);
}

static int pairOpen(Rig &r, SimGateway &g) {
  g.pairing.openWindow(0, 0xFFFFFFF);
  r.net.gws.push_back(&g);
  r.link->start(r.now);
  r.settle();
  int idx = r.reg.find(g.id());
  r.link->pair(idx, nullptr, r.now);
  r.settle();
  return idx;
}

static void test_e2e_retry_on_loss() {
  Rig r;
  SimGateway g(0x04, nullptr);
  int idx = pairOpen(r, g);
  CHECK(r.reg.at(idx).paired);
  uint32_t resent = r.link->stats().resent;
  r.net.dropNext = 2; // zwei Rahmen gehen verloren
  lvp_set_state_t c = {LVP_SET_COLOR, 0, 0, 255, 64, 64, 0};
  r.link->setState(idx, c, r.now);
  r.settle(100);
  CHECK(r.link->stats().resent >= resent + 2);
  CHECK_EQ(r.reg.at(idx).state.g, 64);
  CHECK_EQ(g.light.snapshot().colorRev, 1); // trotz Wiederholungen genau einmal ausgefuehrt
}

static void test_e2e_coalescing_latest_wins() {
  Rig r;
  SimGateway g(0x05, nullptr);
  int idx = pairOpen(r, g);
  // Schnelle Reglerbewegung: viele Befehle, waehrend einer unterwegs ist
  for (int b = 10; b <= 100; b += 10) {
    lvp_set_state_t c = {LVP_SET_BRIGHTNESS, 0, (uint8_t)b, 0, 0, 0, 0};
    r.link->setState(idx, c, r.now);
  }
  lvp_set_state_t on = {LVP_SET_POWER, 1, 0, 0, 0, 0, 0};
  r.link->setState(idx, on, r.now);
  r.settle();
  CHECK_EQ(g.light.snapshot().brightness, 100);
  CHECK_EQ(g.light.snapshot().power, 1);
  CHECK(g.light.snapshot().revision <= 3); // zusammengefasst statt 11 Einzelbefehle
  CHECK_EQ(r.reg.at(idx).state.brightness, 100);

  lvp_set_state_t a = {LVP_SET_RAW, 0, 0, 1, 2, 3, 0}, b = {LVP_SET_COLOR, 0, 0, 4, 5, 6, 0};
  lvp_set_state_t m = ctrl_merge_set(a, b);
  CHECK_EQ(m.mask, LVP_SET_COLOR);
  CHECK_EQ(m.r, 4);
}

static void test_e2e_offline_and_resync() {
  Rig r;
  SimGateway g(0x06, nullptr);
  memcpy(g.idCopy, g.id(), 6);
  int idx = pairOpen(r, g);
  CHECK(r.link->online(idx, r.now));

  // Gateway faellt aus: nach drei verpassten Beacons offline
  g.online = false;
  lvp_set_state_t c = {LVP_SET_POWER, 1, 0, 0, 0, 0, 0};
  r.link->setState(idx, c, r.now);
  for (int i = 0; i < 800; i++) {
    r.step(10);
  }
  CHECK(!r.link->online(idx, r.now));
  CHECK(r.reg.at(idx).lastTimeout); // Befehl nicht bestaetigt
  CHECK(r.reg.at(idx).state.power != 1);

  // Gateway startet neu (neue Sitzung, Zustand unbekannt) und meldet sich wieder
  g.online = true;
  g.reboot(0x9999);
  r.net.beacon(*r.link, &g, r.now);
  CHECK(r.link->online(idx, r.now));
  r.settle();
  CHECK_EQ(r.reg.at(idx).gwSession, 0x9999u); // abgeglichen
  CHECK_EQ(r.reg.at(idx).state.power, -1);    // Gateway kennt den Bandzustand nach Neustart nicht
  CHECK(strcmp(r.reg.at(idx).announcedName, "GW-neu") == 0);
}

static void test_e2e_notify_from_gateway() {
  Rig r;
  SimGateway g(0x07, nullptr);
  int idx = pairOpen(r, g);
  // BLE-Verbindung des Gateways aendert sich: Meldung ohne Anfrage
  g.light.setBle(true);
  g.light.markSent(g.light.snapshot().revision);
  r.net.notify(*r.link, &g, r.now);
  CHECK(r.reg.at(idx).state.flags & LVP_STATE_BLE);
  // Veraltete Meldung (kleinere Revision) wird ignoriert
  lvp_set_state_t c = {LVP_SET_BRIGHTNESS, 0, 77, 0, 0, 0, 0};
  r.link->setState(idx, c, r.now);
  r.settle();
  uint8_t old[LVP_MAX_FRAME];
  lvp_msg_t m;
  lvp_init(&m, LVP_MSG_STATE);
  m.hdr.flags = LVP_FLAG_NOTIFY;
  m.hdr.session = g.h->identity().session;
  m.hdr.seq = 999;
  m.u.state = r.reg.at(idx).state;
  m.u.state.revision = 0;
  m.u.state.brightness = 5;
  size_t n = lvp_encode(&m, old, sizeof(old), nullptr);
  r.link->onFrame(g.id(), false, old, n, r.now);
  CHECK_EQ(r.reg.at(idx).state.brightness, 77);
}

static void test_e2e_calibration() {
  Rig r;
  SimGateway g(0x08, nullptr);
  int idx = pairOpen(r, g);
  CHECK(r.reg.at(idx).calibValid);
  CHECK_EQ(r.reg.at(idx).calib.custom, 0);
  uint8_t pts[30];
  for (int i = 0; i < 30; i++) {
    pts[i] = (uint8_t)(100 + i);
  }
  CHECK(r.link->sendCalib(idx, pts, r.now));
  r.settle();
  CHECK_EQ(r.reg.at(idx).calib.custom, 1);
  CHECK_EQ(r.reg.at(idx).calib.points[29], 129);
  CHECK(g.cal.custom()); // beim Gateway gespeichert
  CHECK(r.link->resetCalib(idx, r.now));
  r.settle();
  CHECK_EQ(r.reg.at(idx).calib.custom, 0);
}

static void test_e2e_unpair() {
  Rig r;
  SimGateway g(0x09, nullptr);
  int idx = pairOpen(r, g);
  CHECK(r.link->unpair(idx, r.now));
  r.settle();
  CHECK(!r.reg.at(idx).paired);
  CHECK(!g.pairing.contains(CTRL_ID));
  lvp_set_state_t c = {LVP_SET_POWER, 1, 0, 0, 0, 0, 0};
  CHECK_EQ(r.link->setState(idx, c, r.now), LVP_ERR_NOT_PAIRED);
}

static void test_e2e_multiple_gateways() {
  Rig r;
  SimGateway a(0x0A, nullptr), b(0x0B, nullptr);
  a.pairing.openWindow(0, 0xFFFFFFF);
  b.pairing.openWindow(0, 0xFFFFFFF);
  r.net.gws.push_back(&a);
  r.net.gws.push_back(&b);
  r.link->start(r.now);
  r.settle();
  int ia = r.reg.find(a.id()), ib = r.reg.find(b.id());
  CHECK(ia >= 0 && ib >= 0 && ia != ib);
  r.link->pair(ia, nullptr, r.now);
  r.link->pair(ib, nullptr, r.now);
  r.settle();
  CHECK(r.reg.at(ia).paired && r.reg.at(ib).paired);
  lvp_set_state_t on = {LVP_SET_POWER, 1, 0, 0, 0, 0, 0}, off = {LVP_SET_POWER, 0, 0, 0, 0, 0, 0};
  r.link->setState(ia, on, r.now);
  r.link->setState(ib, off, r.now);
  r.settle();
  CHECK_EQ(a.light.snapshot().power, 1);
  CHECK_EQ(b.light.snapshot().power, 0);
  CHECK_EQ(r.reg.at(ia).state.power, 1);
  CHECK_EQ(r.reg.at(ib).state.power, 0);
}

// Zielfarben der Controller-Oberflaeche muessen exakt LeviCal::TARGET (Gateway/Legacy) entsprechen
static void test_calib_targets_match_core() {
  CHECK_EQ(CalTargets::N, LeviCal::N);
  for (int i = 0; i < LeviCal::N; i++) {
    CHECK_EQ(CalTargets::list[i].r, LeviCal::TARGET[i][0]);
    CHECK_EQ(CalTargets::list[i].g, LeviCal::TARGET[i][1]);
    CHECK_EQ(CalTargets::list[i].b, LeviCal::TARGET[i][2]);
  }
}

void tests_controller(void) {
  RUN(test_calib_targets_match_core);
  RUN(test_registry_persistence);
  RUN(test_registry_paired_limit);
  RUN(test_zb_endpoint_mapping_stable);
  RUN(test_zb_conversions);
  RUN(test_touch_cal);
  RUN(test_e2e_discovery_pair_control);
  RUN(test_e2e_key_required);
  RUN(test_e2e_pairing_window_closed);
  RUN(test_e2e_retry_on_loss);
  RUN(test_e2e_coalescing_latest_wins);
  RUN(test_e2e_offline_and_resync);
  RUN(test_e2e_notify_from_gateway);
  RUN(test_e2e_calibration);
  RUN(test_e2e_unpair);
  RUN(test_e2e_multiple_gateways);
}
