// LeviLuma ESP-NOW-Protokoll: Kodieren und Dekodieren mit Laengen- und Bereichspruefung.
#include "leviluma_proto.h"
#include <stdio.h>
#include <string.h>

// ---- Schreiber/Leser mit Grenzpruefung ----

typedef struct {
  uint8_t *p;
  size_t cap, pos;
  bool ok;
} wr_t;

typedef struct {
  const uint8_t *p;
  size_t len, pos;
  bool ok;
} rd_t;

static void w_u8(wr_t *w, uint8_t v) {
  if (!w->ok || w->pos + 1 > w->cap) {
    w->ok = false;
    return;
  }
  w->p[w->pos++] = v;
}

static void w_u16(wr_t *w, uint16_t v) {
  w_u8(w, (uint8_t)(v & 0xFF));
  w_u8(w, (uint8_t)(v >> 8));
}

static void w_u32(wr_t *w, uint32_t v) {
  w_u16(w, (uint16_t)(v & 0xFFFF));
  w_u16(w, (uint16_t)(v >> 16));
}

static void w_bytes(wr_t *w, const uint8_t *b, size_t n) {
  if (!w->ok || w->pos + n > w->cap) {
    w->ok = false;
    return;
  }
  memcpy(w->p + w->pos, b, n);
  w->pos += n;
}

// Name: 1 Byte Laenge + Bytes (ohne Nullterminator), gekuerzt auf LVP_NAME_MAX.
static size_t name_len(const char *s) {
  size_t n = 0;
  while (s && n < LVP_NAME_MAX && s[n]) {
    n++;
  }
  return n;
}

static void w_name(wr_t *w, const char *s) {
  size_t n = name_len(s);
  w_u8(w, (uint8_t)n);
  w_bytes(w, (const uint8_t *)s, n);
}

static uint8_t r_u8(rd_t *r) {
  if (!r->ok || r->pos + 1 > r->len) {
    r->ok = false;
    return 0;
  }
  return r->p[r->pos++];
}

static uint16_t r_u16(rd_t *r) {
  uint16_t lo = r_u8(r);
  uint16_t hi = r_u8(r);
  return (uint16_t)(lo | (hi << 8));
}

static uint32_t r_u32(rd_t *r) {
  uint32_t lo = r_u16(r);
  uint32_t hi = r_u16(r);
  return lo | (hi << 16);
}

static void r_bytes(rd_t *r, uint8_t *out, size_t n) {
  if (!r->ok || r->pos + n > r->len) {
    r->ok = false;
    memset(out, 0, n);
    return;
  }
  memcpy(out, r->p + r->pos, n);
  r->pos += n;
}

static void r_name(rd_t *r, char out[LVP_NAME_MAX + 1]) {
  uint8_t n = r_u8(r);
  if (n > LVP_NAME_MAX) {
    r->ok = false;
    out[0] = 0;
    return;
  }
  r_bytes(r, (uint8_t *)out, n);
  out[r->ok ? n : 0] = 0;
  // Steuerzeichen ersetzen, damit Anzeige und Logs sauber bleiben
  for (size_t i = 0; out[i]; i++) {
    if ((uint8_t)out[i] < 0x20 || out[i] == 0x7F) {
      out[i] = '?';
    }
  }
}

// ---- CRC-16/CCITT-FALSE (Polynom 0x1021, Start 0xFFFF) ----

uint16_t lvp_crc16(const uint8_t *data, size_t len, uint16_t crc) {
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

void lvp_init(lvp_msg_t *m, lvp_msg_type_t type) {
  memset(m, 0, sizeof(*m));
  m->hdr.magic0 = LVP_MAGIC0;
  m->hdr.magic1 = LVP_MAGIC1;
  m->hdr.version = LVP_VERSION;
  m->hdr.type = (uint8_t)type;
}

lvp_err_t lvp_validate_set_state(const lvp_set_state_t *s) {
  if (!s || s->mask == 0 || (s->mask & ~LVP_SET_ALL)) {
    return LVP_ERR_BAD_REQUEST;
  }
  if ((s->mask & LVP_SET_COLOR) && (s->mask & LVP_SET_RAW)) {
    return LVP_ERR_BAD_REQUEST;
  }
  if ((s->mask & LVP_SET_POWER) && s->power > 1) {
    return LVP_ERR_OUT_OF_RANGE;
  }
  if ((s->mask & LVP_SET_MUSIC) && s->music > 1) {
    return LVP_ERR_OUT_OF_RANGE;
  }
  if ((s->mask & LVP_SET_BRIGHTNESS) && (s->brightness < LVP_BRIGHTNESS_MIN || s->brightness > LVP_BRIGHTNESS_MAX)) {
    return LVP_ERR_OUT_OF_RANGE;
  }
  return LVP_OK;
}

// Nutzdaten je Typ schreiben
static void encode_payload(const lvp_msg_t *m, wr_t *w) {
  switch (m->hdr.type) {
  case LVP_MSG_DISCOVER:
    w_bytes(w, m->u.discover.controller_id, LVP_ID_LEN);
    w_name(w, m->u.discover.name);
    break;
  case LVP_MSG_ANNOUNCE:
    w_bytes(w, m->u.announce.gateway_id, LVP_ID_LEN);
    w_u16(w, m->u.announce.fw_version);
    w_u8(w, m->u.announce.caps);
    w_u32(w, m->u.announce.state_revision);
    w_name(w, m->u.announce.name);
    break;
  case LVP_MSG_PAIR_REQ:
    w_bytes(w, m->u.pair_req.controller_id, LVP_ID_LEN);
    w_name(w, m->u.pair_req.name);
    break;
  case LVP_MSG_PAIR_RSP:
    w_u8(w, m->u.pair_rsp.result);
    w_bytes(w, m->u.pair_rsp.gateway_id, LVP_ID_LEN);
    w_name(w, m->u.pair_rsp.name);
    break;
  case LVP_MSG_UNPAIR:
    w_bytes(w, m->u.unpair.controller_id, LVP_ID_LEN);
    break;
  case LVP_MSG_SET_STATE:
    w_u8(w, m->u.set_state.mask);
    w_u8(w, m->u.set_state.power);
    w_u8(w, m->u.set_state.brightness);
    w_u8(w, m->u.set_state.r);
    w_u8(w, m->u.set_state.g);
    w_u8(w, m->u.set_state.b);
    w_u8(w, m->u.set_state.music);
    break;
  case LVP_MSG_STATE:
    w_u32(w, m->u.state.revision);
    w_u32(w, m->u.state.sent_revision);
    w_u8(w, (uint8_t)m->u.state.power);
    w_u8(w, m->u.state.brightness);
    w_u8(w, m->u.state.r);
    w_u8(w, m->u.state.g);
    w_u8(w, m->u.state.b);
    w_u8(w, m->u.state.flags);
    w_u16(w, m->u.state.calib_revision);
    break;
  case LVP_MSG_CALIB:
    w_u8(w, m->u.calib.custom);
    w_u16(w, m->u.calib.calib_revision);
    w_bytes(w, m->u.calib.points, LVP_CAL_BYTES);
    break;
  case LVP_MSG_SET_CALIB:
    w_bytes(w, m->u.set_calib.points, LVP_CAL_BYTES);
    break;
  case LVP_MSG_NACK:
    w_u8(w, m->u.nack.error);
    break;
  case LVP_MSG_GET_STATE:
  case LVP_MSG_GET_CALIB:
  case LVP_MSG_RESET_CALIB:
  case LVP_MSG_ACK:
    break; // ohne Nutzdaten
  default:
    w->ok = false;
    break;
  }
}

size_t lvp_encode(const lvp_msg_t *m, uint8_t *buf, size_t cap, lvp_err_t *err) {
  lvp_err_t e = LVP_OK;

  if (!m || !buf) {
    e = LVP_ERR_BAD_REQUEST;
  } else if (cap < LVP_HEADER_LEN) {
    e = LVP_ERR_TOO_BIG;
  }

  if (e == LVP_OK && m->hdr.type == LVP_MSG_SET_STATE) {
    e = lvp_validate_set_state(&m->u.set_state);
  }

  if (e != LVP_OK) {
    if (err) {
      *err = e;
    }
    return 0;
  }
  size_t limit = cap < LVP_MAX_FRAME ? cap : LVP_MAX_FRAME;
  wr_t w = {buf, limit, LVP_HEADER_LEN, true};
  encode_payload(m, &w);

  if (!w.ok) {
    if (err) {
      bool known = lvp_type_name(m->hdr.type)[0] != '?';
      *err = known ? LVP_ERR_TOO_BIG : LVP_ERR_UNSUPPORTED;
    }
    return 0;
  }
  size_t payload = w.pos - LVP_HEADER_LEN;
  wr_t h = {buf, LVP_HEADER_LEN, 0, true};
  w_u8(&h, LVP_MAGIC0);
  w_u8(&h, LVP_MAGIC1);
  w_u8(&h, LVP_VERSION);
  w_u8(&h, m->hdr.type);
  w_u8(&h, m->hdr.flags);
  w_u8(&h, (uint8_t)payload);
  w_u16(&h, m->hdr.seq);
  w_u16(&h, m->hdr.ref_seq);
  w_u32(&h, m->hdr.session);
  w_u16(&h, 0);
  uint16_t crc = lvp_crc16(buf, w.pos, 0xFFFF);
  buf[14] = (uint8_t)(crc & 0xFF);
  buf[15] = (uint8_t)(crc >> 8);

  if (err) {
    *err = LVP_OK;
  }
  return w.pos;
}

lvp_err_t lvp_decode_header(const uint8_t *buf, size_t len, lvp_header_t *h) {
  if (!buf || len < LVP_HEADER_LEN) {
    return LVP_ERR_SHORT;
  }
  if (len > LVP_MAX_FRAME) {
    return LVP_ERR_LENGTH;
  }
  rd_t r = {buf, LVP_HEADER_LEN, 0, true};
  h->magic0 = r_u8(&r);
  h->magic1 = r_u8(&r);
  h->version = r_u8(&r);
  h->type = r_u8(&r);
  h->flags = r_u8(&r);
  h->len = r_u8(&r);
  h->seq = r_u16(&r);
  h->ref_seq = r_u16(&r);
  h->session = r_u32(&r);
  h->crc = r_u16(&r);

  if (h->magic0 != LVP_MAGIC0 || h->magic1 != LVP_MAGIC1) {
    return LVP_ERR_MAGIC;
  }
  if ((size_t)h->len + LVP_HEADER_LEN != len) {
    return LVP_ERR_LENGTH;
  }
  // CRC ueber den Rahmen mit genulltem CRC-Feld
  uint16_t crc = lvp_crc16(buf, 14, 0xFFFF);
  const uint8_t zero[2] = {0, 0};
  crc = lvp_crc16(zero, 2, crc);
  crc = lvp_crc16(buf + LVP_HEADER_LEN, h->len, crc);

  if (crc != h->crc) {
    return LVP_ERR_CRC;
  }
  if (h->version != LVP_VERSION) {
    return LVP_ERR_VERSION;
  }
  return LVP_OK;
}

lvp_err_t lvp_decode(const uint8_t *buf, size_t len, lvp_msg_t *m) {
  memset(m, 0, sizeof(*m));
  lvp_err_t e = lvp_decode_header(buf, len, &m->hdr);

  if (e != LVP_OK) {
    return e;
  }
  rd_t r = {buf + LVP_HEADER_LEN, m->hdr.len, 0, true};

  switch (m->hdr.type) {
  case LVP_MSG_DISCOVER:
    r_bytes(&r, m->u.discover.controller_id, LVP_ID_LEN);
    r_name(&r, m->u.discover.name);
    break;
  case LVP_MSG_ANNOUNCE:
    r_bytes(&r, m->u.announce.gateway_id, LVP_ID_LEN);
    m->u.announce.fw_version = r_u16(&r);
    m->u.announce.caps = r_u8(&r);
    m->u.announce.state_revision = r_u32(&r);
    r_name(&r, m->u.announce.name);
    break;
  case LVP_MSG_PAIR_REQ:
    r_bytes(&r, m->u.pair_req.controller_id, LVP_ID_LEN);
    r_name(&r, m->u.pair_req.name);
    break;
  case LVP_MSG_PAIR_RSP:
    m->u.pair_rsp.result = r_u8(&r);
    r_bytes(&r, m->u.pair_rsp.gateway_id, LVP_ID_LEN);
    r_name(&r, m->u.pair_rsp.name);
    break;
  case LVP_MSG_UNPAIR:
    r_bytes(&r, m->u.unpair.controller_id, LVP_ID_LEN);
    break;
  case LVP_MSG_SET_STATE:
    m->u.set_state.mask = r_u8(&r);
    m->u.set_state.power = r_u8(&r);
    m->u.set_state.brightness = r_u8(&r);
    m->u.set_state.r = r_u8(&r);
    m->u.set_state.g = r_u8(&r);
    m->u.set_state.b = r_u8(&r);
    m->u.set_state.music = r_u8(&r);
    break;
  case LVP_MSG_STATE:
    m->u.state.revision = r_u32(&r);
    m->u.state.sent_revision = r_u32(&r);
    m->u.state.power = (int8_t)r_u8(&r);
    m->u.state.brightness = r_u8(&r);
    m->u.state.r = r_u8(&r);
    m->u.state.g = r_u8(&r);
    m->u.state.b = r_u8(&r);
    m->u.state.flags = r_u8(&r);
    m->u.state.calib_revision = r_u16(&r);
    if (r.ok && (m->u.state.power < -1 || m->u.state.power > 1)) {
      return LVP_ERR_OUT_OF_RANGE;
    }
    break;
  case LVP_MSG_CALIB:
    m->u.calib.custom = r_u8(&r);
    m->u.calib.calib_revision = r_u16(&r);
    r_bytes(&r, m->u.calib.points, LVP_CAL_BYTES);
    break;
  case LVP_MSG_SET_CALIB:
    r_bytes(&r, m->u.set_calib.points, LVP_CAL_BYTES);
    break;
  case LVP_MSG_NACK:
    m->u.nack.error = r_u8(&r);
    break;
  case LVP_MSG_GET_STATE:
  case LVP_MSG_GET_CALIB:
  case LVP_MSG_RESET_CALIB:
  case LVP_MSG_ACK:
    break;
  default:
    return LVP_ERR_UNSUPPORTED;
  }

  // Nutzdaten muessen exakt aufgebraucht sein (keine Restbytes, nichts fehlt)
  if (!r.ok || r.pos != r.len) {
    return LVP_ERR_BAD_REQUEST;
  }
  if (m->hdr.type == LVP_MSG_SET_STATE) {
    return lvp_validate_set_state(&m->u.set_state);
  }
  return LVP_OK;
}

const char *lvp_type_name(uint8_t type) {
  switch (type) {
  case LVP_MSG_DISCOVER:
    return "DISCOVER";
  case LVP_MSG_ANNOUNCE:
    return "ANNOUNCE";
  case LVP_MSG_PAIR_REQ:
    return "PAIR_REQ";
  case LVP_MSG_PAIR_RSP:
    return "PAIR_RSP";
  case LVP_MSG_UNPAIR:
    return "UNPAIR";
  case LVP_MSG_GET_STATE:
    return "GET_STATE";
  case LVP_MSG_SET_STATE:
    return "SET_STATE";
  case LVP_MSG_STATE:
    return "STATE";
  case LVP_MSG_GET_CALIB:
    return "GET_CALIB";
  case LVP_MSG_SET_CALIB:
    return "SET_CALIB";
  case LVP_MSG_RESET_CALIB:
    return "RESET_CALIB";
  case LVP_MSG_CALIB:
    return "CALIB";
  case LVP_MSG_ACK:
    return "ACK";
  case LVP_MSG_NACK:
    return "NACK";
  default:
    return "?";
  }
}

const char *lvp_err_name(uint8_t err) {
  switch (err) {
  case LVP_OK:
    return "OK";
  case LVP_ERR_NOT_PAIRED:
    return "nicht gekoppelt";
  case LVP_ERR_BAD_REQUEST:
    return "ungueltige Anfrage";
  case LVP_ERR_UNSUPPORTED:
    return "nicht unterstuetzt";
  case LVP_ERR_BUSY:
    return "beschaeftigt";
  case LVP_ERR_STORAGE:
    return "Speicherfehler";
  case LVP_ERR_PAIRING_CLOSED:
    return "Kopplung nicht freigegeben";
  case LVP_ERR_VERSION:
    return "Protokollversion";
  case LVP_ERR_OUT_OF_RANGE:
    return "Wert ausserhalb";
  case LVP_ERR_FULL:
    return "kein Platz";
  case LVP_ERR_SHORT:
    return "zu kurz";
  case LVP_ERR_MAGIC:
    return "falsche Kennung";
  case LVP_ERR_CRC:
    return "CRC";
  case LVP_ERR_LENGTH:
    return "Laenge";
  case LVP_ERR_TOO_BIG:
    return "zu gross";
  default:
    return "?";
  }
}

void lvp_id_to_str(const uint8_t id[LVP_ID_LEN], char *out, size_t cap) {
  snprintf(out, cap, "%02X:%02X:%02X:%02X:%02X:%02X", id[0], id[1], id[2], id[3], id[4], id[5]);
}

bool lvp_id_equal(const uint8_t a[LVP_ID_LEN], const uint8_t b[LVP_ID_LEN]) {
  return memcmp(a, b, LVP_ID_LEN) == 0;
}
