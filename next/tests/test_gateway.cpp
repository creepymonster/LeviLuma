// Host-Tests: Gateway-Logik (LightState, BLE-Befehlsreihenfolge, Kalibrierung mit NVS-Ersatz,
// Kopplungstabelle und vollstaendiger Protokollablauf Controller -> Gateway).
#include "band_memory.h"
#include <math.h>
#include "test_util.h"
#include "ble_scheduler.h"
#include "calib_store.h"
#include "gateway_handler.h"
#include "kv_store.h"
#include "levi_key.h"
#include "light_model.h"
#include "pairing_table.h"

static const uint8_t CTRL[6] = {0x24, 0x0A, 0xC4, 0x00, 0x00, 0x01};
static const uint8_t CTRL2[6] = {0x24, 0x0A, 0xC4, 0x00, 0x00, 0x02};

// ---- LightModel ----

static void test_light_model_semantics() {
  LightModel m;
  CHECK_EQ(m.snapshot().power, -1); // unbekannt nach dem Start (wie Legacy)
  CHECK_EQ(m.snapshot().brightness, 100);

  lvp_set_state_t c = {LVP_SET_POWER, 1, 0, 0, 0, 0, 0};
  CHECK_EQ(m.apply(c), LVP_OK);
  CHECK_EQ(m.snapshot().power, 1);
  CHECK_EQ(m.snapshot().revision, 1);

  c = {LVP_SET_MUSIC, 0, 0, 0, 0, 0, 1};
  m.apply(c);
  CHECK(m.snapshot().music);
  // Kalibrierte Farbe beendet Musik (Legacy setRGB)
  c = {LVP_SET_COLOR, 0, 0, 10, 20, 30, 0};
  m.apply(c);
  CHECK(!m.snapshot().music);
  CHECK(!m.snapshot().raw);
  CHECK_EQ(m.snapshot().colorRev, 1);
  // Rohe Farbe laesst Musik unveraendert (Legacy setRaw)
  c = {LVP_SET_MUSIC, 0, 0, 0, 0, 0, 1};
  m.apply(c);
  c = {LVP_SET_RAW, 0, 0, 1, 2, 3, 0};
  m.apply(c);
  CHECK(m.snapshot().music);
  CHECK(m.snapshot().raw);
  CHECK_EQ(m.snapshot().colorRev, 2);
  uint32_t rev = m.snapshot().revision;
  // Ungueltiger Befehl aendert nichts
  c = {LVP_SET_BRIGHTNESS, 0, 0, 0, 0, 0, 0};
  CHECK_EQ(m.apply(c), LVP_ERR_OUT_OF_RANGE);
  CHECK_EQ(m.snapshot().revision, rev);
  // Mehrere Felder = eine Revision
  c = {LVP_SET_POWER | LVP_SET_BRIGHTNESS, 0, 42, 0, 0, 0, 0};
  m.apply(c);
  CHECK_EQ(m.snapshot().revision, rev + 1);
  CHECK_EQ(m.snapshot().brightness, 42);
  m.markSent(rev);
  m.markSent(rev - 1); // sentRevision faellt nie
  CHECK_EQ(m.snapshot().sentRevision, rev);
  m.setBle(true);
  lvp_state_t p = m.toProto();
  CHECK_EQ(p.flags & LVP_STATE_BLE, LVP_STATE_BLE);
  CHECK_EQ(p.flags & LVP_STATE_MUSIC, LVP_STATE_MUSIC);
  CHECK_EQ(p.power, 0);
}

// ---- BLE-Befehlsreihenfolge (Legacy bleTask) ----

static LeviCal::Model defaultModel() {
  LeviCal::Model cm;
  LeviCal::defaultPts(cm, 1.0f, 0.69f, 0.94f);
  LeviCal::solve(cm);
  return cm;
}

static void test_scheduler_order_after_connect() {
  LeviCal::Model cm = defaultModel();
  BleScheduler s;
  LightSnapshot st;
  st.power = 1;
  st.colorRev = 1;
  s.onConnected(st);
  BleScheduler::Step step;
  BleScheduler::Cmd expected[] = {BleScheduler::Cmd::Music, BleScheduler::Cmd::Brightness, BleScheduler::Cmd::Power, BleScheduler::Cmd::Color};
  uint32_t t = 1000;

  for (BleScheduler::Cmd e : expected) {
    CHECK(s.next(st, cm, t, step));
    CHECK(step.cmd == e);
    s.commit(step, true, st, t);
    t += 225;
  }
  CHECK(!s.next(st, cm, t, step)); // alles gesendet
  CHECK(s.fadeDone());
  // Nach Verbinden wird Musik-aus erzwungen (Paket 7E 04 07 00 ...)
}

static void test_scheduler_packets_and_brightness_scale() {
  LeviCal::Model cm = defaultModel();
  BleScheduler::Config cfg;
  cfg.brightnessMax = 255;
  BleScheduler s(cfg);
  LightSnapshot st;
  st.power = 1;
  st.brightness = 50;
  s.onConnected(st);
  BleScheduler::Step step;
  s.next(st, cm, 0, step); // Musik aus
  CHECK_EQ(step.packet[2], 0x07);
  CHECK_EQ(step.packet[3], 0x00);
  s.commit(step, true, st, 0);
  s.next(st, cm, 0, step);
  CHECK(step.cmd == BleScheduler::Cmd::Brightness);
  CHECK_EQ(step.packet[3], 128); // 50 % auf 0..255
  s.commit(step, false, st, 0);  // Schreibfehler: erneut versuchen
  CHECK(s.next(st, cm, 0, step));
  CHECK(step.cmd == BleScheduler::Cmd::Brightness);
}

static void test_scheduler_color_calibrated_and_raw_throttle() {
  LeviCal::Model cm = defaultModel();
  BleScheduler s;
  LightSnapshot st;
  st.power = 1;
  st.r = 255;
  st.g = 255;
  st.b = 255;
  st.colorRev = 5;
  s.onConnected(st);
  BleScheduler::Step step;
  uint32_t t = 10000;
  while (s.next(st, cm, t, step) && step.cmd != BleScheduler::Cmd::Color) {
    s.commit(step, true, st, t);
  }
  CHECK(step.cmd == BleScheduler::Cmd::Color);
  // Weiss mit Standardkalibrierung: G = 255 * 0.69, B = 255 * 0.94
  CHECK_EQ(step.packet[4], 255);
  CHECK_EQ(step.packet[5], 176);
  CHECK_EQ(step.packet[6], 240);
  s.commit(step, true, st, t);

  // Rohe Farbe: ohne Kalibrierung, hoechstens alle 400 ms
  st.raw = true;
  st.r = 10;
  st.g = 20;
  st.b = 30;
  st.colorRev = 6;
  CHECK(s.next(st, cm, t + 1, step));
  CHECK_EQ(step.packet[4], 10);
  CHECK_EQ(step.packet[5], 20);
  s.commit(step, true, st, t + 1);
  st.colorRev = 7;
  CHECK(!s.next(st, cm, t + 200, step)); // gedrosselt
  CHECK(s.next(st, cm, t + 401, step));
}

static void test_scheduler_music_eq_and_power_off() {
  LeviCal::Model cm = defaultModel();
  BleScheduler s;
  LightSnapshot st;
  st.power = 1;
  st.colorRev = 1;
  s.onConnected(st);
  BleScheduler::Step step;
  uint32_t t = 5000;
  while (s.next(st, cm, t, step)) {
    s.commit(step, true, st, t);
  }
  st.music = true;
  CHECK(s.next(st, cm, t, step));
  CHECK(step.cmd == BleScheduler::Cmd::Music);
  s.commit(step, true, st, t);
  CHECK(s.next(st, cm, t, step));
  CHECK(step.cmd == BleScheduler::Cmd::MicEq); // EQ direkt nach Musik an
  s.commit(step, true, st, t);
  // Waehrend Musik keine Farbe
  st.colorRev = 2;
  CHECK(!s.next(st, cm, t, step));
  // Aus: Power-Paket, Farbe wird erst nach Einschalten gesendet
  st.music = false;
  st.power = 0;
  while (s.next(st, cm, t, step)) {
    CHECK(step.cmd != BleScheduler::Cmd::Color);
    s.commit(step, true, st, t);
  }
}

static void test_scheduler_fade() {
  LeviCal::Model cm = defaultModel();
  BleScheduler::Config cfg;
  cfg.fadeSteps = 3;
  BleScheduler s(cfg);
  LightSnapshot st;
  st.power = 1;
  st.r = 255;
  st.g = 0;
  st.b = 0;
  st.colorRev = 1;
  s.onConnected(st);
  BleScheduler::Step step;
  while (s.next(st, cm, 0, step)) {
    s.commit(step, true, st, 0); // erste Farbe sofort (instantColor)
  }
  st.r = 0;
  st.b = 255;
  st.colorRev = 2;
  int colors = 0;
  while (s.next(st, cm, 0, step)) {
    CHECK(step.cmd == BleScheduler::Cmd::Color);
    CHECK_EQ(step.fadeN, 3);
    s.commit(step, true, st, 0);
    colors++;
  }
  CHECK_EQ(colors, 3);
}

// ---- Kalibrierung und NVS ----

static void test_calib_store_persistence() {
  MemKvStore kv;
  CalibStore::Defaults d;
  CalibStore cs(kv, d);
  cs.load();
  CHECK(!cs.custom());
  uint8_t pts[30];
  cs.points(pts);
  // Standard = lineare Gains auf die Zielfarben (Weiss -> 255,176,240)
  CHECK_EQ(pts[0], 255);
  CHECK_EQ(pts[1], 176);
  CHECK_EQ(pts[2], 240);
  uint16_t rev0 = cs.revision();

  for (int i = 0; i < 30; i++) {
    pts[i] = (uint8_t)(LeviCal::TARGET[i / 3][i % 3]); // Identitaet
  }
  CHECK(cs.save(pts));
  CHECK(cs.custom());
  CHECK(cs.revision() != rev0);
  // Identitaet: apply gibt die Eingabe zurueck
  uint8_t out[3];
  LeviCal::apply(cs.model(), 255, 64, 64, out);
  CHECK(abs(out[0] - 255) <= 1 && abs(out[1] - 64) <= 1 && abs(out[2] - 64) <= 1);

  // Neustart: aus dem Speicher laden
  CalibStore cs2(kv, d);
  cs2.load();
  CHECK(cs2.custom());
  CHECK_EQ(cs2.revision(), cs.revision());
  uint8_t p2[30];
  cs2.points(p2);
  CHECK(memcmp(p2, pts, 30) == 0);

  // Schreibfehler: Modell bleibt unveraendert
  kv.failWrites = true;
  uint8_t other[30];
  memset(other, 7, 30);
  CHECK(!cs2.save(other));
  cs2.points(p2);
  CHECK(memcmp(p2, pts, 30) == 0);
  CHECK(!cs2.reset());
  kv.failWrites = false;

  CHECK(cs2.reset());
  CHECK(!cs2.custom());
  CalibStore cs3(kv, d);
  cs3.load();
  CHECK(!cs3.custom());

  // Beschaedigter Eintrag (falsche Laenge) -> Standard
  kv.data[CalibStore::KEY_POINTS] = std::vector<uint8_t>(12, 1);
  cs3.load();
  CHECK(!cs3.custom());
}

static void test_calib_defaults_points() {
  MemKvStore kv;
  CalibStore::Defaults d;
  d.hasPoints = true;
  for (int i = 0; i < 30; i++) {
    d.points[i] = (uint8_t)i;
  }
  CalibStore cs(kv, d);
  cs.load();
  uint8_t p[30];
  cs.points(p);
  CHECK_EQ(p[29], 29);
}

// Legacy-Semantik: identische Ergebnisse wie CalibCore direkt (Kopie unveraendert)
static void test_calib_matches_legacy_core() {
  LeviCal::Model m;
  LeviCal::defaultPts(m, 1.0f, 0.69f, 0.94f);
  LeviCal::solve(m);
  uint8_t out[3];
  LeviCal::apply(m, 255, 0, 0, out);
  CHECK_EQ(out[0], 255);
  CHECK(out[1] <= 1 && out[2] <= 1);
  LeviCal::apply(m, 0, 0, 0, out);
  CHECK_EQ(out[0] + out[1] + out[2], 0);
  // dunkle Farbe (unter 30 %): normalisierter Pfad, Verhaeltnis bleibt
  LeviCal::apply(m, 40, 40, 40, out);
  CHECK(out[0] >= 38 && out[0] <= 42);
}

// ---- Kopplungstabelle ----

static BandMemory::Candidate cand(uint8_t last, int8_t rssi, const char *name, uint8_t type = 0) {
  BandMemory::Candidate c;
  c.addr.type = type;
  const uint8_t v[6] = {last, 0x22, 0x33, 0x44, 0x55, 0x66};
  memcpy(c.addr.val, v, 6);
  c.rssi = rssi;
  c.name = name;
  return c;
}

static void test_band_memory_choose_and_persist() {
  MemKvStore kv;
  BandMemory m(kv);
  m.load();
  CHECK(!m.has());
  CHECK(BandMemory::nameMatches("ELK-BLEDDM 96"));
  CHECK(BandMemory::nameMatches("MELK-OA10"));
  CHECK(!BandMemory::nameMatches(""));
  CHECK(!BandMemory::nameMatches(nullptr));
  CHECK(!BandMemory::nameMatches("BLE MF8470"));

  // Nichts gemerkt: staerkstes passendes Geraet, Geraete ohne passenden Namen zaehlen nicht
  BandMemory::Candidate c[] = {cand(1, -40, ""), cand(2, -87, "ELK-BLEDDM 96"), cand(3, -60, "LEDBLE-1"), cand(4, -30, "Fridge")};
  BandMemory::Choice ch = m.choose(c, 4);
  CHECK(ch.reason == BandMemory::Reason::Strongest);
  CHECK_EQ(ch.index, 2);
  CHECK_EQ(ch.ignored, 0);
  BandMemory::Candidate none[] = {cand(1, -40, ""), cand(4, -30, "Fridge")};
  CHECK(m.choose(none, 2).reason == BandMemory::Reason::None);
  CHECK(m.choose(nullptr, 0).reason == BandMemory::Reason::None);

  // Merken und nach "Neustart" wieder laden
  CHECK(m.remember(c[1].addr));
  BandMemory m2(kv);
  m2.load();
  CHECK(m2.has());
  CHECK(BandMemory::sameAddr(m2.addr(), c[1].addr));
  char s[18];
  BandMemory::addrToStr(m2.addr(), s, sizeof(s));
  CHECK(strcmp(s, "66:55:44:33:22:02") == 0);

  // Gemerkt: nur dieses Band, auch schwaecher und ohne empfangenen Namen; andere passende werden ignoriert
  BandMemory::Candidate c2[] = {cand(3, -40, "LEDBLE-1"), cand(2, -95, ""), cand(5, -50, "ELK-BLEDOM")};
  ch = m2.choose(c2, 3);
  CHECK(ch.reason == BandMemory::Reason::Remembered);
  CHECK_EQ(ch.index, 1);
  CHECK_EQ(ch.ignored, 2);

  // Gemerktes Band fehlt: keine Ausweichwahl
  BandMemory::Candidate c3[] = {cand(3, -40, "LEDBLE-1"), cand(5, -50, "ELK-BLEDOM")};
  ch = m2.choose(c3, 2);
  CHECK(ch.reason == BandMemory::Reason::RememberedMissing);
  CHECK_EQ(ch.index, -1);
  CHECK_EQ(ch.ignored, 2);
  CHECK(m2.choose(nullptr, 0).reason == BandMemory::Reason::RememberedMissing);

  // Gleiche Adressbytes, anderer Adresstyp: nicht dasselbe Band
  BandMemory::Candidate c4[] = {cand(2, -50, "ELK-BLEDDM 96", 1)};
  CHECK(m2.choose(c4, 1).reason == BandMemory::Reason::RememberedMissing);

  // Vergessen: danach wieder staerkstes passendes Geraet, auch nach erneutem Laden
  CHECK(m2.forget());
  CHECK(!m2.has());
  BandMemory m3(kv);
  m3.load();
  CHECK(!m3.has());
  CHECK(m3.choose(c3, 2).reason == BandMemory::Reason::Strongest);
}

static void test_band_memory_storage_errors() {
  MemKvStore kv;
  BandMemory m(kv);
  m.load();
  BandMemory::Candidate a = cand(7, -50, "ELK-BLEDDM"), b = cand(8, -50, "ELK-BLEDDM");
  CHECK(m.remember(a.addr));
  kv.failWrites = true;
  CHECK(!m.remember(b.addr)); // Speicherfehler: alter Stand bleibt
  CHECK(BandMemory::sameAddr(m.addr(), a.addr));
  kv.failWrites = false;

  // Beschaedigte oder fremde Daten werden nicht als Band uebernommen
  uint8_t bad[BandMemory::BLOB_LEN] = {2, 0, 1, 2, 3, 4, 5, 6}; // unbekannte Version
  CHECK(kv.setBlob(BandMemory::KEY, bad, sizeof(bad)));
  BandMemory m2(kv);
  m2.load();
  CHECK(!m2.has());
  uint8_t shortBlob[3] = {1, 0, 1};
  CHECK(kv.setBlob(BandMemory::KEY, shortBlob, sizeof(shortBlob)));
  m2.load();
  CHECK(!m2.has());
  BandMemory::Addr out;
  CHECK(!BandMemory::deserialize(nullptr, 8, out));
  uint8_t buf[4];
  CHECK_EQ(BandMemory::serialize(a.addr, buf, sizeof(buf)), (size_t)0);
}

static void test_pairing_table() {
  MemKvStore kv;
  PairingTable t(kv);
  t.load();
  CHECK_EQ(t.count(), 0);
  CHECK_EQ(t.mayPair(false, 0), LVP_ERR_PAIRING_CLOSED);
  CHECK_EQ(t.mayPair(true, 0), LVP_OK); // mit Schluessel jederzeit
  t.openWindow(1000, 5000);
  CHECK_EQ(t.mayPair(false, 3000), LVP_OK);
  CHECK_EQ(t.mayPair(false, 6000), LVP_ERR_PAIRING_CLOSED);

  CHECK_EQ(t.add(CTRL, "Kueche"), LVP_OK);
  CHECK_EQ(t.add(CTRL, "Kueche 2"), LVP_OK); // Aktualisierung
  CHECK_EQ(t.count(), 1);
  uint8_t mac[6] = {1, 2, 3, 4, 5, 0};
  for (int i = 0; i < PairingTable::MAX - 1; i++) {
    mac[5] = (uint8_t)i;
    CHECK_EQ(t.add(mac, "x"), LVP_OK);
  }
  mac[5] = 99;
  CHECK_EQ(t.add(mac, "voll"), LVP_ERR_FULL);

  PairingTable t2(kv);
  t2.load();
  CHECK_EQ(t2.count(), PairingTable::MAX);
  CHECK(t2.contains(CTRL));
  CHECK(strcmp(t2.at(0).name, "Kueche 2") == 0);
  CHECK(t2.remove(CTRL));
  CHECK(!t2.contains(CTRL));
  PairingTable t3(kv);
  t3.load();
  CHECK_EQ(t3.count(), PairingTable::MAX - 1);

  // Speicherfehler beim Hinzufuegen: Tabelle bleibt unveraendert
  kv.failWrites = true;
  CHECK_EQ(t3.add(CTRL, "neu"), LVP_ERR_STORAGE);
  CHECK(!t3.contains(CTRL));
  kv.failWrites = false;

  // beschaedigte Daten werden verworfen
  kv.data[PairingTable::KEY] = {1, 3, 0};
  PairingTable t4(kv);
  t4.load();
  CHECK_EQ(t4.count(), 0);
}

static void test_key_hex() {
  uint8_t k[16];
  CHECK(levi_key_parse_hex("00112233445566778899AABBCCDDEEFF", k));
  CHECK_EQ(k[0], 0x00);
  CHECK_EQ(k[15], 0xFF);
  CHECK(levi_key_parse_hex("00:11:22:33:44:55:66:77:88:99:aa:bb:cc:dd:ee:ff", k));
  CHECK(!levi_key_parse_hex("0011", k));                                 // zu kurz
  CHECK(!levi_key_parse_hex("00000000000000000000000000000000", k));     // nur Nullen
  CHECK(!levi_key_parse_hex("00112233445566778899AABBCCDDEEFG", k));     // fremdes Zeichen
  CHECK(!levi_key_parse_hex("00112233445566778899AABBCCDDEEFF00", k));   // zu lang
  char hex[33];
  levi_key_to_hex(k, hex, sizeof(hex));
  CHECK(strcmp(hex, "00112233445566778899AABBCCDDEEFF") == 0);
}

// ---- Protokollablauf Gateway ----

struct GwFixture {
  MemKvStore kv;
  LightModel light;
  CalibStore cal{kv, CalibStore::Defaults()};
  PairingTable pairing{kv};
  GatewayHandler::Identity id;
  GatewayHandler *h = nullptr;
  uint16_t seq = 0;
  uint32_t session = 0xC0FFEE;

  explicit GwFixture(bool key) {
    cal.load();
    pairing.load();
    id.id[5] = 0x42;
    id.keyConfigured = key;
    id.session = 0x1234;
    h = new GatewayHandler(light, cal, pairing, id);
  }
  ~GwFixture() {
    delete h;
  }
  // Anfrage senden, Antwort dekodieren
  lvp_err_t send(const uint8_t *src, lvp_msg_t &req, lvp_msg_t &rsp, GatewayHandler::Output &out, uint32_t now = 0, bool bcast = false,
                 bool newSeq = true) {
    if (newSeq) {
      seq = lvp_seq_next(seq);
    }
    req.hdr.seq = seq;
    req.hdr.session = session;
    uint8_t b[LVP_MAX_FRAME];
    size_t n = lvp_encode(&req, b, sizeof(b), nullptr);
    h->handle(src, bcast, b, n, now, out);
    if (!out.replyLen) {
      return LVP_ERR_SHORT;
    }
    return lvp_decode(out.reply, out.replyLen, &rsp);
  }
};

static void test_handler_discovery_and_pairing_window() {
  GwFixture f(false);
  lvp_msg_t req, rsp;
  GatewayHandler::Output out;
  lvp_init(&req, LVP_MSG_DISCOVER);
  memcpy(req.u.discover.controller_id, CTRL, 6);
  CHECK_EQ(f.send(CTRL, req, rsp, out, 0, true), LVP_OK);
  CHECK(out.replyBroadcast);
  CHECK(out.ensurePeer);
  CHECK_EQ(rsp.hdr.type, LVP_MSG_ANNOUNCE);
  CHECK_EQ(rsp.u.announce.gateway_id[5], 0x42);
  CHECK_EQ(rsp.u.announce.caps & LVP_GW_KEY_REQUIRED, 0);
  CHECK_EQ(rsp.u.announce.caps & LVP_GW_PAIRING_OPEN, 0);

  // Ohne Schluessel und ohne Fenster: Kopplung abgelehnt
  lvp_init(&req, LVP_MSG_PAIR_REQ);
  memcpy(req.u.pair_req.controller_id, CTRL, 6);
  strcpy(req.u.pair_req.name, "Zentrale");
  CHECK_EQ(f.send(CTRL, req, rsp, out, 100), LVP_OK);
  CHECK_EQ(rsp.hdr.type, LVP_MSG_NACK);
  CHECK_EQ(rsp.u.nack.error, LVP_ERR_PAIRING_CLOSED);

  // Steuerung ohne Kopplung abgelehnt
  lvp_init(&req, LVP_MSG_GET_STATE);
  CHECK_EQ(f.send(CTRL, req, rsp, out, 100), LVP_OK);
  CHECK_EQ(rsp.u.nack.error, LVP_ERR_NOT_PAIRED);

  // Fenster offen (BOOT-Taste): Kopplung angenommen
  f.pairing.openWindow(200, 300000);
  lvp_init(&req, LVP_MSG_PAIR_REQ);
  memcpy(req.u.pair_req.controller_id, CTRL, 6);
  CHECK_EQ(f.send(CTRL, req, rsp, out, 300), LVP_OK);
  CHECK_EQ(rsp.hdr.type, LVP_MSG_PAIR_RSP);
  CHECK_EQ(rsp.u.pair_rsp.result, LVP_OK);
  CHECK(out.pairedChanged);
  CHECK(f.pairing.contains(CTRL));
  CHECK(rsp.hdr.flags & LVP_FLAG_RESPONSE);
  CHECK_EQ(rsp.hdr.ref_seq, f.seq);
}

static void test_handler_key_allows_pairing_anytime() {
  GwFixture f(true);
  lvp_msg_t req, rsp;
  GatewayHandler::Output out;
  lvp_init(&req, LVP_MSG_PAIR_REQ);
  memcpy(req.u.pair_req.controller_id, CTRL, 6);
  CHECK_EQ(f.send(CTRL, req, rsp, out, 999999), LVP_OK);
  CHECK_EQ(rsp.hdr.type, LVP_MSG_PAIR_RSP);
  uint8_t b[LVP_MAX_FRAME];
  size_t n = f.h->buildAnnounce(b, sizeof(b), 0);
  lvp_msg_t a;
  CHECK_EQ(lvp_decode(b, n, &a), LVP_OK);
  CHECK(a.u.announce.caps & LVP_GW_KEY_REQUIRED);
  CHECK(a.u.announce.caps & LVP_GW_PAIRED);
}

static void pairCtrl(GwFixture &f, const uint8_t *mac) {
  f.pairing.openWindow(0, 1000000);
  lvp_msg_t req, rsp;
  GatewayHandler::Output out;
  lvp_init(&req, LVP_MSG_PAIR_REQ);
  memcpy(req.u.pair_req.controller_id, mac, 6);
  f.send(mac, req, rsp, out, 1);
}

static void test_handler_set_state_confirmed_and_dedup() {
  GwFixture f(false);
  pairCtrl(f, CTRL);
  lvp_msg_t req, rsp;
  GatewayHandler::Output out;
  lvp_init(&req, LVP_MSG_SET_STATE);
  req.u.set_state = {LVP_SET_POWER | LVP_SET_COLOR, 1, 0, 255, 64, 64, 0};
  CHECK_EQ(f.send(CTRL, req, rsp, out), LVP_OK);
  CHECK_EQ(rsp.hdr.type, LVP_MSG_STATE); // bestaetigter Zustand vom Gateway
  CHECK_EQ(rsp.u.state.power, 1);
  CHECK_EQ(rsp.u.state.g, 64);
  CHECK(out.stateChanged);
  uint32_t rev = rsp.u.state.revision;

  // Wiederholung derselben Anfrage (Antwort verloren): gleiche Antwort, keine neue Revision
  GatewayHandler::Output out2;
  lvp_msg_t rsp2;
  CHECK_EQ(f.send(CTRL, req, rsp2, out2, 0, false, false), LVP_OK);
  CHECK_EQ(rsp2.u.state.revision, rev);
  CHECK_EQ(f.light.snapshot().revision, rev);
  CHECK(!out2.stateChanged);
  CHECK(memcmp(out.reply, out2.reply, out.replyLen) == 0);

  // Ungueltiger Wert: NACK, Zustand unveraendert
  lvp_init(&req, LVP_MSG_SET_STATE);
  req.u.set_state = {LVP_SET_BRIGHTNESS, 0, 50, 0, 0, 0, 0};
  uint8_t b[LVP_MAX_FRAME];
  f.seq = lvp_seq_next(f.seq);
  req.hdr.seq = f.seq;
  req.hdr.session = f.session;
  size_t n = lvp_encode(&req, b, sizeof(b), nullptr);
  b[LVP_HEADER_LEN + 2] = 0; // Helligkeit 0 einschleusen, CRC neu
  b[14] = b[15] = 0;
  uint16_t crc = lvp_crc16(b, n, 0xFFFF);
  b[14] = (uint8_t)crc;
  b[15] = (uint8_t)(crc >> 8);
  f.h->handle(CTRL, false, b, n, 0, out);
  CHECK_EQ(lvp_decode(out.reply, out.replyLen, &rsp), LVP_OK);
  CHECK_EQ(rsp.hdr.type, LVP_MSG_NACK);
  CHECK_EQ(rsp.u.nack.error, LVP_ERR_OUT_OF_RANGE);
  CHECK_EQ(f.light.snapshot().revision, rev);

  // Steuerbefehle per Broadcast werden ignoriert
  lvp_init(&req, LVP_MSG_SET_STATE);
  req.u.set_state = {LVP_SET_POWER, 0, 0, 0, 0, 0, 0};
  f.send(CTRL, req, rsp, out, 0, true);
  CHECK_EQ(out.replyLen, 0);
  CHECK_EQ(f.light.snapshot().power, 1);
}

static void test_handler_calibration_roundtrip() {
  GwFixture f(false);
  pairCtrl(f, CTRL);
  lvp_msg_t req, rsp;
  GatewayHandler::Output out;
  lvp_init(&req, LVP_MSG_GET_CALIB);
  CHECK_EQ(f.send(CTRL, req, rsp, out), LVP_OK);
  CHECK_EQ(rsp.hdr.type, LVP_MSG_CALIB);
  CHECK_EQ(rsp.u.calib.custom, 0);
  uint16_t rev = rsp.u.calib.calib_revision;

  lvp_init(&req, LVP_MSG_SET_CALIB);
  for (int i = 0; i < 30; i++) {
    req.u.set_calib.points[i] = (uint8_t)(200 - i);
  }
  CHECK_EQ(f.send(CTRL, req, rsp, out), LVP_OK);
  CHECK_EQ(rsp.u.calib.custom, 1);
  CHECK(rsp.u.calib.calib_revision != rev);
  CHECK_EQ(rsp.u.calib.points[29], 171);
  CHECK(out.calibChanged);
  CHECK(f.kv.data.count(CalibStore::KEY_POINTS) == 1); // im NVS

  // Zustand meldet neue Kalibrierrevision
  lvp_init(&req, LVP_MSG_GET_STATE);
  CHECK_EQ(f.send(CTRL, req, rsp, out), LVP_OK);
  CHECK_EQ(rsp.u.state.calib_revision, f.cal.revision());

  // Speicherfehler -> NACK
  f.kv.failWrites = true;
  lvp_init(&req, LVP_MSG_RESET_CALIB);
  CHECK_EQ(f.send(CTRL, req, rsp, out), LVP_OK);
  CHECK_EQ(rsp.hdr.type, LVP_MSG_NACK);
  CHECK_EQ(rsp.u.nack.error, LVP_ERR_STORAGE);
  f.kv.failWrites = false;
  CHECK_EQ(f.send(CTRL, req, rsp, out), LVP_OK);
  CHECK_EQ(rsp.u.calib.custom, 0);
}

static void test_handler_unpair_and_reconnect() {
  GwFixture f(false);
  pairCtrl(f, CTRL);
  pairCtrl(f, CTRL2);
  CHECK_EQ(f.pairing.count(), 2);
  lvp_msg_t req, rsp;
  GatewayHandler::Output out;
  lvp_init(&req, LVP_MSG_UNPAIR);
  memcpy(req.u.unpair.controller_id, CTRL, 6);
  CHECK_EQ(f.send(CTRL, req, rsp, out), LVP_OK);
  CHECK_EQ(rsp.hdr.type, LVP_MSG_ACK);
  CHECK(out.removePeer);
  CHECK(!f.pairing.contains(CTRL));
  CHECK(f.pairing.contains(CTRL2));

  // Neustart des Controllers (neue Sitzung, Sequenz von vorn): wird als neu erkannt
  f.session = 0xBEEF;
  f.seq = 0;
  lvp_init(&req, LVP_MSG_GET_STATE);
  CHECK_EQ(f.send(CTRL2, req, rsp, out), LVP_OK);
  CHECK_EQ(rsp.hdr.type, LVP_MSG_STATE);

  // Kopplung bleibt ueber einen Neustart des Gateways erhalten (NVS)
  PairingTable reloaded(f.kv);
  reloaded.load();
  CHECK(reloaded.contains(CTRL2));
  CHECK(!reloaded.contains(CTRL));
}

static void test_handler_rejects_garbage_and_responses() {
  GwFixture f(false);
  pairCtrl(f, CTRL);
  GatewayHandler::Output out;
  uint8_t junk[20] = {'L', 'V', 1, 0x21};
  f.h->handle(CTRL, false, junk, sizeof(junk), 0, out);
  CHECK_EQ(out.replyLen, 0); // Laenge/CRC falsch: still verworfen
  // Antworten anderer Geraete werden nicht als Anfrage behandelt
  lvp_msg_t m;
  lvp_init(&m, LVP_MSG_GET_STATE);
  m.hdr.flags = LVP_FLAG_RESPONSE;
  m.hdr.seq = 77;
  uint8_t b[LVP_MAX_FRAME];
  size_t n = lvp_encode(&m, b, sizeof(b), nullptr);
  f.h->handle(CTRL, false, b, n, 0, out);
  CHECK_EQ(out.replyLen, 0);
  // Unbekannter Typ per Unicast -> NACK
  b[3] = 0x66;
  b[14] = b[15] = 0;
  b[4] = 0;
  uint16_t crc = lvp_crc16(b, n, 0xFFFF);
  b[14] = (uint8_t)crc;
  b[15] = (uint8_t)(crc >> 8);
  f.h->handle(CTRL, false, b, n, 0, out);
  lvp_msg_t r;
  CHECK_EQ(lvp_decode(out.reply, out.replyLen, &r), LVP_OK);
  CHECK_EQ(r.u.nack.error, LVP_ERR_UNSUPPORTED);
}

static void test_state_notify_frame() {
  GwFixture f(false);
  f.light.setBle(true);
  uint8_t b[LVP_MAX_FRAME];
  size_t n = f.h->buildStateNotify(b, sizeof(b));
  lvp_msg_t m;
  CHECK_EQ(lvp_decode(b, n, &m), LVP_OK);
  CHECK_EQ(m.hdr.type, LVP_MSG_STATE);
  CHECK(m.hdr.flags & LVP_FLAG_NOTIFY);
  CHECK(m.u.state.flags & LVP_STATE_BLE);
}

void tests_gateway(void) {
  RUN(test_light_model_semantics);
  RUN(test_scheduler_order_after_connect);
  RUN(test_scheduler_packets_and_brightness_scale);
  RUN(test_scheduler_color_calibrated_and_raw_throttle);
  RUN(test_scheduler_music_eq_and_power_off);
  RUN(test_scheduler_fade);
  RUN(test_calib_store_persistence);
  RUN(test_calib_defaults_points);
  RUN(test_calib_matches_legacy_core);
  RUN(test_pairing_table);
  RUN_D(test_band_memory_choose_and_persist, "Gemerktes Band: Auswahl (staerkstes, gemerktes, fehlendes), Speichern, Laden, Vergessen");
  RUN_D(test_band_memory_storage_errors, "Gemerktes Band: Speicherfehler behalten alten Stand, beschaedigte Daten werden verworfen");
  RUN(test_key_hex);
  RUN(test_handler_discovery_and_pairing_window);
  RUN(test_handler_key_allows_pairing_anytime);
  RUN(test_handler_set_state_confirmed_and_dedup);
  RUN(test_handler_calibration_roundtrip);
  RUN(test_handler_unpair_and_reconnect);
  RUN(test_handler_rejects_garbage_and_responses);
  RUN(test_state_notify_frame);
}
