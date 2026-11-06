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
  struct Sent {
    uint8_t dst0; // letztes Adressbyte des Ziels
    uint8_t type;
    uint16_t seq;
    lvp_set_state_t cmd; // nur bei SET_STATE belegt
  };
  std::vector<Sent> sentLog; // alle Unicast-Sendungen des Controllers (inkl. Wiederholungen)
  int countSent(uint8_t type, int last = -1, int seq = -1) const {
    int n = 0;
    for (auto &e : sentLog) {
      n += e.type == type && (last < 0 || e.dst0 == last) && (seq < 0 || e.seq == seq);
    }
    return n;
  }
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
    lvp_msg_t dm;
    if (lvp_decode(d, n, &dm) == LVP_OK) {
      Sent e = {mac[5], dm.hdr.type, dm.hdr.seq, {}};
      if (dm.hdr.type == LVP_MSG_SET_STATE) {
        e.cmd = dm.u.set_state;
      }
      sentLog.push_back(e);
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

// Zweiter Druck auf den Gateway-Eintrag waehrend einer laufenden Kopplung: kein zweiter PAIR_REQ
static void test_e2e_pair_busy_while_pending() {
  Rig r;
  SimGateway g(0x0A, nullptr);
  g.pairing.openWindow(0, 0xFFFFFFF);
  r.net.gws.push_back(&g);
  r.link->start(r.now);
  r.settle();
  int idx = r.reg.find(g.id());
  r.net.dropNext = 1; // erste Sendung geht verloren: Kopplung bleibt eine Weile offen
  CHECK_EQ(r.link->pair(idx, nullptr, r.now), LVP_OK);
  r.step();
  CHECK(r.reg.at(idx).pairPhase == PairPhase::Pending);
  CHECK_EQ(r.link->pair(idx, nullptr, r.now), LVP_ERR_BUSY);
  r.settle(100);
  CHECK(r.reg.at(idx).paired);
  // alle Sendungen gehoeren zu derselben Anfrage (Wiederholungen mit gleicher Sequenznummer)
  int pairSends = r.net.countSent(LVP_MSG_PAIR_REQ);
  CHECK(pairSends >= 2);
  uint16_t seq = 0;
  for (auto &e : r.net.sentLog) {
    if (e.type == LVP_MSG_PAIR_REQ) {
      seq = e.seq;
      break;
    }
  }
  CHECK_EQ(r.net.countSent(LVP_MSG_PAIR_REQ, -1, seq), pairSends);
}

// Diagnose-Schnittstelle: Sendungen mit Versuch und Wartezeit, Antwort und endgueltiger Timeout
struct TraceLog : CtrlLink::Trace {
  struct Ev {
    char kind; // 'S' Sendung, 'A' Antwort, 'T' Timeout
    uint8_t type, rsp, error, tryNo, maxTries;
    uint16_t seq;
    uint32_t ms;
  };
  std::vector<Ev> ev;
  void onSend(const uint8_t *, uint8_t type, uint16_t seq, uint8_t tryNo, uint8_t maxTries, uint32_t waitMs) override {
    ev.push_back({'S', type, 0, 0, tryNo, maxTries, seq, waitMs});
  }
  void onAnswer(const uint8_t *, uint8_t reqType, uint8_t rspType, uint8_t error, uint16_t seq, uint8_t tries, uint32_t elapsedMs) override {
    ev.push_back({'A', reqType, rspType, error, tries, 0, seq, elapsedMs});
  }
  void onTimeout(const uint8_t *, uint8_t type, uint16_t seq, uint8_t tries, uint32_t elapsedMs) override {
    ev.push_back({'T', type, 0, 0, tries, 0, seq, elapsedMs});
  }
};

static void test_e2e_trace_pair_retry_and_timeout() {
  Rig r;
  TraceLog t;
  r.link->setTrace(&t);
  SimGateway g(0x0B, nullptr);
  g.pairing.openWindow(0, 0xFFFFFFF);
  r.net.gws.push_back(&g);
  r.link->start(r.now);
  r.settle();
  int idx = r.reg.find(g.id());
  r.net.dropNext = 1;
  CHECK_EQ(r.link->pair(idx, nullptr, r.now), LVP_OK);
  r.settle(100);
  CHECK(r.reg.at(idx).paired);
  // Erstsendung (250 ms), Wiederholung (500 ms), dann PAIR_RSP nach dem zweiten Versuch
  CHECK(t.ev.size() >= 3);
  CHECK(t.ev[0].kind == 'S' && t.ev[0].type == LVP_MSG_PAIR_REQ && t.ev[0].tryNo == 1 && t.ev[0].maxTries == 4);
  CHECK_EQ(t.ev[0].ms, 250u);
  CHECK(t.ev[1].kind == 'S' && t.ev[1].tryNo == 2 && t.ev[1].seq == t.ev[0].seq);
  CHECK_EQ(t.ev[1].ms, 500u);
  CHECK(t.ev[2].kind == 'A' && t.ev[2].type == LVP_MSG_PAIR_REQ && t.ev[2].rsp == LVP_MSG_PAIR_RSP && t.ev[2].tryNo == 2);
  CHECK(t.ev[2].ms >= 250u);

  // Gateway nicht erreichbar: vier Sendungen, dann Timeout nach 250+500+750+1000 ms
  Rig r2;
  TraceLog t2;
  r2.link->setTrace(&t2);
  SimGateway g2(0x0C, nullptr);
  g2.pairing.openWindow(0, 0xFFFFFFF);
  r2.net.gws.push_back(&g2);
  r2.link->start(r2.now);
  r2.settle();
  int idx2 = r2.reg.find(g2.id());
  r2.net.dropNext = 100;
  CHECK_EQ(r2.link->pair(idx2, nullptr, r2.now), LVP_OK);
  r2.settle(400);
  CHECK(r2.reg.at(idx2).pairPhase == PairPhase::Failed);
  CHECK(r2.reg.at(idx2).lastTimeout);
  int sends = 0;
  const TraceLog::Ev *to = nullptr;
  for (auto &e : t2.ev) {
    sends += e.kind == 'S' && e.type == LVP_MSG_PAIR_REQ;
    if (e.kind == 'T') {
      to = &e;
    }
  }
  CHECK_EQ(sends, 4);
  CHECK(to != nullptr);
  if (to) {
    CHECK_EQ(to->type, LVP_MSG_PAIR_REQ);
    CHECK_EQ(to->tryNo, 4);
    CHECK(to->ms >= 2500u && to->ms < 2600u);
  }
  // Nach dem Timeout ist eine neue Kopplung wieder erlaubt
  r2.net.dropNext = 0;
  CHECK_EQ(r2.link->pair(idx2, nullptr, r2.now), LVP_OK);
  r2.settle(100);
  CHECK(r2.reg.at(idx2).paired);
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
  uint32_t rev0 = g.light.snapshot().revision;
  // Schnelle Reglerbewegung: viele Befehle, waehrend einer unterwegs ist
  for (int b = 10; b <= 100; b += 10) {
    lvp_set_state_t c = {LVP_SET_BRIGHTNESS, 0, (uint8_t)b, 0, 0, 0, 0};
    r.link->setState(idx, c, r.now);
  }
  r.settle();
  // Nur der erste (unterwegs) und der letzte (wartend) Befehl erreichen das Gateway
  CHECK_EQ(g.light.snapshot().brightness, 100);
  CHECK_EQ(g.light.snapshot().revision - rev0, 2u);
  CHECK_EQ(r.reg.at(idx).state.brightness, 100);
  CHECK(!r.link->commandPending(idx));
}

// Ein neuer Befehl ersetzt den wartenden vollstaendig: aeltere Felder anderer Befehlstypen werden nicht uebernommen
static void test_e2e_waiting_command_replaced_not_merged() {
  Rig r;
  SimGateway g(0x15, nullptr);
  int idx = pairOpen(r, g);
  uint32_t rev0 = g.light.snapshot().revision;
  lvp_set_state_t a = {LVP_SET_BRIGHTNESS, 0, 10, 0, 0, 0, 0};      // A: unterwegs
  lvp_set_state_t b = {LVP_SET_BRIGHTNESS, 0, 20, 0, 0, 0, 0};      // B
  lvp_set_state_t c = {LVP_SET_POWER, 1, 0, 0, 0, 0, 0};            // C
  lvp_set_state_t d = {LVP_SET_COLOR, 0, 0, 200, 100, 50, 0};       // D: letzter
  CHECK_EQ(r.link->setState(idx, a, r.now), LVP_OK);
  CHECK_EQ(r.link->setState(idx, b, r.now), LVP_OK);
  CHECK_EQ(r.link->setState(idx, c, r.now), LVP_OK);
  CHECK_EQ(r.link->setState(idx, d, r.now), LVP_OK);
  CHECK_EQ(r.net.countSent(LVP_MSG_SET_STATE, 0x15), 1); // nur A wurde gesendet
  r.settle();
  CHECK_EQ(r.net.countSent(LVP_MSG_SET_STATE, 0x15), 2); // A, dann ausschliesslich D
  LightSnapshot s = g.light.snapshot();
  CHECK_EQ(s.revision - rev0, 2u);
  CHECK_EQ(s.brightness, 10);           // B nicht uebernommen
  CHECK(s.power != 1);                  // C nicht uebernommen (kein Zusammenfuehren in D)
  CHECK_EQ(s.r, 200);                   // D
  CHECK_EQ(s.g, 100);
  CHECK_EQ(s.b, 50);
}

// Bekannter Fehlerfall: SET_STATE geht verloren, danach wird eine neuere Abfrage verlangt.
// Die Abfrage darf die Wiederholung nicht ueberholen (sonst verwirft das Gateway sie als veraltet).
static void test_e2e_loss_then_query_does_not_overtake() {
  Rig r;
  SimGateway g(0x16, nullptr);
  int idx = pairOpen(r, g);
  r.net.sentLog.clear();
  uint32_t rev0 = g.light.snapshot().revision;
  r.net.dropNext = 1; // erste Sendung des SET_STATE geht verloren
  lvp_set_state_t c = {LVP_SET_POWER | LVP_SET_BRIGHTNESS, 1, 42, 0, 0, 0, 0};
  CHECK_EQ(r.link->setState(idx, c, r.now), LVP_OK);
  CHECK(r.link->requestCalib(idx, r.now));   // vorgemerkt, nicht gesendet
  CHECK(r.link->requestState(idx, r.now));   // vorgemerkt, nicht gesendet
  CHECK_EQ(r.net.sentLog.size(), 1u);
  r.settle(100);
  // Wiederholung mit derselben Sequenznummer, dann erst die Abfragen
  CHECK(r.net.sentLog.size() >= 3);
  uint16_t seq = r.net.sentLog[0].seq;
  CHECK_EQ(r.net.countSent(LVP_MSG_SET_STATE, 0x16), 2);
  CHECK_EQ(r.net.countSent(LVP_MSG_SET_STATE, 0x16, seq), 2);
  CHECK(r.net.sentLog[1].type == LVP_MSG_SET_STATE && r.net.sentLog[1].seq == seq);
  CHECK(r.net.sentLog[2].type != LVP_MSG_SET_STATE);
  CHECK_EQ(g.light.snapshot().revision - rev0, 1u); // genau einmal ausgefuehrt, nicht als veraltet verworfen
  CHECK_EQ(g.light.snapshot().brightness, 42);
  CHECK_EQ(r.reg.at(idx).state.brightness, 42);
  CHECK_EQ(r.net.countSent(LVP_MSG_GET_CALIB, 0x16), 1); // Bedarf zusammengefasst
  CHECK_EQ(r.net.countSent(LVP_MSG_GET_STATE, 0x16), 0); // die STATE-Antwort des Befehls deckt den Bedarf ab
}

// Abfragebedarf staut sich nicht: viele Anforderungen waehrend einer offenen Anfrage -> je Art eine Abfrage
static void test_e2e_query_demand_coalesced() {
  Rig r;
  SimGateway g(0x17, nullptr);
  int idx = pairOpen(r, g);
  r.net.sentLog.clear();
  lvp_set_state_t c = {LVP_SET_POWER, 1, 0, 0, 0, 0, 0};
  r.link->setState(idx, c, r.now);
  for (int i = 0; i < 20; i++) {
    r.link->requestState(idx, r.now);
    r.link->requestCalib(idx, r.now);
  }
  r.settle(100);
  CHECK_EQ(r.net.countSent(LVP_MSG_SET_STATE, 0x17), 1);
  CHECK(r.net.countSent(LVP_MSG_GET_STATE, 0x17) <= 1); // STATE der Antwort deckt den Bedarf bereits ab
  CHECK_EQ(r.net.countSent(LVP_MSG_GET_CALIB, 0x17), 1);
}

// Endgueltiger Timeout: danach folgt ausschliesslich der letzte wartende Befehl; der alte kehrt nie zurueck
static void test_e2e_final_timeout_then_last_waiting() {
  Rig r;
  SimGateway g(0x18, nullptr);
  int idx = pairOpen(r, g);
  r.net.sentLog.clear();
  uint32_t rev0 = g.light.snapshot().revision;
  g.online = false; // Gateway antwortet nicht
  lvp_set_state_t a = {LVP_SET_BRIGHTNESS, 0, 11, 0, 0, 0, 0}, b = {LVP_SET_BRIGHTNESS, 0, 22, 0, 0, 0, 0},
                  c = {LVP_SET_COLOR, 0, 0, 9, 8, 7, 0};
  r.link->setState(idx, a, r.now);
  uint16_t seqA = r.net.sentLog[0].seq;
  r.link->setState(idx, b, r.now);
  r.link->setState(idx, c, r.now);
  // bis zum endgueltigen Timeout von A (250+500+750+1000 ms)
  for (int i = 0; i < 400 && r.net.countSent(LVP_MSG_SET_STATE, 0x18) < 5; i++) {
    r.step(10);
  }
  CHECK_EQ(r.net.countSent(LVP_MSG_SET_STATE, 0x18, seqA), 4); // begrenzt: 4 Sendungen mit gleicher Sequenz
  CHECK(r.reg.at(idx).lastTimeout);
  CHECK(r.link->commandPending(idx)); // C ist jetzt unterwegs
  g.online = true;
  r.settle(500);
  CHECK_EQ(r.net.countSent(LVP_MSG_SET_STATE, 0x18, seqA), 4); // A wird nie wieder gesendet
  LightSnapshot s = g.light.snapshot();
  CHECK_EQ(s.revision - rev0, 1u); // nur C
  CHECK(s.brightness != 11 && s.brightness != 22);
  CHECK_EQ(s.r, 9);
  CHECK(!r.link->commandPending(idx));
}

// Gateways werden unabhaengig behandelt: ein haengendes Gateway blockiert das andere nicht
static void test_e2e_gateways_independent_queues() {
  Rig r;
  SimGateway a(0x1A, nullptr), b(0x1B, nullptr);
  a.pairing.openWindow(0, 0xFFFFFFF);
  b.pairing.openWindow(0, 0xFFFFFFF);
  r.net.gws.push_back(&a);
  r.net.gws.push_back(&b);
  r.link->start(r.now);
  r.settle();
  int ia = r.reg.find(a.id()), ib = r.reg.find(b.id());
  r.link->pair(ia, nullptr, r.now);
  r.link->pair(ib, nullptr, r.now);
  r.settle();
  CHECK(r.reg.at(ia).paired && r.reg.at(ib).paired);
  r.net.sentLog.clear();
  uint32_t revB = b.light.snapshot().revision;
  a.online = false;
  lvp_set_state_t a1 = {LVP_SET_BRIGHTNESS, 0, 10, 0, 0, 0, 0}, a2 = {LVP_SET_BRIGHTNESS, 0, 20, 0, 0, 0, 0},
                  b1 = {LVP_SET_BRIGHTNESS, 0, 30, 0, 0, 0, 0}, b2 = {LVP_SET_BRIGHTNESS, 0, 40, 0, 0, 0, 0};
  r.link->setState(ia, a1, r.now);
  r.link->setState(ib, b1, r.now); // sofort unterwegs, obwohl A offen ist
  CHECK_EQ(r.net.countSent(LVP_MSG_SET_STATE, 0x1A), 1);
  CHECK_EQ(r.net.countSent(LVP_MSG_SET_STATE, 0x1B), 1);
  r.link->setState(ia, a2, r.now); // wartet bei A
  r.link->setState(ib, b2, r.now); // wartet bei B
  r.settle(20);
  CHECK_EQ(b.light.snapshot().brightness, 40);
  CHECK_EQ(b.light.snapshot().revision - revB, 2u);
  CHECK(r.link->commandPending(ia)); // A haengt weiter, B ist fertig
  CHECK(!r.link->commandPending(ib));
  a.online = true;
  r.settle(500);
  CHECK_EQ(a.light.snapshot().brightness, 20);
  CHECK_EQ(r.reg.at(ia).state.brightness, 20);
}

// Kalibrierung speichern darf nicht durch Lichtbefehle (Farbwiederherstellung) verdraengt werden
static void test_e2e_calib_save_then_restore_color() {
  Rig r;
  SimGateway g(0x19, nullptr);
  int idx = pairOpen(r, g);
  uint8_t pts[30];
  for (int i = 0; i < 30; i++) {
    pts[i] = (uint8_t)(50 + i);
  }
  // Vorschau (roh) ist unterwegs, Speichern wartet, Farbwiederherstellung wird abgewiesen -> Speichern bleibt erhalten
  lvp_set_state_t preview = {LVP_SET_RAW, 0, 0, 10, 20, 30, 0}, restore = {LVP_SET_COLOR, 0, 0, 255, 0, 0, 0};
  CHECK_EQ(r.link->setState(idx, preview, r.now), LVP_OK);
  CHECK(r.link->sendCalib(idx, pts, r.now));
  CHECK(r.link->calibPending(idx));
  CHECK_EQ(r.link->setState(idx, restore, r.now), LVP_ERR_BUSY);
  CHECK(!r.link->sendCalib(idx, pts, r.now));  // zweiter Vorgang ersetzt den ersten nicht stillschweigend
  CHECK(!r.link->resetCalib(idx, r.now));
  r.settle();
  CHECK(g.cal.custom());
  uint8_t got[30];
  g.cal.points(got);
  CHECK_EQ(got[29], 79);
  CHECK(!r.link->calibPending(idx));
  CHECK_EQ(r.link->setState(idx, restore, r.now), LVP_OK); // danach moeglich
  r.settle();
  CHECK_EQ(g.light.snapshot().r, 255);
  CHECK_EQ(r.reg.at(idx).calib.custom, 1);

  // Speichern ist unterwegs: die Wiederherstellung wartet und wird danach ausgefuehrt
  uint8_t pts2[30];
  for (int i = 0; i < 30; i++) {
    pts2[i] = (uint8_t)(90 + i);
  }
  CHECK(r.link->sendCalib(idx, pts2, r.now));
  lvp_set_state_t restore2 = {LVP_SET_COLOR, 0, 0, 1, 2, 3, 0};
  CHECK_EQ(r.link->setState(idx, restore2, r.now), LVP_OK);
  r.settle();
  g.cal.points(got);
  CHECK_EQ(got[0], 90);
  CHECK_EQ(g.light.snapshot().b, 3);
}

// Die n-te gesendete SET_STATE-Sendung an dieses Gateway (0-basiert)
static const SimNet::Sent *nthSet(const SimNet &n, uint8_t last, int k) {
  for (auto &e : n.sentLog) {
    if (e.type == LVP_MSG_SET_STATE && e.dst0 == last && k-- == 0) {
      return &e;
    }
  }
  return nullptr;
}

static void settleSets(Rig &r, SimGateway &g, int idx) {
  lvp_set_state_t on = {LVP_SET_MUSIC, 0, 0, 0, 0, 0, 1};
  CHECK_EQ(r.link->setState(idx, on, r.now), LVP_OK);
  r.settle();
  CHECK(g.light.snapshot().music);
}

// Musik an wartet, danach kalibrierte Farbe: nur die Farbe wartet weiter, ohne uebernommenes MUSIC-Feld.
static void test_e2e_waiting_music_then_color_music_stays_off() {
  Rig r;
  SimGateway g(0x20, nullptr);
  int idx = pairOpen(r, g);
  settleSets(r, g, idx); // Gateway: Musik laeuft
  r.net.sentLog.clear();
  lvp_set_state_t a = {LVP_SET_BRIGHTNESS, 0, 55, 0, 0, 0, 0}, music = {LVP_SET_MUSIC, 0, 0, 0, 0, 0, 1},
                  color = {LVP_SET_COLOR, 0, 0, 10, 20, 30, 0};
  r.link->setState(idx, a, r.now);     // unterwegs
  r.link->setState(idx, music, r.now); // wartet
  r.link->setState(idx, color, r.now); // ersetzt "Musik an" vollstaendig
  r.settle();
  const SimNet::Sent *second = nthSet(r.net, 0x20, 1);
  CHECK(second != nullptr);
  if (second) {
    CHECK_EQ(second->cmd.mask, LVP_SET_COLOR); // kein MUSIC-Feld uebernommen
    CHECK_EQ(second->cmd.music, 0);
  }
  CHECK_EQ(r.net.countSent(LVP_MSG_SET_STATE, 0x20), 2);
  CHECK(!g.light.snapshot().music); // kalibrierte Farbe beendet den Musikmodus
  CHECK(!(r.reg.at(idx).state.flags & LVP_STATE_MUSIC)); // Anzeige = bestaetigter Zustand
  CHECK_EQ(g.light.snapshot().r, 10);
}

// Farbe wartet, danach Helligkeit: nur die Helligkeit bleibt erhalten
static void test_e2e_waiting_color_then_brightness_only_brightness() {
  Rig r;
  SimGateway g(0x21, nullptr);
  int idx = pairOpen(r, g);
  r.net.sentLog.clear();
  uint32_t colorRev0 = g.light.snapshot().colorRev;
  lvp_set_state_t a = {LVP_SET_POWER, 1, 0, 0, 0, 0, 0}, color = {LVP_SET_COLOR, 0, 0, 1, 2, 3, 0},
                  br = {LVP_SET_BRIGHTNESS, 0, 33, 0, 0, 0, 0};
  r.link->setState(idx, a, r.now);
  r.link->setState(idx, color, r.now);
  r.link->setState(idx, br, r.now);
  r.settle();
  const SimNet::Sent *second = nthSet(r.net, 0x21, 1);
  CHECK(second != nullptr);
  if (second) {
    CHECK_EQ(second->cmd.mask, LVP_SET_BRIGHTNESS);
  }
  CHECK_EQ(g.light.snapshot().colorRev, colorRev0); // Farbe wurde nie gesendet
  CHECK_EQ(g.light.snapshot().brightness, 33);
}

// Roh <-> kalibriert: der letzte wartende Befehl bleibt unveraendert; Musik bleibt bei RAW, endet bei COLOR
static void test_e2e_waiting_raw_calibrated_switch_last_wins() {
  Rig r;
  SimGateway g(0x22, nullptr);
  int idx = pairOpen(r, g);
  settleSets(r, g, idx);
  r.net.sentLog.clear();
  lvp_set_state_t a = {LVP_SET_POWER, 1, 0, 0, 0, 0, 0}, raw = {LVP_SET_RAW, 0, 0, 5, 6, 7, 0},
                  cal = {LVP_SET_COLOR, 0, 0, 8, 9, 10, 0}, raw2 = {LVP_SET_RAW, 0, 0, 11, 12, 13, 0};
  r.link->setState(idx, a, r.now);
  r.link->setState(idx, raw, r.now);
  r.link->setState(idx, cal, r.now);
  r.link->setState(idx, raw2, r.now); // letzter: roh, darf weder COLOR noch Werte des Vorgaengers enthalten
  r.settle();
  const SimNet::Sent *second = nthSet(r.net, 0x22, 1);
  CHECK(second != nullptr);
  if (second) {
    CHECK_EQ(second->cmd.mask, LVP_SET_RAW);
    CHECK_EQ(second->cmd.r, 11);
  }
  CHECK(g.light.snapshot().raw);
  CHECK(g.light.snapshot().music); // RAW beendet Musik nicht; COLOR (ersetzt) wurde nie gesendet
  CHECK_EQ(g.light.snapshot().r, 11);

  // Umgekehrt: roh wartet, dann kalibriert -> Musik aus, nicht roh
  r.net.sentLog.clear();
  r.link->setState(idx, a, r.now);
  r.link->setState(idx, raw, r.now);
  r.link->setState(idx, cal, r.now);
  r.settle();
  second = nthSet(r.net, 0x22, 1);
  CHECK(second != nullptr);
  if (second) {
    CHECK_EQ(second->cmd.mask, LVP_SET_COLOR);
  }
  CHECK(!g.light.snapshot().raw);
  CHECK(!g.light.snapshot().music);
}

// Paketverlust und Wiederholungen aendern weder Inhalt noch Bedeutung des letzten wartenden Befehls
static void test_e2e_loss_keeps_waiting_command_identical() {
  Rig r;
  SimGateway g(0x23, nullptr);
  int idx = pairOpen(r, g);
  settleSets(r, g, idx);
  r.net.sentLog.clear();
  uint32_t rev0 = g.light.snapshot().revision;
  lvp_set_state_t a = {LVP_SET_BRIGHTNESS, 0, 60, 0, 0, 0, 0}, music = {LVP_SET_MUSIC, 0, 0, 0, 0, 0, 1},
                  color = {LVP_SET_COLOR, 0, 0, 40, 50, 60, 0};
  r.net.dropNext = 5; // A: alle 4 Sendungen verloren (endgueltiger Timeout), erste Sendung des wartenden Befehls ebenfalls
  r.link->setState(idx, a, r.now);
  r.link->setState(idx, music, r.now);
  r.link->setState(idx, color, r.now);
  r.settle(450);
  CHECK_EQ(r.net.countSent(LVP_MSG_SET_STATE, 0x23), 6); // 4x A, 2x der letzte Befehl
  const SimNet::Sent *w1 = nthSet(r.net, 0x23, 4), *w2 = nthSet(r.net, 0x23, 5);
  CHECK(w1 && w2);
  if (w1 && w2) {
    CHECK_EQ(w1->seq, w2->seq); // Wiederholung: gleiche Sequenznummer
    CHECK_EQ(w1->cmd.mask, LVP_SET_COLOR);
    CHECK_EQ(w2->cmd.mask, LVP_SET_COLOR);
    CHECK(w1->seq != nthSet(r.net, 0x23, 0)->seq);
    CHECK(w2->cmd.r == 40 && w2->cmd.g == 50 && w2->cmd.b == 60 && w2->cmd.music == 0);
  }
  CHECK_EQ(g.light.snapshot().revision - rev0, 1u); // nur der Farbbefehl, genau einmal
  CHECK(!g.light.snapshot().music);
  CHECK_EQ(g.light.snapshot().brightness, 100); // A nie angekommen und nicht erneut eingereiht
}

// Ein bewusst zusammengesetzter einzelner SET_STATE behaelt seine Semantik (Farbe, Musik aus, dann Musikfeld)
static void test_e2e_single_composite_set_state_semantics() {
  Rig r;
  SimGateway g(0x24, nullptr);
  int idx = pairOpen(r, g);
  r.net.sentLog.clear();
  // direkt gesendet (nichts unterwegs)
  lvp_set_state_t both = {LVP_SET_COLOR | LVP_SET_MUSIC, 0, 0, 1, 2, 3, 1};
  CHECK_EQ(r.link->setState(idx, both, r.now), LVP_OK);
  r.settle();
  CHECK(g.light.snapshot().music); // ausdrueckliches Musikfeld wirkt nach der Farbe
  CHECK_EQ(g.light.snapshot().r, 1);

  // als wartender Befehl: unveraendert uebernommen
  lvp_set_state_t a = {LVP_SET_POWER, 1, 0, 0, 0, 0, 0}, off = {LVP_SET_COLOR | LVP_SET_MUSIC, 0, 0, 4, 5, 6, 0};
  r.link->setState(idx, a, r.now);
  r.link->setState(idx, both, r.now);
  r.link->setState(idx, off, r.now); // ersetzt "both" vollstaendig
  r.settle();
  const SimNet::Sent *last = nthSet(r.net, 0x24, 2);
  CHECK(last != nullptr);
  if (last) {
    CHECK_EQ(last->cmd.mask, LVP_SET_COLOR | LVP_SET_MUSIC);
    CHECK_EQ(last->cmd.music, 0);
  }
  CHECK(!g.light.snapshot().music);
  CHECK_EQ(g.light.snapshot().r, 4);
}

// Verlorene Kalibrierung speichern: endgueltiger Timeout wird gemeldet (nicht als Erfolg), nichts wird spaeter erneut gesendet
static void test_e2e_calib_save_timeout_reported() {
  Rig r;
  SimGateway g(0x1C, nullptr);
  int idx = pairOpen(r, g);
  r.net.sentLog.clear();
  uint8_t pts[30] = {0};
  g.online = false;
  CHECK(r.link->sendCalib(idx, pts, r.now));
  for (int i = 0; i < 400 && r.link->calibPending(idx) && r.net.countSent(LVP_MSG_SET_CALIB, 0x1C) <= 4; i++) {
    r.step(10);
  }
  CHECK(r.reg.at(idx).lastTimeout); // UI: "Keine Antwort" statt "Gespeichert"
  CHECK_EQ(r.net.countSent(LVP_MSG_SET_CALIB, 0x1C), 4);
  g.online = true;
  r.settle(400);
  CHECK_EQ(r.net.countSent(LVP_MSG_SET_CALIB, 0x1C), 4); // nicht erneut eingereiht
  CHECK(!g.cal.custom());
  CHECK_EQ(r.reg.at(idx).calib.custom, 0); // gespeicherte Kalibrierung neu gelesen
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
  RUN_D(test_e2e_pair_busy_while_pending, "Zweiter Kopplungsversuch waehrend einer laufenden Kopplung wird abgewiesen, kein zweiter PAIR_REQ");
  RUN_D(test_e2e_trace_pair_retry_and_timeout, "Diagnose meldet Versuche mit Wartezeit, Antwort und endgueltigen Timeout");
  RUN_D(test_e2e_coalescing_latest_wins, "Viele Befehle waehrend einer offenen Anfrage: nur erster (unterwegs) und letzter (wartend) erreichen das Gateway");
  RUN_D(test_e2e_waiting_command_replaced_not_merged, "A unterwegs, B/C/D folgen: ausschliesslich D; keine Felder aelterer Befehle uebernommen");
  RUN_D(test_e2e_loss_then_query_does_not_overtake, "SET_STATE geht verloren, GET_CALIB/GET_STATE ueberholen die Wiederholung nicht (kein STALE)");
  RUN_D(test_e2e_query_demand_coalesced, "Abfragebedarf wird zusammengefasst, kein Rueckstau");
  RUN_D(test_e2e_final_timeout_then_last_waiting, "Endgueltiger Timeout: danach nur der letzte wartende Befehl, der alte kehrt nie zurueck");
  RUN_D(test_e2e_gateways_independent_queues, "Zwei Gateways werden unabhaengig behandelt");
  RUN_D(test_e2e_calib_save_then_restore_color, "Kalibrierung speichern wird von der Farbwiederherstellung nicht verdraengt");
  RUN_D(test_e2e_calib_save_timeout_reported, "Timeout beim Speichern der Kalibrierung wird gemeldet, nicht als Erfolg");
  RUN_D(test_e2e_waiting_music_then_color_music_stays_off, "Wartendes 'Musik an' + kalibrierte Farbe: Musik bleibt aus");
  RUN_D(test_e2e_waiting_color_then_brightness_only_brightness, "Wartende Farbe + Helligkeit: nur Helligkeit");
  RUN_D(test_e2e_waiting_raw_calibrated_switch_last_wins, "Wechsel roh/kalibriert: letzter wartender Befehl unveraendert");
  RUN_D(test_e2e_loss_keeps_waiting_command_identical, "Verlust/Wiederholung aendert Inhalt des wartenden Befehls nicht");
  RUN_D(test_e2e_single_composite_set_state_semantics, "Einzelner SET_STATE mit mehreren Feldern behaelt seine Semantik");
  RUN(test_e2e_offline_and_resync);
  RUN(test_e2e_notify_from_gateway);
  RUN(test_e2e_calibration);
  RUN(test_e2e_unpair);
  RUN(test_e2e_multiple_gateways);
}
