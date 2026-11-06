// Host-Tests: ESP-NOW-Protokoll (Kodierung, Dekodierung, Fehlerfaelle) und Zuverlaessigkeitsschicht.
#include "test_util.h"
#include "leviluma_link.h"
#include "leviluma_proto.h"

static const uint8_t ID_A[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
static const uint8_t ID_B[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};

static size_t enc(const lvp_msg_t &m, uint8_t *buf) {
  lvp_err_t e = LVP_ERR_BAD_REQUEST;
  size_t n = lvp_encode(&m, buf, LVP_MAX_FRAME, &e);
  CHECK_EQ(e, LVP_OK);
  return n;
}

static void test_crc_reference() {
  // Referenzwert CRC-16/CCITT-FALSE fuer "123456789" ist 0x29B1
  const uint8_t s[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  CHECK_EQ(lvp_crc16(s, 9, 0xFFFF), 0x29B1);
}

static void test_header_layout_little_endian() {
  lvp_msg_t m;
  lvp_init(&m, LVP_MSG_GET_STATE);
  m.hdr.seq = 0x1234;
  m.hdr.ref_seq = 0xABCD;
  m.hdr.session = 0x11223344;
  m.hdr.flags = LVP_FLAG_RESPONSE;
  uint8_t b[LVP_MAX_FRAME];
  size_t n = enc(m, b);
  CHECK_EQ(n, LVP_HEADER_LEN);
  CHECK_EQ(b[0], 'L');
  CHECK_EQ(b[1], 'V');
  CHECK_EQ(b[2], LVP_VERSION);
  CHECK_EQ(b[3], LVP_MSG_GET_STATE);
  CHECK_EQ(b[4], LVP_FLAG_RESPONSE);
  CHECK_EQ(b[5], 0);
  CHECK_EQ(b[6], 0x34);
  CHECK_EQ(b[7], 0x12);
  CHECK_EQ(b[8], 0xCD);
  CHECK_EQ(b[9], 0xAB);
  CHECK_EQ(b[10], 0x44);
  CHECK_EQ(b[13], 0x11);
}

static void test_roundtrip_all_types() {
  uint8_t b[LVP_MAX_FRAME];
  lvp_msg_t m, d;

  lvp_init(&m, LVP_MSG_DISCOVER);
  memcpy(m.u.discover.controller_id, ID_A, 6);
  strcpy(m.u.discover.name, "Wohnzimmer");
  size_t n = enc(m, b);
  CHECK_EQ(lvp_decode(b, n, &d), LVP_OK);
  CHECK(lvp_id_equal(d.u.discover.controller_id, ID_A));
  CHECK(strcmp(d.u.discover.name, "Wohnzimmer") == 0);

  lvp_init(&m, LVP_MSG_ANNOUNCE);
  memcpy(m.u.announce.gateway_id, ID_B, 6);
  m.u.announce.fw_version = 0x0102;
  m.u.announce.caps = LVP_GW_KEY_REQUIRED | LVP_GW_BLE_CONNECTED;
  m.u.announce.state_revision = 0xDEADBEEF;
  strcpy(m.u.announce.name, "LeviLuma-EE01");
  n = enc(m, b);
  CHECK_EQ(lvp_decode(b, n, &d), LVP_OK);
  CHECK_EQ(d.u.announce.fw_version, 0x0102);
  CHECK_EQ(d.u.announce.caps, LVP_GW_KEY_REQUIRED | LVP_GW_BLE_CONNECTED);
  CHECK_EQ(d.u.announce.state_revision, 0xDEADBEEFu);
  CHECK(strcmp(d.u.announce.name, "LeviLuma-EE01") == 0);

  lvp_init(&m, LVP_MSG_SET_STATE);
  m.u.set_state = {LVP_SET_POWER | LVP_SET_BRIGHTNESS | LVP_SET_COLOR, 1, 55, 255, 64, 64, 0};
  n = enc(m, b);
  CHECK_EQ(lvp_decode(b, n, &d), LVP_OK);
  CHECK_EQ(d.u.set_state.mask, LVP_SET_POWER | LVP_SET_BRIGHTNESS | LVP_SET_COLOR);
  CHECK_EQ(d.u.set_state.brightness, 55);
  CHECK_EQ(d.u.set_state.g, 64);

  lvp_init(&m, LVP_MSG_STATE);
  m.u.state.revision = 7;
  m.u.state.sent_revision = 6;
  m.u.state.power = -1;
  m.u.state.brightness = 100;
  m.u.state.r = 1;
  m.u.state.g = 2;
  m.u.state.b = 3;
  m.u.state.flags = LVP_STATE_BLE | LVP_STATE_MUSIC;
  m.u.state.calib_revision = 513;
  n = enc(m, b);
  CHECK_EQ(lvp_decode(b, n, &d), LVP_OK);
  CHECK_EQ(d.u.state.revision, 7);
  CHECK_EQ(d.u.state.sent_revision, 6);
  CHECK_EQ(d.u.state.power, -1);
  CHECK_EQ(d.u.state.flags, LVP_STATE_BLE | LVP_STATE_MUSIC);
  CHECK_EQ(d.u.state.calib_revision, 513);

  lvp_init(&m, LVP_MSG_CALIB);
  m.u.calib.custom = 1;
  m.u.calib.calib_revision = 9;
  for (int i = 0; i < LVP_CAL_BYTES; i++) {
    m.u.calib.points[i] = (uint8_t)(i * 8);
  }
  n = enc(m, b);
  CHECK_EQ(n, LVP_HEADER_LEN + 3 + LVP_CAL_BYTES);
  CHECK_EQ(lvp_decode(b, n, &d), LVP_OK);
  CHECK(memcmp(d.u.calib.points, m.u.calib.points, LVP_CAL_BYTES) == 0);
  CHECK_EQ(d.u.calib.calib_revision, 9);

  lvp_init(&m, LVP_MSG_SET_CALIB);
  memset(m.u.set_calib.points, 200, LVP_CAL_BYTES);
  n = enc(m, b);
  CHECK_EQ(lvp_decode(b, n, &d), LVP_OK);
  CHECK_EQ(d.u.set_calib.points[29], 200);

  lvp_init(&m, LVP_MSG_PAIR_RSP);
  m.u.pair_rsp.result = LVP_OK;
  memcpy(m.u.pair_rsp.gateway_id, ID_B, 6);
  n = enc(m, b);
  CHECK_EQ(lvp_decode(b, n, &d), LVP_OK);
  CHECK(lvp_id_equal(d.u.pair_rsp.gateway_id, ID_B));
  CHECK_EQ(d.u.pair_rsp.name[0], 0);

  lvp_init(&m, LVP_MSG_NACK);
  m.u.nack.error = LVP_ERR_NOT_PAIRED;
  n = enc(m, b);
  CHECK_EQ(lvp_decode(b, n, &d), LVP_OK);
  CHECK_EQ(d.u.nack.error, LVP_ERR_NOT_PAIRED);

  const lvp_msg_type_t empty[] = {LVP_MSG_GET_STATE, LVP_MSG_GET_CALIB, LVP_MSG_RESET_CALIB, LVP_MSG_ACK};
  for (lvp_msg_type_t t : empty) {
    lvp_init(&m, t);
    n = enc(m, b);
    CHECK_EQ(n, LVP_HEADER_LEN);
    CHECK_EQ(lvp_decode(b, n, &d), LVP_OK);
    CHECK_EQ(d.hdr.type, t);
  }
}

static void test_frame_size_limit() {
  // Groesster Rahmen (ANNOUNCE mit langem Namen) bleibt unter 250 Byte
  lvp_msg_t m;
  lvp_init(&m, LVP_MSG_ANNOUNCE);
  memset(m.u.announce.name, 'x', LVP_NAME_MAX);
  m.u.announce.name[LVP_NAME_MAX] = 0;
  uint8_t b[LVP_MAX_FRAME];
  size_t n = enc(m, b);
  CHECK(n <= LVP_MAX_FRAME);
  CHECK(n > 0);
  // Zu kleiner Puffer wird abgelehnt
  lvp_err_t e = LVP_OK;
  CHECK_EQ(lvp_encode(&m, b, 20, &e), 0);
  CHECK_EQ(e, LVP_ERR_TOO_BIG);
}

static void test_name_truncated_and_sanitized() {
  lvp_msg_t m, d;
  lvp_init(&m, LVP_MSG_DISCOVER);
  memset(m.u.discover.name, 'A', sizeof(m.u.discover.name) - 1); // 24 Zeichen + Terminator
  uint8_t b[LVP_MAX_FRAME];
  size_t n = enc(m, b);
  CHECK_EQ(lvp_decode(b, n, &d), LVP_OK);
  CHECK_EQ(strlen(d.u.discover.name), LVP_NAME_MAX);

  // Steuerzeichen im Namen werden beim Lesen ersetzt
  strcpy(m.u.discover.name, "a\nb");
  n = enc(m, b);
  CHECK_EQ(lvp_decode(b, n, &d), LVP_OK);
  CHECK(strcmp(d.u.discover.name, "a?b") == 0);
}

static void test_decode_errors() {
  uint8_t b[LVP_MAX_FRAME];
  lvp_msg_t m, d;
  lvp_init(&m, LVP_MSG_SET_STATE);
  m.u.set_state = {LVP_SET_BRIGHTNESS, 0, 40, 0, 0, 0, 0};
  size_t n = enc(m, b);

  CHECK_EQ(lvp_decode(b, 5, &d), LVP_ERR_SHORT);
  CHECK_EQ(lvp_decode(b, n - 1, &d), LVP_ERR_LENGTH);

  uint8_t c[LVP_MAX_FRAME];
  memcpy(c, b, n);
  c[0] = 'X';
  CHECK_EQ(lvp_decode(c, n, &d), LVP_ERR_MAGIC);

  memcpy(c, b, n);
  c[LVP_HEADER_LEN + 2] ^= 0x01; // Nutzdaten verfaelscht
  CHECK_EQ(lvp_decode(c, n, &d), LVP_ERR_CRC);

  // Falsche Version mit korrekter CRC
  memcpy(c, b, n);
  c[2] = 9;
  c[14] = c[15] = 0;
  uint16_t crc = lvp_crc16(c, n, 0xFFFF);
  c[14] = (uint8_t)crc;
  c[15] = (uint8_t)(crc >> 8);
  CHECK_EQ(lvp_decode(c, n, &d), LVP_ERR_VERSION);

  // Unbekannter Typ
  memcpy(c, b, n);
  c[3] = 0x55;
  c[14] = c[15] = 0;
  crc = lvp_crc16(c, n, 0xFFFF);
  c[14] = (uint8_t)crc;
  c[15] = (uint8_t)(crc >> 8);
  CHECK_EQ(lvp_decode(c, n, &d), LVP_ERR_UNSUPPORTED);

  // Nutzdaten zu kurz fuer den Typ (Laenge stimmt zum Kopf, aber SET_STATE braucht 7 Byte)
  memcpy(c, b, n);
  c[5] = 3;
  c[14] = c[15] = 0;
  crc = lvp_crc16(c, LVP_HEADER_LEN + 3, 0xFFFF);
  c[14] = (uint8_t)crc;
  c[15] = (uint8_t)(crc >> 8);
  CHECK_EQ(lvp_decode(c, LVP_HEADER_LEN + 3, &d), LVP_ERR_BAD_REQUEST);

  // Ueberlange Namenslaenge im Rahmen
  lvp_init(&m, LVP_MSG_DISCOVER);
  n = enc(m, b);
  b[LVP_HEADER_LEN + 6] = 200;
  b[14] = b[15] = 0;
  crc = lvp_crc16(b, n, 0xFFFF);
  b[14] = (uint8_t)crc;
  b[15] = (uint8_t)(crc >> 8);
  CHECK_EQ(lvp_decode(b, n, &d), LVP_ERR_BAD_REQUEST);
}

static void test_set_state_validation() {
  lvp_set_state_t s = {0, 0, 0, 0, 0, 0, 0};
  CHECK_EQ(lvp_validate_set_state(&s), LVP_ERR_BAD_REQUEST); // leere Maske
  s = {LVP_SET_BRIGHTNESS, 0, 0, 0, 0, 0, 0};
  CHECK_EQ(lvp_validate_set_state(&s), LVP_ERR_OUT_OF_RANGE);
  s.brightness = 101;
  CHECK_EQ(lvp_validate_set_state(&s), LVP_ERR_OUT_OF_RANGE);
  s.brightness = 1;
  CHECK_EQ(lvp_validate_set_state(&s), LVP_OK);
  s = {LVP_SET_COLOR | LVP_SET_RAW, 0, 0, 1, 2, 3, 0};
  CHECK_EQ(lvp_validate_set_state(&s), LVP_ERR_BAD_REQUEST);
  s = {LVP_SET_POWER, 2, 0, 0, 0, 0, 0};
  CHECK_EQ(lvp_validate_set_state(&s), LVP_ERR_OUT_OF_RANGE);
  s = {0x80, 0, 0, 0, 0, 0, 0};
  CHECK_EQ(lvp_validate_set_state(&s), LVP_ERR_BAD_REQUEST);

  // Encoder verweigert ungueltige Befehle
  lvp_msg_t m;
  lvp_init(&m, LVP_MSG_SET_STATE);
  m.u.set_state = {LVP_SET_BRIGHTNESS, 0, 0, 0, 0, 0, 0};
  uint8_t b[LVP_MAX_FRAME];
  lvp_err_t e = LVP_OK;
  CHECK_EQ(lvp_encode(&m, b, sizeof(b), &e), 0);
  CHECK_EQ(e, LVP_ERR_OUT_OF_RANGE);
}

static void test_seq_wrap() {
  CHECK(lvp_seq_after(2, 1));
  CHECK(!lvp_seq_after(1, 2));
  CHECK(lvp_seq_after(1, 65535)); // Ueberlauf
  CHECK(!lvp_seq_after(5, 5));
  CHECK_EQ(lvp_seq_next(65535), 1); // 0 wird uebersprungen
  CHECK_EQ(lvp_seq_next(1), 2);
}

static void test_fuzz_no_crash() {
  // Zufallsrahmen duerfen den Decoder nie zum Absturz bringen (Grenzpruefungen)
  uint32_t x = 12345;
  uint8_t b[LVP_MAX_FRAME + 10];
  lvp_msg_t d;
  int ok = 0;
  for (int it = 0; it < 20000; it++) {
    size_t n = 0;
    x = x * 1103515245u + 12345u;
    n = (x >> 8) % sizeof(b);
    for (size_t i = 0; i < n; i++) {
      x = x * 1103515245u + 12345u;
      b[i] = (uint8_t)(x >> 16);
    }
    if (n >= 2) {
      b[0] = 'L';
      b[1] = 'V';
    }
    if (lvp_decode(b, n, &d) == LVP_OK) {
      ok++;
    }
  }
  CHECK(ok < 20000);
}

void tests_protocol(void) {
  RUN(test_crc_reference);
  RUN(test_header_layout_little_endian);
  RUN(test_roundtrip_all_types);
  RUN(test_frame_size_limit);
  RUN(test_name_truncated_and_sanitized);
  RUN(test_decode_errors);
  RUN(test_set_state_validation);
  RUN(test_seq_wrap);
  RUN(test_fuzz_no_crash);
}

// ---- Zuverlaessigkeitsschicht ----

static void test_txq_retry_and_timeout() {
  lvl_txq_t q;
  lvl_txq_init(&q, 100, 3);
  uint8_t f[20] = {1, 2, 3};
  CHECK_EQ(lvl_txq_add(&q, ID_A, 7, LVP_MSG_SET_STATE, f, 3, 1000, 42), 0);
  int idx = -1;
  lvl_pending_t out;
  CHECK_EQ(lvl_txq_poll(&q, 1050, &idx, &out), LVL_NONE);
  CHECK_EQ(lvl_txq_poll(&q, 1100, &idx, &out), LVL_RESEND); // nach 100 ms
  CHECK_EQ(q.slot[idx].tries, 2);
  CHECK_EQ(lvl_txq_poll(&q, 1250, &idx, &out), LVL_NONE);    // jetzt 200 ms Wartezeit
  CHECK_EQ(lvl_txq_poll(&q, 1300, &idx, &out), LVL_RESEND);
  CHECK_EQ(lvl_txq_poll(&q, 1599, &idx, &out), LVL_NONE);
  CHECK_EQ(lvl_txq_poll(&q, 1600, &idx, &out), LVL_TIMEOUT); // drei Sendungen, dann Abbruch
  CHECK_EQ(out.tag, 42);
  CHECK_EQ(out.tries, 3);
  CHECK_EQ(lvl_txq_count(&q), 0);
}

static void test_txq_complete_and_cancel() {
  lvl_txq_t q;
  lvl_txq_init(&q, 100, 3);
  uint8_t f[4] = {0};
  lvl_txq_add(&q, ID_A, 1, LVP_MSG_GET_STATE, f, 4, 0, 1);
  lvl_txq_add(&q, ID_B, 1, LVP_MSG_GET_STATE, f, 4, 0, 2);
  lvl_txq_add(&q, ID_A, 2, LVP_MSG_GET_CALIB, f, 4, 0, 3);
  CHECK(lvl_txq_has(&q, ID_A, LVP_MSG_GET_CALIB));
  lvl_pending_t out;
  CHECK(!lvl_txq_complete(&q, ID_A, 9, &out)); // unbekannte Sequenz
  CHECK(lvl_txq_complete(&q, ID_B, 1, &out));  // Peer und Sequenz muessen passen
  CHECK_EQ(out.tag, 2);
  CHECK(!lvl_txq_complete(&q, ID_B, 1, &out)); // nur einmal
  const lvl_pending_t *open = lvl_txq_find_peer(&q, ID_A);
  CHECK(open != nullptr && open->type == LVP_MSG_GET_STATE);
  CHECK(lvl_txq_find_peer(&q, ID_B) == nullptr);
  CHECK_EQ(lvl_txq_cancel_peer(&q, ID_A), 2);
  CHECK(lvl_txq_find_peer(&q, ID_A) == nullptr);
  CHECK_EQ(lvl_txq_count(&q), 0);
  // volle Warteschlange
  for (int i = 0; i < LVL_MAX_PENDING; i++) {
    CHECK(lvl_txq_add(&q, ID_A, (uint16_t)(i + 1), 0, f, 4, 0, 0) >= 0);
  }
  CHECK_EQ(lvl_txq_add(&q, ID_A, 99, 0, f, 4, 0, 0), -1);
}

static void test_txq_time_wrap() {
  lvl_txq_t q;
  lvl_txq_init(&q, 100, 2);
  uint8_t f[1] = {0};
  lvl_txq_add(&q, ID_A, 1, 0, f, 1, 0xFFFFFFF0u, 0);
  int idx;
  CHECK_EQ(lvl_txq_poll(&q, 0x00000010u, &idx, nullptr), LVL_NONE); // 32 ms nach Ueberlauf
  CHECK_EQ(lvl_txq_poll(&q, 0x00000054u, &idx, nullptr), LVL_RESEND);
}

static void test_dedup() {
  lvl_dedup_t d;
  lvl_dedup_init(&d);
  const uint8_t *rsp;
  uint8_t rl;
  CHECK_EQ(lvl_dedup_check(&d, ID_A, 100, 5, &rsp, &rl), LVL_DEDUP_NEW);
  uint8_t answer[3] = {9, 8, 7};
  lvl_dedup_store(&d, ID_A, answer, 3);
  CHECK_EQ(lvl_dedup_check(&d, ID_A, 100, 5, &rsp, &rl), LVL_DEDUP_REPEAT);
  CHECK_EQ(rl, 3);
  CHECK_EQ(rsp[0], 9);
  CHECK_EQ(lvl_dedup_check(&d, ID_A, 100, 4, &rsp, &rl), LVL_DEDUP_STALE);
  CHECK_EQ(lvl_dedup_check(&d, ID_A, 100, 6, &rsp, &rl), LVL_DEDUP_NEW);
  // Neustart des Controllers: neue Sitzung, Sequenz beginnt von vorn
  CHECK_EQ(lvl_dedup_check(&d, ID_A, 200, 1, &rsp, &rl), LVL_DEDUP_NEW);
  // zweiter Peer ist unabhaengig
  CHECK_EQ(lvl_dedup_check(&d, ID_B, 100, 5, &rsp, &rl), LVL_DEDUP_NEW);
  lvl_dedup_forget(&d, ID_B);
  CHECK_EQ(lvl_dedup_check(&d, ID_B, 100, 5, &rsp, &rl), LVL_DEDUP_NEW);
}

static void test_dedup_eviction() {
  lvl_dedup_t d;
  lvl_dedup_init(&d);
  uint8_t id[6] = {1, 1, 1, 1, 1, 0};
  for (int i = 0; i < LVL_DEDUP_PEERS + 2; i++) {
    id[5] = (uint8_t)i;
    CHECK_EQ(lvl_dedup_check(&d, id, 1, 1, nullptr, nullptr), LVL_DEDUP_NEW);
  }
  // der aelteste Eintrag wurde verdraengt und gilt wieder als neu
  id[5] = 0;
  CHECK_EQ(lvl_dedup_check(&d, id, 1, 1, nullptr, nullptr), LVL_DEDUP_NEW);
}

static void test_online() {
  CHECK(!lvl_online(0, 1000, 500)); // nie gesehen
  CHECK(lvl_online(900, 1000, 500));
  CHECK(!lvl_online(400, 1000, 500));
  CHECK(lvl_online(0xFFFFFF00u, 0x10, 500)); // Ueberlauf
}

void tests_link(void) {
  RUN(test_txq_retry_and_timeout);
  RUN(test_txq_complete_and_cancel);
  RUN(test_txq_time_wrap);
  RUN(test_dedup);
  RUN(test_dedup_eviction);
  RUN(test_online);
}
