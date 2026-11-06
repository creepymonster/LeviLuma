// Host-Tests: ESP-NOW-Diagnosegeraet (debug, Komponente debug_core).
// ALLE Rahmen in dieser Datei sind SYNTHETISCH: von Hand bzw. mit dem gemeinsamen Protokoll-Kodierer (lvp_encode) erzeugt,
// nach dem Rahmenaufbau der ESP-IDF-Dokumentation. Es sind keine Mitschnitte von Hardware; ob echte Rahmen genau so aussehen,
// ist nur am Geraet pruefbar.
#include <stdlib.h>
#include <string>
#include <vector>
#include "dbg_cmd.h"
#include "dbg_format.h"
#include "dbg_frame.h"
#include "dbg_levi.h"
#include "dbg_state.h"
#include "leviluma_proto.h"
#include "test_util.h"

using namespace dbg;

static const uint8_t GW[6] = {0x24, 0x0A, 0xC4, 0x00, 0x00, 0x01};   // synthetische Adressen
static const uint8_t CTRL[6] = {0x24, 0x0A, 0xC4, 0x00, 0x00, 0x02};
static const uint8_t BC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ---- Hilfen zum Bauen synthetischer Rahmen ----

struct Opt {
  bool prot = false, retry = false, fcs = true;
  uint8_t fc0 = 0xD0;
  uint8_t category = 127;
  uint8_t oui[3] = {0x18, 0xFE, 0x34};
  uint8_t elem_id = 221, elem_type = 4, version = 1;
  int elem_len_delta = 0; // Abweichung der Elementlaenge vom wahren Wert (Beschaedigung)
  bool no_element = false;
  const uint8_t *addr3 = BC;
};

static size_t build80211(uint8_t *out, const uint8_t dst[6], const uint8_t src[6], const uint8_t *body, size_t blen, const Opt &o = Opt()) {
  size_t p = 0;
  out[p++] = o.fc0;
  out[p++] = (uint8_t)((o.retry ? 0x08 : 0) | (o.prot ? 0x40 : 0));
  out[p++] = 0;
  out[p++] = 0; // Dauer
  memcpy(out + p, dst, 6), p += 6;
  memcpy(out + p, src, 6), p += 6;
  memcpy(out + p, o.addr3, 6), p += 6;
  out[p++] = 0x30; // Sequenzfeld: Rahmenzaehler 3, Fragment 0
  out[p++] = 0x00;
  out[p++] = o.category;
  memcpy(out + p, o.oui, 3), p += 3;
  const uint8_t rnd[4] = {0xDE, 0xAD, 0xBE, 0xEF};
  memcpy(out + p, rnd, 4), p += 4;

  if (!o.no_element) {
    out[p++] = o.elem_id;
    out[p++] = (uint8_t)(5 + blen + o.elem_len_delta);
    memcpy(out + p, o.oui, 3), p += 3;
    out[p++] = o.elem_type;
    out[p++] = o.version;
    memcpy(out + p, body, blen), p += blen;
  }
  if (o.fcs) {
    const uint8_t f[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    memcpy(out + p, f, 4), p += 4;
  }
  return p;
}

static void fill_cap(Capture &c, const uint8_t *frame, size_t n, bool fcs = true) {
  memset(&c, 0, sizeof(c));
  c.source = Source::Promiscuous;
  c.t_ms = 12345;
  c.has_rx_us = true;
  c.rx_us = 777000;
  c.has_rssi = true;
  c.rssi = -52;
  c.channel = 1;
  c.fcs_included = fcs;
  c.orig_len = (uint16_t)n;
  c.len = (uint16_t)(n < MAX_CAPTURE ? n : MAX_CAPTURE);
  memcpy(c.data, frame, c.len);
}

static size_t levi_frame(uint8_t *buf, lvp_msg_type_t t, uint8_t flags, uint16_t seq, uint16_t ref, void (*fill)(lvp_msg_t &) = nullptr) {
  lvp_msg_t m;
  lvp_init(&m, t);
  m.hdr.flags = flags;
  m.hdr.seq = seq;
  m.hdr.ref_seq = ref;
  m.hdr.session = 0xCAFE0001;
  if (fill) {
    fill(m);
  }
  lvp_err_t e;
  size_t n = lvp_encode(&m, buf, LVP_MAX_FRAME, &e);
  CHECK_EQ(e, LVP_OK);
  return n;
}

static void fill_announce(lvp_msg_t &m) {
  memcpy(m.u.announce.gateway_id, GW, 6);
  m.u.announce.fw_version = 0x0102;
  m.u.announce.caps = LVP_GW_KEY_REQUIRED | LVP_GW_BLE_CONNECTED;
  m.u.announce.state_revision = 42;
  snprintf(m.u.announce.name, sizeof(m.u.announce.name), "Wohnzimmer");
}

static void fix_crc(uint8_t *lv, size_t n) {
  lv[14] = lv[15] = 0;
  uint16_t c = lvp_crc16(lv, n, 0xFFFF);
  lv[14] = (uint8_t)c;
  lv[15] = (uint8_t)(c >> 8);
}

static std::string render(const Capture &c, const FormatOptions &opt = FormatOptions()) {
  static Capture cc;
  cc = c;
  FrameInfo fi;
  parse_capture(cc, fi);
  LeviInfo li;
  if (fi.body && (fi.verdict == Verdict::Ok || fi.verdict == Verdict::Truncated)) {
    decode_levi(fi.body, fi.body_len, fi.body_complete, li);
  }
  static char buf[16384];
  Out out(buf, sizeof(buf));
  format_record(out, 1, cc, fi, li, opt);
  return buf;
}

static bool has(const std::string &s, const char *needle) {
  return s.find(needle) != std::string::npos;
}

static bool has_field(const LeviInfo &li, const char *name, const char *value_part) {
  for (int i = 0; i < li.fields.n; i++) {
    if (!strcmp(li.fields.f[i].name, name) && strstr(li.fields.f[i].value, value_part)) {
      return true;
    }
  }
  return false;
}

// ---- Rahmenauswertung ----

static void test_dbg_frame_valid_broadcast() {
  uint8_t lv[LVP_MAX_FRAME], fr[400];
  size_t ln = levi_frame(lv, LVP_MSG_ANNOUNCE, LVP_FLAG_NOTIFY, 5, 0, fill_announce);
  size_t n = build80211(fr, BC, GW, lv, ln);
  Capture c;
  fill_cap(c, fr, n);
  FrameInfo fi;
  parse_capture(c, fi);
  CHECK(fi.verdict == Verdict::Ok);
  CHECK(fi.has_mac_header && fi.has_addrs && fi.broadcast && !fi.multicast);
  CHECK(!memcmp(fi.src, GW, 6) && !memcmp(fi.dst, BC, 6));
  CHECK_EQ(fi.fc_type, 0);
  CHECK_EQ(fi.fc_subtype, 13);
  CHECK(!fi.protected_bit && !fi.retry);
  CHECK_EQ(fi.seq_ctrl >> 4, 3);
  CHECK_EQ(fi.frame_len_driver, n);
  CHECK_EQ(fi.frame_len_no_fcs, n - 4);
  CHECK_EQ(fi.captured_len, n);
  CHECK_EQ(fi.mac_hdr_len, 24);
  CHECK(fi.has_category && fi.has_element);
  CHECK(fi.random[0] == 0xDE && fi.random[3] == 0xEF);
  CHECK_EQ(fi.element_len, 5 + ln);
  CHECK_EQ(fi.element_version_raw, 1);
  CHECK_EQ(fi.body_len, ln);
  CHECK_EQ(fi.body_declared, ln);
  CHECK(fi.body_complete && fi.fcs_present && fi.fcs[0] == 0xAA);

  LeviInfo li;
  decode_levi(fi.body, fi.body_len, fi.body_complete, li);
  CHECK(li.kind == LeviInfo::Kind::Decoded);
  CHECK(li.hdr_verified && li.crc_checked && li.crc_ok);
  CHECK_EQ(li.type, LVP_MSG_ANNOUNCE);
  CHECK_EQ(li.flags, LVP_FLAG_NOTIFY);
  CHECK_EQ(li.seq, 5);
  CHECK_EQ(li.ref_seq, 0);
  CHECK_EQ(li.session, 0xCAFE0001u);
  CHECK(has_field(li, "gateway_id", "24:0A:C4:00:00:01"));
  CHECK(has_field(li, "fw_version", "1.2"));
  CHECK(has_field(li, "caps", "KEY_REQUIRED|BLE_CONNECTED"));
  CHECK(has_field(li, "state_revision", "42"));
  CHECK(has_field(li, "name", "Wohnzimmer"));
}

static void test_dbg_frame_unicast_multicast_retry() {
  uint8_t lv[LVP_MAX_FRAME], fr[400];
  size_t ln = levi_frame(lv, LVP_MSG_GET_STATE, 0, 9, 0);
  Opt o;
  o.retry = true;
  Capture c;
  FrameInfo fi;
  size_t n = build80211(fr, GW, CTRL, lv, ln, o);
  fill_cap(c, fr, n);
  parse_capture(c, fi);
  CHECK(fi.verdict == Verdict::Ok && !fi.broadcast && !fi.multicast && fi.retry);
  CHECK(has(render(c), "(Unicast)"));
  const uint8_t mc[6] = {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01};
  n = build80211(fr, mc, CTRL, lv, ln);
  fill_cap(c, fr, n);
  parse_capture(c, fi);
  CHECK(fi.multicast && !fi.broadcast);
  CHECK(has(render(c), "(Multicast)"));
}

static void test_dbg_frame_encrypted_not_decoded() {
  // Protected-Bit: Der Rest ist Chiffretext. Selbst wenn die Bytes zufaellig wie ein LeviLuma-Rahmen aussaehen, wird nichts dekodiert.
  uint8_t lv[LVP_MAX_FRAME], fr[400];
  size_t ln = levi_frame(lv, LVP_MSG_STATE, LVP_FLAG_RESPONSE, 7, 6, nullptr);
  (void)ln;
  Opt o;
  o.prot = true;
  size_t n = build80211(fr, GW, CTRL, lv, 15, o); // Inhalt beliebig
  Capture c;
  fill_cap(c, fr, n);
  FrameInfo fi;
  parse_capture(c, fi);
  CHECK(fi.verdict == Verdict::Encrypted);
  CHECK(fi.protected_bit && fi.has_addrs);
  CHECK(!fi.has_element);
  CHECK(!fi.has_category);
  CHECK_EQ(fi.body_len, n - 4 - 24);
  std::string s = render(c);
  CHECK(has(s, "NICHT DEKODIERT"));
  CHECK(has(s, "VERSCHLUESSELT"));
  CHECK(!has(s, "Felder"));
  CHECK(!has(s, "GUELTIG"));
  CHECK(has(s, "24:0A:C4:00:00:02")); // Metadaten bleiben sichtbar

  // Protected, aber Adressfeld 3 nicht Broadcast: nicht als ESP-NOW erkennbar
  Opt o2 = o;
  o2.addr3 = GW;
  n = build80211(fr, GW, CTRL, lv, 15, o2);
  fill_cap(c, fr, n);
  parse_capture(c, fi);
  CHECK(fi.verdict == Verdict::NotEspNow);
  CHECK(!is_espnow_candidate(c.data, c.len));
}

static void test_dbg_frame_candidate_prefilter() {
  uint8_t lv[LVP_MAX_FRAME], fr[400];
  size_t ln = levi_frame(lv, LVP_MSG_ACK, LVP_FLAG_RESPONSE, 1, 1);
  size_t n = build80211(fr, GW, CTRL, lv, ln);
  CHECK(is_espnow_candidate(fr, n));
  CHECK(!is_espnow_candidate(fr, 10));
  CHECK(!is_espnow_candidate(fr, 27));
  CHECK(!is_espnow_candidate(nullptr, 100));
  Opt b;
  b.fc0 = 0x80; // Beacon
  n = build80211(fr, BC, GW, lv, ln, b);
  CHECK(!is_espnow_candidate(fr, n));
  Opt a;
  a.fc0 = 0xE0; // Action No Ack wird ebenfalls zugelassen
  n = build80211(fr, GW, CTRL, lv, ln, a);
  CHECK(is_espnow_candidate(fr, n));
  Opt k;
  k.category = 4;
  n = build80211(fr, GW, CTRL, lv, ln, k);
  CHECK(!is_espnow_candidate(fr, n));
}

static void test_dbg_frame_not_espnow() {
  uint8_t lv[LVP_MAX_FRAME], fr[400];
  size_t ln = levi_frame(lv, LVP_MSG_ACK, 0, 1, 0);
  Capture c;
  FrameInfo fi;
  Opt o;
  o.fc0 = 0x80; // Beacon
  size_t n = build80211(fr, BC, GW, lv, ln, o);
  fill_cap(c, fr, n);
  parse_capture(c, fi);
  CHECK(fi.verdict == Verdict::NotEspNow);
  CHECK(!fi.has_element);
  Opt k;
  k.category = 4;
  n = build80211(fr, GW, CTRL, lv, ln, k);
  fill_cap(c, fr, n);
  parse_capture(c, fi);
  CHECK(fi.verdict == Verdict::NotEspNow);
  Opt u;
  u.oui[0] = 0x00; // fremde OUI
  n = build80211(fr, GW, CTRL, lv, ln, u);
  fill_cap(c, fr, n);
  parse_capture(c, fi);
  CHECK(fi.verdict == Verdict::NotEspNow);
  // nicht auswertbar: der ganze Rahmen wird als Hexdump gezeigt
  CHECK(has(render(c), "Rohrahmen"));
}

static void test_dbg_frame_too_short_and_malformed() {
  uint8_t lv[LVP_MAX_FRAME], fr[400];
  size_t ln = levi_frame(lv, LVP_MSG_ACK, 0, 1, 0);
  Capture c;
  FrameInfo fi;
  size_t n = build80211(fr, GW, CTRL, lv, ln);

  for (size_t cut : {(size_t)0, (size_t)1, (size_t)10, (size_t)23, (size_t)24, (size_t)25, (size_t)27, (size_t)31}) {
    fill_cap(c, fr, cut + 4, true); // FCS gedacht; Rahmen = cut Byte
    parse_capture(c, fi);
    CHECK(fi.verdict == Verdict::TooShort);
    CHECK(fi.body == nullptr || fi.body_len == 0);
    (void)render(c); // darf nicht abstuerzen
  }
  // Element laenger als der Rahmen (nicht durch Erfassung abgeschnitten): beschaedigt
  Opt o;
  o.elem_len_delta = 20;
  n = build80211(fr, GW, CTRL, lv, ln, o);
  fill_cap(c, fr, n);
  parse_capture(c, fi);
  CHECK(fi.verdict == Verdict::Malformed);
  CHECK(has(fi.reason, "ueberschreitet"));
  // Element kuerzer als sein fester Kopf
  Opt s;
  s.elem_len_delta = -(int)ln - 2; // Laenge 3
  n = build80211(fr, GW, CTRL, lv, ln, s);
  fill_cap(c, fr, n);
  parse_capture(c, fi);
  CHECK(fi.verdict == Verdict::Malformed);
  // kein Element
  Opt ne;
  ne.no_element = true;
  n = build80211(fr, GW, CTRL, lv, 0, ne);
  fill_cap(c, fr, n);
  parse_capture(c, fi);
  CHECK(fi.verdict == Verdict::TooShort || fi.verdict == Verdict::Malformed);
  // Rahmen laenger als die Laengenangabe vom Treiber: orig_len kleiner als Daten
  n = build80211(fr, GW, CTRL, lv, ln);
  fill_cap(c, fr, n);
  c.orig_len = 20; // Treiber meldet weniger als kopiert wurde: darf nicht ueber orig_len hinaus lesen
  c.len = 20;
  parse_capture(c, fi);
  CHECK(fi.verdict == Verdict::TooShort);
}

static void test_dbg_frame_capture_truncated() {
  uint8_t lv[LVP_MAX_FRAME], fr[400];
  size_t ln = levi_frame(lv, LVP_MSG_CALIB, LVP_FLAG_RESPONSE, 2, 1, [](lvp_msg_t &m) { m.u.calib.custom = 1; });
  size_t n = build80211(fr, CTRL, GW, lv, ln);
  Capture c;
  FrameInfo fi;
  fill_cap(c, fr, n);
  c.len = 60; // Erfassung endet mitten im Element
  parse_capture(c, fi);
  CHECK(fi.verdict == Verdict::Truncated);
  CHECK(fi.has_element && !fi.body_complete);
  CHECK(fi.body_declared_known && fi.body_declared == ln);
  CHECK(fi.body_len < ln);
  CHECK(!fi.fcs_present);
  LeviInfo li;
  decode_levi(fi.body, fi.body_len, fi.body_complete, li);
  CHECK(li.kind == LeviInfo::Kind::Invalid);
  CHECK(!li.crc_checked); // vollstaendige Nutzdaten fehlen: keine CRC-Aussage
  CHECK(li.hdr_read && !li.hdr_verified);
  std::string s = render(c);
  CHECK(has(s, "ABGESCHNITTEN"));
  CHECK(has(s, "nicht geprueft"));
  CHECK(has(s, "UNGEPRUEFT"));

  // Zwei Elemente, Erfassung endet im zweiten: das erste ist vollstaendig und wird ausgewertet
  uint8_t big[400];
  size_t p = build80211(big, CTRL, GW, lv, ln, Opt());
  p -= 4; // FCS entfernen
  big[p++] = 221;
  big[p++] = 200;
  for (int i = 0; i < 100; i++) {
    big[p++] = (uint8_t)i;
  }
  fill_cap(c, big, p + 4);
  c.len = (uint16_t)p; // Rest des zweiten Elements fehlt
  c.orig_len = (uint16_t)(p + 4 + 100);
  parse_capture(c, fi);
  CHECK(fi.verdict == Verdict::Truncated);
  CHECK(fi.has_element && fi.body_complete);
  CHECK(has(fi.reason, "erstes ESP-NOW-Element vollstaendig"));
}

static void test_dbg_frame_multi_element_and_version() {
  uint8_t lv[LVP_MAX_FRAME], fr[400];
  size_t ln = levi_frame(lv, LVP_MSG_ACK, 0, 1, 0);
  size_t n = build80211(fr, GW, CTRL, lv, ln);
  n -= 4;
  // zweites ESP-NOW-Element anhaengen
  fr[n++] = 221;
  fr[n++] = 7;
  fr[n++] = 0x18;
  fr[n++] = 0xFE;
  fr[n++] = 0x34;
  fr[n++] = 4;
  fr[n++] = 0x11;
  fr[n++] = 0xAB;
  fr[n++] = 0xCD;
  n += 4; // FCS
  Capture c;
  FrameInfo fi;
  fill_cap(c, fr, n);
  parse_capture(c, fi);
  CHECK(fi.verdict == Verdict::Ok);
  CHECK_EQ(fi.espnow_elements, 2);
  CHECK_EQ(fi.body_len, ln); // nur das erste wird ausgewertet
  CHECK(has(render(c), "2 ESP-NOW-Elemente"));
  // Versionsfeld wird roh gezeigt, nicht gedeutet
  Opt o;
  o.version = 0x12;
  n = build80211(fr, GW, CTRL, lv, ln, o);
  fill_cap(c, fr, n);
  parse_capture(c, fi);
  CHECK_EQ(fi.element_version_raw, 0x12);
  CHECK(has(render(c), "Versionsfeld 0x12 (untere 4 Bit: 2)"));
}

static void test_dbg_frame_payload_only_path() {
  uint8_t lv[LVP_MAX_FRAME];
  size_t ln = levi_frame(lv, LVP_MSG_STATE, LVP_FLAG_NOTIFY, 3, 0, [](lvp_msg_t &m) {
    m.u.state.revision = 9;
    m.u.state.power = 1;
    m.u.state.brightness = 80;
    m.u.state.r = 255;
  });
  Capture c;
  memset(&c, 0, sizeof(c));
  c.source = Source::EspNowCallback;
  c.t_ms = 1000;
  c.has_rssi = true;
  c.rssi = -60;
  c.channel = 6;
  c.has_addr = true;
  memcpy(c.src, GW, 6);
  memcpy(c.dst, BC, 6);
  c.orig_len = c.len = (uint16_t)ln;
  memcpy(c.data, lv, ln);
  FrameInfo fi;
  parse_capture(c, fi);
  CHECK(fi.verdict == Verdict::Ok && !fi.has_mac_header && fi.has_addrs);
  CHECK_EQ(fi.body_len, ln);
  std::string s = render(c);
  CHECK(has(s, "nicht verfuegbar"));
  CHECK(has(s, "Versions-/Verschluesselungsangaben nicht verfuegbar"));
  CHECK(has(s, "Kanal 6"));
  CHECK(has(s, "STATE (0x22)"));
  // ohne Adressen und ohne RSSI
  c.has_addr = false;
  c.has_rssi = false;
  c.channel = 0;
  s = render(c);
  CHECK(has(s, "Absender   : nicht verfuegbar"));
  CHECK(has(s, "RSSI nicht verfuegbar"));
  CHECK(has(s, "Kanal nicht verfuegbar"));
}

// ---- LeviLuma-Dekodierung ----

static void test_dbg_levi_errors() {
  uint8_t lv[LVP_MAX_FRAME];
  size_t ln = levi_frame(lv, LVP_MSG_ANNOUNCE, 0, 1, 0, fill_announce);
  LeviInfo li;

  // beschaedigte CRC (Nutzdatenbit gekippt)
  uint8_t bad[LVP_MAX_FRAME];
  memcpy(bad, lv, ln);
  bad[20] ^= 0x01;
  decode_levi(bad, ln, true, li);
  CHECK(li.kind == LeviInfo::Kind::Invalid);
  CHECK_EQ(li.err, LVP_ERR_CRC);
  CHECK(li.crc_checked && !li.crc_ok && li.crc_calc != li.crc_rx);
  CHECK(!li.hdr_verified);

  // unbekannte Version, gueltige CRC
  memcpy(bad, lv, ln);
  bad[2] = 2;
  fix_crc(bad, ln);
  decode_levi(bad, ln, true, li);
  CHECK(li.kind == LeviInfo::Kind::Invalid);
  CHECK_EQ(li.err, LVP_ERR_VERSION);
  CHECK(li.hdr_verified && li.crc_ok);
  CHECK_EQ(li.version, 2);
  CHECK_EQ(li.fields.n, 0); // keine Felder bei unbekannter Version

  // unbekannter Typ, gueltige CRC
  memcpy(bad, lv, ln);
  bad[3] = 0x55;
  fix_crc(bad, ln);
  decode_levi(bad, ln, true, li);
  CHECK(li.kind == LeviInfo::Kind::Unsupported);
  CHECK_EQ(li.err, LVP_ERR_UNSUPPORTED);
  CHECK_EQ(li.type, 0x55);
  CHECK_EQ(li.fields.n, 0);
  char lab[40];
  type_label(0x55, lab, sizeof(lab));
  CHECK(!strcmp(lab, "unbekannt (0x55)"));

  // Nutzdaten passen nicht zum Typ (SET_STATE mit 3 statt 7 Bytes, CRC korrekt)
  uint8_t sh[LVP_HEADER_LEN + 3] = {};
  memcpy(sh, lv, LVP_HEADER_LEN);
  sh[3] = LVP_MSG_SET_STATE;
  sh[5] = 3;
  fix_crc(sh, sizeof(sh));
  decode_levi(sh, sizeof(sh), true, li);
  CHECK(li.kind == LeviInfo::Kind::Invalid);
  CHECK_EQ(li.err, LVP_ERR_BAD_REQUEST);
  CHECK(li.hdr_verified && li.fields.n == 0);

  // len-Feld zu gross: abgeschnittene Nutzdaten
  decode_levi(lv, ln - 5, true, li);
  CHECK(li.kind == LeviInfo::Kind::Invalid);
  CHECK_EQ(li.err, LVP_ERR_LENGTH);
  CHECK(!li.crc_checked);
  CHECK(has(li.reason, "kuerzer"));
  // Restbytes
  uint8_t more[LVP_MAX_FRAME + 4];
  memcpy(more, lv, ln);
  more[ln] = 0;
  decode_levi(more, ln + 1, true, li);
  CHECK_EQ(li.err, LVP_ERR_LENGTH);
  CHECK(has(li.reason, "mehr Nutzdaten"));
  // nur Kennung, kein voller Kopf
  decode_levi(lv, 10, true, li);
  CHECK(li.kind == LeviInfo::Kind::Invalid && !li.hdr_read);
  CHECK_EQ(li.err, LVP_ERR_SHORT);
  // fremde Nutzdaten
  const uint8_t foreign[] = {'h', 'e', 'l', 'l', 'o'};
  decode_levi(foreign, sizeof(foreign), true, li);
  CHECK(li.kind == LeviInfo::Kind::NotLevi);
  decode_levi(foreign, 1, true, li);
  CHECK(li.kind == LeviInfo::Kind::NotLevi);
  decode_levi(nullptr, 0, true, li);
  CHECK(li.kind == LeviInfo::Kind::NotLevi);
}

static void test_dbg_levi_flags() {
  char s[64];
  flags_label(0, s, sizeof(s));
  CHECK(!strcmp(s, "0x00 [-]"));
  flags_label(LVP_FLAG_RESPONSE, s, sizeof(s));
  CHECK(!strcmp(s, "0x01 [RESPONSE]"));
  flags_label(LVP_FLAG_RESPONSE | LVP_FLAG_NOTIFY, s, sizeof(s));
  CHECK(!strcmp(s, "0x03 [RESPONSE|NOTIFY]"));
  flags_label(0x82, s, sizeof(s));
  CHECK(has(s, "NOTIFY") && has(s, "unbekannt 0x80"));
  flags_label(0xFF, s, 8); // kleiner Puffer: darf nicht ueberlaufen (ASan)
  CHECK(strlen(s) < 8);
}

// Ein gueltiges Beispiel je Nachrichtentyp. Fehlt hier ein Typ, den das gemeinsame Protokoll kennt, schlaegt der Test fehl.
static bool sample_for(uint8_t t, uint8_t *buf, size_t *n) {
  auto build = [&](void (*fill)(lvp_msg_t &)) {
    *n = levi_frame(buf, (lvp_msg_type_t)t, 0, 1, 0, fill);
    return true;
  };
  switch (t) {
  case LVP_MSG_DISCOVER:
    return build([](lvp_msg_t &m) {
      memcpy(m.u.discover.controller_id, CTRL, 6);
      snprintf(m.u.discover.name, sizeof(m.u.discover.name), "Remote");
    });
  case LVP_MSG_ANNOUNCE:
    return build(fill_announce);
  case LVP_MSG_PAIR_REQ:
    return build([](lvp_msg_t &m) {
      memcpy(m.u.pair_req.controller_id, CTRL, 6);
      snprintf(m.u.pair_req.name, sizeof(m.u.pair_req.name), "Remote");
    });
  case LVP_MSG_PAIR_RSP:
    return build([](lvp_msg_t &m) {
      m.u.pair_rsp.result = LVP_ERR_PAIRING_CLOSED;
      memcpy(m.u.pair_rsp.gateway_id, GW, 6);
      snprintf(m.u.pair_rsp.name, sizeof(m.u.pair_rsp.name), "GW");
    });
  case LVP_MSG_UNPAIR:
    return build([](lvp_msg_t &m) { memcpy(m.u.unpair.controller_id, CTRL, 6); });
  case LVP_MSG_SET_STATE:
    return build([](lvp_msg_t &m) {
      m.u.set_state.mask = LVP_SET_POWER | LVP_SET_COLOR;
      m.u.set_state.power = 1;
      m.u.set_state.r = 0xFF;
      m.u.set_state.g = 0x80;
      m.u.set_state.brightness = 50;
    });
  case LVP_MSG_STATE:
    return build([](lvp_msg_t &m) {
      m.u.state.revision = 10;
      m.u.state.sent_revision = 9;
      m.u.state.power = -1;
      m.u.state.brightness = 70;
      m.u.state.flags = LVP_STATE_BLE | LVP_STATE_MUSIC;
      m.u.state.calib_revision = 3;
    });
  case LVP_MSG_CALIB:
    return build([](lvp_msg_t &m) {
      m.u.calib.custom = 1;
      m.u.calib.calib_revision = 4;
      for (int i = 0; i < LVP_CAL_BYTES; i++) {
        m.u.calib.points[i] = (uint8_t)(i * 8);
      }
    });
  case LVP_MSG_SET_CALIB:
    return build([](lvp_msg_t &m) {
      for (int i = 0; i < LVP_CAL_BYTES; i++) {
        m.u.set_calib.points[i] = (uint8_t)(255 - i);
      }
    });
  case LVP_MSG_NACK:
    return build([](lvp_msg_t &m) { m.u.nack.error = LVP_ERR_UNSUPPORTED; });
  case LVP_MSG_GET_STATE:
  case LVP_MSG_GET_CALIB:
  case LVP_MSG_RESET_CALIB:
  case LVP_MSG_ACK:
    return build(nullptr);
  default:
    return false;
  }
}

static void test_dbg_levi_all_protocol_types() {
  // Neue Nachrichtentypen im gemeinsamen Protokoll muessen hier und in dbg_levi.cpp (levi_type_supported, fill_fields) nachgezogen werden.
  int known = 0;
  for (int t = 0; t < 256; t++) {
    if (lvp_type_name((uint8_t)t)[0] == '?') {
      CHECK(!levi_type_supported((uint8_t)t)); // nichts Unbekanntes als unterstuetzt melden
      continue;
    }
    known++;
    uint8_t buf[LVP_MAX_FRAME];
    size_t n = 0;
    bool have = sample_for((uint8_t)t, buf, &n);
    if (!have) {
      printf("  Typ 0x%02X (%s) ist im Protokoll, aber ohne Testrahmen in test_debug.cpp (und ggf. ohne Dekodierung im Diagnosetool)\n", t, lvp_type_name((uint8_t)t));
    }
    CHECK(have);
    CHECK(levi_type_supported((uint8_t)t));
    if (!have) {
      continue;
    }
    LeviInfo li;
    decode_levi(buf, n, true, li);
    CHECK(li.kind == LeviInfo::Kind::Decoded);
    CHECK(li.crc_ok && li.hdr_verified);
    CHECK_EQ(li.type, t);
    CHECK(li.fields.n >= 1);
    for (int i = 0; i < li.fields.n; i++) {
      CHECK(!strstr(li.fields.f[i].value, "nicht dekodiert"));
    }
  }
  CHECK_EQ(known, 14); // aktuell 14 Typen; bei Aenderung bewusst anpassen
}

static void test_dbg_levi_field_values() {
  uint8_t buf[LVP_MAX_FRAME];
  size_t n;
  LeviInfo li;

  sample_for(LVP_MSG_SET_STATE, buf, &n);
  decode_levi(buf, n, true, li);
  CHECK(has_field(li, "mask", "POWER|COLOR"));
  CHECK(has_field(li, "power", "1 = an"));
  CHECK(has_field(li, "brightness", "nicht in Maske"));
  CHECK(has_field(li, "rgb", "#FF8000"));
  CHECK(has_field(li, "rgb_modus", "mit Kalibrierung"));
  CHECK(has_field(li, "music", "nicht in Maske"));

  sample_for(LVP_MSG_STATE, buf, &n);
  decode_levi(buf, n, true, li);
  CHECK(has_field(li, "power", "-1 = unbekannt"));
  CHECK(has_field(li, "sent_revision", "noch nicht komplett"));
  CHECK(has_field(li, "flags", "MUSIC|BLE"));
  CHECK(has_field(li, "calib_revision", "3"));
  CHECK(has_field(li, "brightness", "70 %"));

  sample_for(LVP_MSG_CALIB, buf, &n);
  decode_levi(buf, n, true, li);
  CHECK(has_field(li, "custom", "gespeicherte Kalibrierung"));
  CHECK(has_field(li, "punkt[0]", "R=0 G=8 B=16"));
  CHECK(has_field(li, "punkt[9]", "#"));
  CHECK_EQ(li.fields.n, 12);
  CHECK(!li.fields.overflow);

  sample_for(LVP_MSG_NACK, buf, &n);
  decode_levi(buf, n, true, li);
  CHECK(has_field(li, "error", "3 = nicht unterstuetzt"));

  sample_for(LVP_MSG_PAIR_RSP, buf, &n);
  decode_levi(buf, n, true, li);
  CHECK(has_field(li, "result", "6 = Kopplung nicht freigegeben"));

  sample_for(LVP_MSG_DISCOVER, buf, &n);
  decode_levi(buf, n, true, li);
  CHECK(has_field(li, "controller_id", "24:0A:C4:00:00:02"));
  CHECK(has_field(li, "name", "\"Remote\""));
}

// ---- Ausgabe ----

static void test_dbg_format_record() {
  uint8_t lv[LVP_MAX_FRAME], fr[400];
  size_t ln = levi_frame(lv, LVP_MSG_STATE, LVP_FLAG_RESPONSE | LVP_FLAG_NOTIFY, 12, 11, [](lvp_msg_t &m) {
    m.u.state.revision = 5;
    m.u.state.sent_revision = 5;
    m.u.state.power = 1;
    m.u.state.brightness = 100;
  });
  size_t n = build80211(fr, CTRL, GW, lv, ln);
  Capture c;
  fill_cap(c, fr, n);
  std::string s = render(c);
  CHECK(has(s, "#1  t=12.345s  Kanal 1  RSSI -52 dBm"));
  CHECK(has(s, "Weg: Promiscuous"));
  CHECK(has(s, "Absender   : 24:0A:C4:00:00:01"));
  CHECK(has(s, "Ziel       : 24:0A:C4:00:00:02  (Unicast)"));
  CHECK(has(s, "Rahmen laut Treiber"));
  CHECK(has(s, "inkl. 4 Byte FCS"));
  CHECK(has(s, "MAC-Kopf 24"));
  CHECK(has(s, "Zufallswerte DEADBEEF"));
  CHECK(has(s, "Bewertung  : OK"));
  CHECK(has(s, "Typ      : STATE (0x22)"));
  CHECK(has(s, "Flags    : 0x03 [RESPONSE|NOTIFY]"));
  CHECK(has(s, "Sequenz  : 12   Referenzsequenz: 11   Sitzung: 0xCAFE0001"));
  CHECK(has(s, "CRC      : OK"));
  CHECK(has(s, "revision"));
  CHECK(has(s, "0000  4C 56 01 22")); // Hexdump: 'L' 'V' Version Typ
  CHECK(has(s, "|LV.\""));            // ASCII-Spalte
  FormatOptions no;
  no.ascii = false;
  CHECK(!has(render(c, no), "|LV"));
  FormatOptions raw;
  raw.full_frame = true;
  CHECK(has(render(c, raw), "Rohrahmen"));
  CHECK(!has(s, "Rohrahmen"));
}

static void test_dbg_out_truncation() {
  char small[40];
  Out o(small, sizeof(small));
  o.printf("%s", "0123456789012345678901234567890123456789012345");
  CHECK(o.truncated());
  CHECK_EQ(strlen(small), sizeof(small) - 1);
  o.printf("mehr"); // nach dem Abschneiden passiert nichts mehr
  CHECK_EQ(strlen(small), sizeof(small) - 1);
  Out z(small, 0); // Groesse 0: nichts schreiben
  z.printf("x");
  uint8_t data[100];
  for (int i = 0; i < 100; i++) {
    data[i] = (uint8_t)i;
  }
  Out h(small, sizeof(small));
  hexdump(h, "  ", data, sizeof(data), true);
  CHECK(h.truncated());
  CHECK(strlen(small) < sizeof(small));
}

static void test_dbg_hexdump_and_mac() {
  char b[256];
  Out o(b, sizeof(b));
  const uint8_t d[] = {'A', 'b', 0x00, 0x7F, 0xFF};
  hexdump(o, ">", d, sizeof(d), true);
  CHECK(has(b, ">0000  41 62 00 7F FF"));
  CHECK(has(b, "|Ab...|"));
  Out e(b, sizeof(b));
  hexdump(e, ">", d, 0, true);
  CHECK(has(b, "(leer)"));
  uint8_t m[6];
  CHECK(mac_parse("aa:bb:cc:dd:ee:0f", m) && m[0] == 0xAA && m[5] == 0x0F);
  CHECK(mac_parse("AA-BB-CC-DD-EE-FF", m));
  CHECK(!mac_parse("AA:BB:CC:DD:EE", m));
  CHECK(!mac_parse("AA:BB:CC:DD:EE:FF:00", m));
  CHECK(!mac_parse("AA:BB:CC:DD:EE:GG", m));
  CHECK(!mac_parse("AABBCCDDEEFF", m));
  CHECK(!mac_parse("", m));
  CHECK(!mac_parse(nullptr, m));
}

// ---- Filter, Doppelerkennung, Befehle ----

static void parse_with(FrameInfo &fi, LeviInfo &li, lvp_msg_type_t t, const uint8_t dst[6], const uint8_t src[6], bool prot = false) {
  static uint8_t lv[LVP_MAX_FRAME], fr[400];
  static Capture c;
  size_t ln = levi_frame(lv, t, 0, 1, 0, t == LVP_MSG_ANNOUNCE ? fill_announce : nullptr);
  Opt o;
  o.prot = prot;
  size_t n = build80211(fr, dst, src, lv, ln, o);
  fill_cap(c, fr, n);
  parse_capture(c, fi);
  li = LeviInfo();
  if (fi.verdict == Verdict::Ok) {
    decode_levi(fi.body, fi.body_len, fi.body_complete, li);
  }
}

static void test_dbg_filter() {
  FrameInfo fi;
  LeviInfo li;
  Filter f;
  parse_with(fi, li, LVP_MSG_ANNOUNCE, BC, GW);
  CHECK(f.match(fi, li)); // ohne Filter alles
  f.add_mac(CTRL);
  CHECK(!f.match(fi, li)); // weder Absender noch Ziel
  f.add_mac(GW);
  CHECK(f.match(fi, li));
  CHECK(f.del_mac(GW) && !f.del_mac(GW));
  parse_with(fi, li, LVP_MSG_ACK, CTRL, GW);
  CHECK(f.match(fi, li)); // Ziel passt
  f.clear();
  f.set_type(LVP_MSG_STATE, true);
  CHECK(f.type_active());
  CHECK(!f.match(fi, li)); // ACK
  parse_with(fi, li, LVP_MSG_STATE, CTRL, GW);
  CHECK(f.match(fi, li));
  // Verschluesselter Rahmen: kein bestaetigter Typ, wird bei Typfilter nicht gezeigt
  FrameInfo fe;
  LeviInfo le;
  parse_with(fe, le, LVP_MSG_STATE, CTRL, GW, true);
  CHECK(fe.verdict == Verdict::Encrypted);
  CHECK(!f.match(fe, le));
  f.clear();
  CHECK(f.match(fe, le));
  f.show_encrypted = false;
  CHECK(!f.match(fe, le));
  f.show_encrypted = true;
  f.levi_only = true;
  CHECK(!f.match(fe, le)); // unlesbar: nicht als LeviLuma bekannt
  CHECK(f.match(fi, li));
  // Typfilter blendet Rahmen mit ungeprueftem Kopf aus
  LeviInfo bad;
  uint8_t lv[LVP_MAX_FRAME];
  size_t ln = levi_frame(lv, LVP_MSG_STATE, 0, 1, 0);
  lv[20 > (int)ln - 1 ? ln - 1 : 17] ^= 1;
  decode_levi(lv, ln, true, bad);
  f.clear();
  f.set_type(LVP_MSG_STATE, true);
  CHECK(!bad.hdr_verified);
  CHECK(!f.match(fi, bad));
  // MAC-Filter ohne Adressen (Callback ohne Info)
  FrameInfo none;
  f.clear();
  f.add_mac(GW);
  CHECK(!f.match(none, li));
  // Grenzen
  f.clear();
  for (int i = 0; i < Filter::MAX_MACS; i++) {
    uint8_t m[6] = {1, 2, 3, 4, 5, (uint8_t)i};
    CHECK(f.add_mac(m));
  }
  uint8_t extra[6] = {9, 9, 9, 9, 9, 9};
  CHECK(!f.add_mac(extra));
  char b[512];
  Out o(b, sizeof(b));
  f.describe(o);
  CHECK(has(b, "01:02:03:04:05:00"));
}

static void test_dbg_dedup() {
  FrameInfo fi;
  LeviInfo li;
  parse_with(fi, li, LVP_MSG_STATE, CTRL, GW);
  Dedup d(50);
  CHECK(!d.seen(Source::Promiscuous, fi, 1000));
  CHECK(d.seen(Source::EspNowCallback, fi, 1003));    // anderer Weg, im Fenster: Duplikat
  CHECK(!d.seen(Source::EspNowCallback, fi, 1004));   // der Eintrag erklaert nur ein Duplikat
  // gleiche Quelle zweimal (z. B. MAC-Wiederholung): nie unterdruecken
  Dedup d2(50);
  CHECK(!d2.seen(Source::Promiscuous, fi, 2000));
  CHECK(!d2.seen(Source::Promiscuous, fi, 2001));
  // ausserhalb des Fensters
  Dedup d3(50);
  CHECK(!d3.seen(Source::Promiscuous, fi, 3000));
  CHECK(!d3.seen(Source::EspNowCallback, fi, 3100));
  // anderer Inhalt
  FrameInfo f2;
  LeviInfo l2;
  parse_with(f2, l2, LVP_MSG_ACK, CTRL, GW);
  Dedup d4(50);
  CHECK(!d4.seen(Source::Promiscuous, fi, 4000));
  CHECK(!d4.seen(Source::EspNowCallback, f2, 4001));
  // Zeitueberlauf von t_ms
  Dedup d5(50);
  CHECK(!d5.seen(Source::Promiscuous, fi, 0xFFFFFFF0u));
  CHECK(d5.seen(Source::EspNowCallback, fi, 0x00000005u));
  // verschluesselte Rahmen werden nie als Duplikat gewertet
  FrameInfo fe;
  LeviInfo le;
  parse_with(fe, le, LVP_MSG_STATE, CTRL, GW, true);
  Dedup d6(50);
  CHECK(!d6.seen(Source::Promiscuous, fe, 10));
  CHECK(!d6.seen(Source::EspNowCallback, fe, 11));
  // Ringpuffer ueberlaeuft ohne Fehler
  Dedup d7(50);
  for (int i = 0; i < 100; i++) {
    d7.seen(Source::Promiscuous, fi, 5000 + i * 100);
  }
}

static std::string cmd(Settings &st, const char *line, CmdEffects *fxo = nullptr, bool *ok = nullptr, const Stats *stats = nullptr) {
  static char buf[4096];
  Out o(buf, sizeof(buf));
  CmdEffects fx;
  Stats s0;
  bool r = run_command(line, st, stats ? *stats : s0, 65432, o, fx);
  if (fxo) {
    *fxo = fx;
  }
  if (ok) {
    *ok = r;
  }
  return buf;
}

static void test_dbg_commands() {
  Settings st;
  st.plan.first = 1;
  st.plan.last = 11;
  st.plan.current = 1;
  CmdEffects fx;
  bool ok;
  CHECK(has(cmd(st, "help", &fx, &ok), "stats"));
  CHECK(ok);
  CHECK(cmd(st, "", &fx, &ok).empty() && ok);
  CHECK(cmd(st, "   ", &fx, &ok).empty() && ok);
  CHECK(has(cmd(st, "HELP", &fx, &ok), "channel") && ok);
  cmd(st, "bogus", &fx, &ok);
  CHECK(!ok);
  // Kanal
  CHECK(has(cmd(st, "channel", &fx, &ok), "1..11") && !fx.set_channel);
  cmd(st, "channel 6", &fx, &ok);
  CHECK(ok && fx.set_channel && fx.channel == 6);
  cmd(st, "channel 13", &fx, &ok);
  CHECK(!ok && !fx.set_channel); // laut Plan nicht zulaessig
  cmd(st, "channel 0", &fx, &ok);
  CHECK(!ok && !fx.set_channel);
  cmd(st, "channel 99", &fx, &ok);
  CHECK(!ok && !fx.set_channel);
  cmd(st, "channel x", &fx, &ok);
  CHECK(!ok);
  cmd(st, "channel 6x", &fx, &ok);
  CHECK(!ok);
  cmd(st, "channel -1", &fx, &ok);
  CHECK(!ok);
  st.plan.last = 13;
  cmd(st, "channel 13", &fx, &ok);
  CHECK(ok && fx.channel == 13);
  // Suchlauf: nur wenn erlaubt
  CHECK(has(cmd(st, "scan", &fx, &ok), "NICHT erlaubt"));
  cmd(st, "scan on", &fx, &ok);
  CHECK(!ok && !fx.scan_start);
  st.plan.scan_available = true;
  cmd(st, "scan on", &fx, &ok);
  CHECK(ok && fx.scan_start && fx.dwell_ms == 500);
  CHECK(has(cmd(st, "scan on 250", &fx, &ok), "250 ms") && fx.dwell_ms == 250);
  cmd(st, "scan on 50", &fx, &ok);
  CHECK(!ok && !fx.scan_start);
  cmd(st, "scan on 9999", &fx, &ok);
  CHECK(!ok);
  cmd(st, "scan off", &fx, &ok);
  CHECK(ok && fx.scan_stop);
  st.plan.scanning = true;
  cmd(st, "channel 5", &fx, &ok);
  CHECK(ok && fx.set_channel && fx.scan_stop); // fester Kanal beendet den Suchlauf
  st.plan.scanning = false;
  // Filter
  cmd(st, "filter mac add 24:0a:c4:00:00:01", &fx, &ok);
  CHECK(ok && st.filter.nmacs == 1);
  cmd(st, "filter mac add nonsense", &fx, &ok);
  CHECK(!ok && st.filter.nmacs == 1);
  cmd(st, "filter mac del 24:0A:C4:00:00:01", &fx, &ok);
  CHECK(ok && st.filter.nmacs == 0);
  cmd(st, "filter mac del 24:0A:C4:00:00:01", &fx, &ok);
  CHECK(!ok);
  for (int i = 0; i < 4; i++) {
    char l[64];
    snprintf(l, sizeof(l), "filter mac add 00:00:00:00:00:0%d", i);
    cmd(st, l, &fx, &ok);
    CHECK(ok);
  }
  cmd(st, "filter mac add 00:00:00:00:00:99", &fx, &ok);
  CHECK(!ok);
  cmd(st, "filter mac clear", &fx, &ok);
  CHECK(ok && st.filter.nmacs == 0);
  cmd(st, "filter type add STATE", &fx, &ok);
  CHECK(ok && st.filter.has_type(LVP_MSG_STATE));
  cmd(st, "filter type add set_state", &fx, &ok);
  CHECK(ok && st.filter.has_type(LVP_MSG_SET_STATE));
  cmd(st, "filter type add 0x7e", &fx, &ok);
  CHECK(ok && st.filter.has_type(LVP_MSG_ACK));
  cmd(st, "filter type add 55", &fx, &ok); // unbekannter Typ als Zahl ist erlaubt (Filter auf Rohwert)
  CHECK(ok && st.filter.has_type(55));
  cmd(st, "filter type add NOPE", &fx, &ok);
  CHECK(!ok);
  cmd(st, "filter type add 256", &fx, &ok);
  CHECK(!ok);
  cmd(st, "filter type del STATE", &fx, &ok);
  CHECK(ok && !st.filter.has_type(LVP_MSG_STATE));
  CHECK(has(cmd(st, "filter", &fx, &ok), "SET_STATE (0x21)"));
  cmd(st, "filter type clear", &fx, &ok);
  CHECK(!st.filter.type_active());
  cmd(st, "filter levi on", &fx, &ok);
  CHECK(ok && st.filter.levi_only);
  cmd(st, "filter enc off", &fx, &ok);
  CHECK(ok && !st.filter.show_encrypted);
  cmd(st, "filter enc maybe", &fx, &ok);
  CHECK(!ok);
  cmd(st, "filter clear", &fx, &ok);
  CHECK(ok && !st.filter.levi_only && st.filter.show_encrypted);
  cmd(st, "filter wat", &fx, &ok);
  CHECK(!ok);
  // Darstellung
  cmd(st, "raw on", &fx, &ok);
  CHECK(ok && st.fmt.full_frame);
  cmd(st, "ascii off", &fx, &ok);
  CHECK(ok && !st.fmt.ascii);
  cmd(st, "raw", &fx, &ok);
  CHECK(!ok);
  // Statistik
  Stats s;
  s.queue_drops = 7;
  s.queued = 100;
  s.dup_dropped = 2;
  std::string t = cmd(st, "stats", &fx, &ok, &s);
  CHECK(ok && has(t, "VERWORFEN (Queue voll)    : 7") && has(t, "Verlust") == false);
  CHECK(has(t, "Nachrichten gingen verloren"));
  CHECK(has(t, "65.432 s"));
  cmd(st, "stats reset", &fx, &ok);
  CHECK(ok && fx.reset_stats);
  cmd(st, "stats foo", &fx, &ok);
  CHECK(!ok);
  // Eingabegrenzen
  cmd(st, "a b c d e f g", &fx, &ok);
  CHECK(!ok);
  cmd(st, "a b c d e f   ", &fx, &ok); // genau 6 Woerter plus Leerzeichen sind erlaubt (Befehl ist unbekannt, aber nicht "zu viele")
  CHECK(!ok);
  std::string longline(200, 'x');
  CHECK(has(cmd(st, longline.c_str(), &fx, &ok), "zu lang"));
  CHECK(!ok);
  cmd(st, nullptr, &fx, &ok);
  CHECK(ok);
}

static void test_dbg_stats_format() {
  Stats s;
  s.by_channel[1] = 5;
  s.by_channel[0] = 2;
  s.mgmt_seen = 100;
  s.ignored = 90;
  char b[2048];
  Out o(b, sizeof(b));
  format_stats(o, s, 1500);
  CHECK(has(b, "1.500 s"));
  CHECK(has(b, "K1=5"));
  CHECK(has(b, "unbekannt=2"));
  CHECK(has(b, "ignoriert: 90"));
  CHECK(!has(b, "gingen verloren")); // keine Verluste: keine Warnung
}

static void test_dbg_channel_plan() {
  ChannelPlan p;
  p.first = 1;
  p.last = 13;
  CHECK(p.valid(1) && p.valid(13));
  CHECK(!p.valid(0) && !p.valid(14) && !p.valid(-3) && !p.valid(200));
  CHECK_EQ(p.next_after(1), 2);
  CHECK_EQ(p.next_after(12), 13);
  CHECK_EQ(p.next_after(13), 1); // Umlauf
  CHECK_EQ(p.next_after(0), 1);
  p.first = 3;
  p.last = 5;
  CHECK_EQ(p.next_after(1), 3);
  CHECK_EQ(p.next_after(5), 3);
  CHECK(!p.valid(2));
}

// ---- Robustheit ----

static void test_dbg_fuzz_no_crash() {
  // Zufallsbytes und gezielt beschaedigte gueltige Rahmen: Parser und Dekoder duerfen weder abstuerzen noch ueber Puffer lesen
  // (ASan/UBSan). Festes Seed fuer Nachvollziehbarkeit.
  srand(4711);
  uint8_t lv[LVP_MAX_FRAME], fr[400];
  size_t ln = levi_frame(lv, LVP_MSG_CALIB, LVP_FLAG_RESPONSE, 1, 1, [](lvp_msg_t &m) { m.u.calib.custom = 1; });
  size_t good = build80211(fr, GW, CTRL, lv, ln);
  Capture c;
  FrameInfo fi;
  LeviInfo li;
  static char buf[16384];
  int decoded = 0;

  for (int iter = 0; iter < 20000; iter++) {
    memset(&c, 0, sizeof(c));
    c.source = (iter % 5 == 0) ? Source::EspNowCallback : Source::Promiscuous;
    c.fcs_included = iter % 3 != 0;
    c.has_addr = c.source == Source::EspNowCallback;
    size_t len;

    if (iter % 2) {
      // beschaedigter guter Rahmen
      len = good;
      memcpy(c.data, fr, good);
      int flips = 1 + rand() % 4;
      for (int k = 0; k < flips; k++) {
        c.data[rand() % len] ^= (uint8_t)(1 << (rand() % 8));
      }
    } else {
      len = (size_t)(rand() % (MAX_CAPTURE + 1));
      for (size_t i = 0; i < len; i++) {
        c.data[i] = (uint8_t)rand();
      }
      if (len > 28 && rand() % 2) { // oefter in die tiefen Pfade kommen
        c.data[0] = 0xD0;
        c.data[1] = 0;
        c.data[24] = 127;
        memcpy(c.data + 25, ESPNOW_OUI, 3);
        if (len > 40) {
          c.data[32] = 221;
          memcpy(c.data + 34, ESPNOW_OUI, 3);
          c.data[37] = 4;
        }
      }
    }
    c.orig_len = (uint16_t)(len + (size_t)(rand() % 3 == 0 ? rand() % 400 : 0));
    if (rand() % 7 == 0 && len > 0) {
      c.orig_len = (uint16_t)(rand() % (len + 1));
    }
    c.len = (uint16_t)len;
    (void)is_espnow_candidate(c.data, c.len);
    parse_capture(c, fi);

    if (fi.body && (fi.verdict == Verdict::Ok || fi.verdict == Verdict::Truncated)) {
      CHECK(fi.body >= c.data && fi.body + fi.body_len <= c.data + MAX_CAPTURE);
      decode_levi(fi.body, fi.body_len, fi.body_complete, li);
      decoded += li.kind == LeviInfo::Kind::Decoded;
    }
    if (iter % 50 == 0) {
      Out o(buf, sizeof(buf));
      format_record(o, (uint32_t)iter, c, fi, li, FormatOptions());
    }
  }
  CHECK(decoded > 0); // die unbeschaedigten Faelle kommen vor
  // Laenge ueber dem Erfassungspuffer wird abgewiesen statt gelesen
  memset(&c, 0, sizeof(c));
  c.len = MAX_CAPTURE + 1;
  parse_capture(c, fi);
  CHECK(fi.verdict == Verdict::Malformed);
}

void tests_debug(void) {
  RUN_D(test_dbg_frame_valid_broadcast, "Gueltiger Broadcast-Rahmen (synthetisch): Adressen, Laengen, ESP-NOW-Kopf, LeviLuma-Felder, CRC.");
  RUN_D(test_dbg_frame_unicast_multicast_retry, "Unicast, Multicast, Retry-Bit (synthetisch).");
  RUN_D(test_dbg_frame_encrypted_not_decoded, "Verschluesselter Rahmen (Protected-Bit, synthetisch): nur Metadaten und Rohdaten, nie dekodiert.");
  RUN_D(test_dbg_frame_candidate_prefilter, "Vorfilter im Callback: Action/Action-No-Ack, Kategorie 127, Beacon und kurze Rahmen.");
  RUN_D(test_dbg_frame_not_espnow, "Fremde Rahmen (Beacon, andere Kategorie, andere OUI) werden nicht als ESP-NOW ausgewertet.");
  RUN_D(test_dbg_frame_too_short_and_malformed, "Zu kurze und beschaedigte Rahmen (Elementlaenge, fehlendes Element, falsche Treiberlaenge) ohne Absturz.");
  RUN_D(test_dbg_frame_capture_truncated, "Abgeschnittene Erfassung: Urteil, keine CRC-Aussage, ungepruefte Kopffelder gekennzeichnet; mehrere Elemente.");
  RUN_D(test_dbg_frame_multi_element_and_version, "Mehrere ESP-NOW-Elemente und rohes Versionsfeld (nicht gedeutet).");
  RUN_D(test_dbg_frame_payload_only_path, "Normaler ESP-NOW-Empfang (nur Nutzdaten): fehlende Angaben als nicht verfuegbar.");
  RUN_D(test_dbg_levi_errors, "LeviLuma-Fehlerfaelle: CRC, unbekannte Version/Typ, falsche Nutzdaten, Laenge, kurzer Kopf, Fremddaten.");
  RUN_D(test_dbg_levi_flags, "Flag-Beschriftung inkl. unbekannter Bits und kleinem Puffer.");
  RUN_D(test_dbg_levi_all_protocol_types, "Jeder Nachrichtentyp des gemeinsamen Protokolls hat Testrahmen und Dekodierung (faengt neue Typen).");
  RUN_D(test_dbg_levi_field_values, "Feldnamen und Werte (SET_STATE, STATE, CALIB, NACK, PAIR_RSP, DISCOVER).");
  RUN_D(test_dbg_format_record, "Ausgabeformat einer Nachricht, Hexdump, ASCII-Spalte, Rohrahmen.");
  RUN_D(test_dbg_out_truncation, "Ausgabepuffer: Abschneiden ohne Ueberlauf.");
  RUN_D(test_dbg_hexdump_and_mac, "Hexdump und MAC-Parser.");
  RUN_D(test_dbg_filter, "Filter nach MAC, LeviLuma-Typ, Kennung, Verschluesselung.");
  RUN_D(test_dbg_dedup, "Doppelerkennung zwischen zwei Empfangswegen (nie bei gleichem Weg).");
  RUN_D(test_dbg_commands, "Monitorbefehle: Hilfe, Kanal, Suchlauf, Filter, Anzeige, Statistik, Eingabegrenzen.");
  RUN_D(test_dbg_stats_format, "Statistikausgabe inkl. Verlustwarnung.");
  RUN_D(test_dbg_channel_plan, "Kanalplan: zulaessiger Bereich und Umlauf.");
  RUN_D(test_dbg_fuzz_no_crash, "Fuzzing (20000 Faelle) von Rahmenauswertung, Dekodierung und Ausgabe unter ASan/UBSan.");
}
