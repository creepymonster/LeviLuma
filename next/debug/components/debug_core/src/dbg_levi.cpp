#include "dbg_levi.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "dbg_text.h"

namespace dbg {

void FieldList::add(const char *name, const char *fmt, ...) {
  if (n >= MAX) {
    overflow = true;
    return;
  }
  Field &x = f[n++];
  snprintf(x.name, sizeof(x.name), "%s", name);
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(x.value, sizeof(x.value), fmt, ap);
  va_end(ap);
}

// ---- Beschriftungen ----

void type_label(uint8_t type, char *out, size_t cap) {
  const char *nm = lvp_type_name(type);

  if (nm[0] == '?') {
    snprintf(out, cap, "unbekannt (0x%02X)", type);
  } else {
    snprintf(out, cap, "%s (0x%02X)", nm, type);
  }
}

// Haengt "|NAME" an, wenn Bit gesetzt
static void bits_label(char *out, size_t cap, uint8_t v, uint8_t known_mask, const uint8_t *bits, const char *const *names, int n) {
  size_t pos = 0;
  out[0] = 0;

  for (int i = 0; i < n; i++) {
    if (v & bits[i]) {
      pos += snprintf(out + pos, pos < cap ? cap - pos : 0, "%s%s", pos ? "|" : "", names[i]);
    }
  }
  if (v & ~known_mask) {
    pos += snprintf(out + pos, pos < cap ? cap - pos : 0, "%sunbekannt 0x%02X", pos ? "|" : "", (unsigned)(v & ~known_mask));
  }
  if (pos == 0 && cap > 4) {
    snprintf(out, cap, "-");
  }
}

void flags_label(uint8_t flags, char *out, size_t cap) {
  static const uint8_t bits[] = {LVP_FLAG_RESPONSE, LVP_FLAG_NOTIFY};
  static const char *const names[] = {"RESPONSE", "NOTIFY"};
  char tmp[64];
  bits_label(tmp, sizeof(tmp), flags, LVP_FLAG_RESPONSE | LVP_FLAG_NOTIFY, bits, names, 2);
  snprintf(out, cap, "0x%02X [%s]", flags, tmp);
}

static void caps_label(uint8_t v, char *out, size_t cap) {
  static const uint8_t bits[] = {LVP_GW_KEY_REQUIRED, LVP_GW_PAIRING_OPEN, LVP_GW_BLE_CONNECTED, LVP_GW_PAIRED};
  static const char *const names[] = {"KEY_REQUIRED", "PAIRING_OPEN", "BLE_CONNECTED", "PAIRED"};
  char tmp[96];
  bits_label(tmp, sizeof(tmp), v, 0x0F, bits, names, 4);
  snprintf(out, cap, "0x%02X [%s]", v, tmp);
}

static void setmask_label(uint8_t v, char *out, size_t cap) {
  static const uint8_t bits[] = {LVP_SET_POWER, LVP_SET_BRIGHTNESS, LVP_SET_COLOR, LVP_SET_RAW, LVP_SET_MUSIC};
  static const char *const names[] = {"POWER", "BRIGHTNESS", "COLOR", "RAW", "MUSIC"};
  char tmp[96];
  bits_label(tmp, sizeof(tmp), v, LVP_SET_ALL, bits, names, 5);
  snprintf(out, cap, "0x%02X [%s]", v, tmp);
}

static void stflags_label(uint8_t v, char *out, size_t cap) {
  static const uint8_t bits[] = {LVP_STATE_MUSIC, LVP_STATE_RAW, LVP_STATE_BLE};
  static const char *const names[] = {"MUSIC", "RAW", "BLE"};
  char tmp[64];
  bits_label(tmp, sizeof(tmp), v, LVP_STATE_MUSIC | LVP_STATE_RAW | LVP_STATE_BLE, bits, names, 3);
  snprintf(out, cap, "0x%02X [%s]", v, tmp);
}

// ---- Felder je Typ ----

static void add_id(FieldList &fl, const char *name, const uint8_t id[LVP_ID_LEN]) {
  char s[18];
  lvp_id_to_str(id, s, sizeof(s));
  fl.add(name, "%s", s);
}

static void add_name(FieldList &fl, const char *name, const char *v) {
  fl.add(name, "\"%s\"", v);
}

static void add_rgb(FieldList &fl, const char *name, uint8_t r, uint8_t g, uint8_t b) {
  fl.add(name, "R=%u G=%u B=%u (#%02X%02X%02X)", r, g, b, r, g, b);
}

static void add_points(FieldList &fl, const uint8_t *p) {
  for (int i = 0; i < LVP_CAL_POINTS; i++) {
    char nm[24];
    snprintf(nm, sizeof(nm), "punkt[%d]", i);
    add_rgb(fl, nm, p[i * 3], p[i * 3 + 1], p[i * 3 + 2]);
  }
}

static const char *onoff(unsigned v) {
  return v == 0 ? "aus" : (v == 1 ? "an" : "ungueltig");
}

static void add_err(FieldList &fl, const char *name, uint8_t e) {
  fl.add(name, "%u = %s", e, lvp_err_name(e));
}

bool levi_type_supported(uint8_t type) {
  switch (type) {
  case LVP_MSG_DISCOVER:
  case LVP_MSG_ANNOUNCE:
  case LVP_MSG_PAIR_REQ:
  case LVP_MSG_PAIR_RSP:
  case LVP_MSG_UNPAIR:
  case LVP_MSG_GET_STATE:
  case LVP_MSG_SET_STATE:
  case LVP_MSG_STATE:
  case LVP_MSG_GET_CALIB:
  case LVP_MSG_SET_CALIB:
  case LVP_MSG_RESET_CALIB:
  case LVP_MSG_CALIB:
  case LVP_MSG_ACK:
  case LVP_MSG_NACK:
    return true;
  default:
    return false;
  }
}

static void fill_fields(const lvp_msg_t &m, FieldList &fl) {
  char t[128];

  switch (m.hdr.type) {
  case LVP_MSG_DISCOVER:
    add_id(fl, "controller_id", m.u.discover.controller_id);
    add_name(fl, "name", m.u.discover.name);
    break;
  case LVP_MSG_ANNOUNCE:
    add_id(fl, "gateway_id", m.u.announce.gateway_id);
    fl.add("fw_version", "%u.%u (0x%04X)", m.u.announce.fw_version >> 8, m.u.announce.fw_version & 0xFF, m.u.announce.fw_version);
    caps_label(m.u.announce.caps, t, sizeof(t));
    fl.add("caps", "%s", t);
    fl.add("state_revision", "%lu", (unsigned long)m.u.announce.state_revision);
    add_name(fl, "name", m.u.announce.name);
    break;
  case LVP_MSG_PAIR_REQ:
    add_id(fl, "controller_id", m.u.pair_req.controller_id);
    add_name(fl, "name", m.u.pair_req.name);
    break;
  case LVP_MSG_PAIR_RSP:
    add_err(fl, "result", m.u.pair_rsp.result);
    add_id(fl, "gateway_id", m.u.pair_rsp.gateway_id);
    add_name(fl, "name", m.u.pair_rsp.name);
    break;
  case LVP_MSG_UNPAIR:
    add_id(fl, "controller_id", m.u.unpair.controller_id);
    break;
  case LVP_MSG_SET_STATE: {
    const lvp_set_state_t &s = m.u.set_state;
    setmask_label(s.mask, t, sizeof(t));
    fl.add("mask", "%s", t);
    // Felder ausserhalb der Maske stehen im Rahmen, werden vom Gateway aber nicht ausgewertet
    fl.add("power", "%u = %s%s", s.power, onoff(s.power), (s.mask & LVP_SET_POWER) ? "" : " (nicht in Maske, ohne Wirkung)");
    fl.add("brightness", "%u %%%s", s.brightness, (s.mask & LVP_SET_BRIGHTNESS) ? "" : " (nicht in Maske, ohne Wirkung)");
    add_rgb(fl, "rgb", s.r, s.g, s.b);
    if (s.mask & LVP_SET_COLOR) {
      fl.add("rgb_modus", "COLOR = mit Kalibrierung");
    } else if (s.mask & LVP_SET_RAW) {
      fl.add("rgb_modus", "RAW = ohne Kalibrierung");
    } else {
      fl.add("rgb_modus", "kein Farbbefehl (rgb ohne Wirkung)");
    }
    fl.add("music", "%u = %s%s", s.music, onoff(s.music), (s.mask & LVP_SET_MUSIC) ? "" : " (nicht in Maske, ohne Wirkung)");
    break;
  }
  case LVP_MSG_STATE: {
    const lvp_state_t &s = m.u.state;
    fl.add("revision", "%lu", (unsigned long)s.revision);
    fl.add("sent_revision", "%lu%s", (unsigned long)s.sent_revision, s.sent_revision >= s.revision ? " (alles an das Band gesendet)" : " (noch nicht komplett gesendet)");
    fl.add("power", "%d = %s", s.power, s.power < 0 ? "unbekannt" : (s.power ? "an" : "aus"));
    fl.add("brightness", "%u %%", s.brightness);
    add_rgb(fl, "rgb", s.r, s.g, s.b);
    stflags_label(s.flags, t, sizeof(t));
    fl.add("flags", "%s", t);
    fl.add("calib_revision", "%u", s.calib_revision);
    break;
  }
  case LVP_MSG_CALIB:
    fl.add("custom", "%u = %s", m.u.calib.custom, m.u.calib.custom ? "gespeicherte Kalibrierung" : "Standard");
    fl.add("calib_revision", "%u", m.u.calib.calib_revision);
    add_points(fl, m.u.calib.points);
    break;
  case LVP_MSG_SET_CALIB:
    add_points(fl, m.u.set_calib.points);
    break;
  case LVP_MSG_NACK:
    add_err(fl, "error", m.u.nack.error);
    break;
  case LVP_MSG_GET_STATE:
  case LVP_MSG_GET_CALIB:
  case LVP_MSG_RESET_CALIB:
  case LVP_MSG_ACK:
    fl.add("nutzdaten", "keine (Typ ohne Nutzdaten)");
    break;
  default:
    // lvp_decode kennt den Typ, dieses Tool aber (noch) nicht: nichts erfinden
    fl.add("nutzdaten", "nicht dekodiert: Typ im Diagnosetool nicht umgesetzt (siehe Rohdaten)");
    break;
  }
}

static uint16_t rd16(const uint8_t *p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)rd16(p) | ((uint32_t)rd16(p + 2) << 16);
}

void decode_levi(const uint8_t *body, size_t len, bool complete, LeviInfo &o) {
  o = LeviInfo();

  if (!body || len < 2 || body[0] != LVP_MAGIC0 || body[1] != LVP_MAGIC1) {
    o.kind = LeviInfo::Kind::NotLevi;
    o.reason = "keine LeviLuma-Kennung (erste Bytes sind nicht 'LV')";
    return;
  }
  o.kind = LeviInfo::Kind::Invalid;

  if (len < LVP_HEADER_LEN) {
    o.err = LVP_ERR_SHORT;
    o.reason = complete ? "LeviLuma-Kennung vorhanden, aber weniger als der 16-Byte-Kopf" : "abgeschnitten: weniger als der 16-Byte-Kopf erfasst";
    return;
  }
  // Kopffelder roh lesen; bestaetigt sind sie erst nach bestandener CRC
  o.hdr_read = true;
  o.version = body[2];
  o.type = body[3];
  o.flags = body[4];
  o.len = body[5];
  o.seq = rd16(body + 6);
  o.ref_seq = rd16(body + 8);
  o.session = rd32(body + 10);
  o.crc_rx = rd16(body + 14);
  o.payload_avail = len - LVP_HEADER_LEN;

  if (!complete) {
    o.err = LVP_ERR_LENGTH;
    o.reason = "Erfassung unvollstaendig: Laenge, CRC und Nutzdaten nicht pruefbar";
    return;
  }
  if ((size_t)o.len != o.payload_avail) {
    o.err = LVP_ERR_LENGTH;
    o.reason = o.payload_avail < o.len ? "Nutzdaten kuerzer als im len-Feld angegeben (abgeschnitten oder beschaedigt), CRC nicht pruefbar"
                                       : "mehr Nutzdaten als im len-Feld angegeben (Restbytes), CRC nicht pruefbar";
    return;
  }
  // Vollstaendige Nutzdaten: CRC unabhaengig nachrechnen (wie lvp_decode_header: CRC-Feld als 0)
  static const uint8_t zero[2] = {0, 0};
  uint16_t crc = lvp_crc16(body, 14, 0xFFFF);
  crc = lvp_crc16(zero, 2, crc);
  crc = lvp_crc16(body + LVP_HEADER_LEN, o.len, crc);
  o.crc_checked = true;
  o.crc_calc = crc;
  o.crc_ok = crc == o.crc_rx;

  lvp_msg_t m;
  lvp_err_t e = lvp_decode(body, len, &m);
  o.err = e;

  switch (e) {
  case LVP_OK:
    o.kind = LeviInfo::Kind::Decoded;
    o.hdr_verified = true;
    o.reason = "gueltig";
    fill_fields(m, o.fields);
    break;
  case LVP_ERR_CRC:
    o.reason = "CRC-Pruefsumme stimmt nicht: Rahmen beschaedigt, Feldwerte unzuverlaessig";
    break;
  case LVP_ERR_VERSION:
    o.hdr_verified = true;
    o.reason = "Protokollversion wird vom gemeinsamen Protokoll nicht unterstuetzt; Nutzdaten nicht dekodiert";
    break;
  case LVP_ERR_UNSUPPORTED:
    o.kind = LeviInfo::Kind::Unsupported;
    o.hdr_verified = true;
    o.reason = "Nachrichtentyp unbekannt; Nutzdaten nicht dekodiert";
    break;
  case LVP_ERR_BAD_REQUEST:
  case LVP_ERR_OUT_OF_RANGE:
    o.hdr_verified = true;
    o.reason = "Kopf und CRC gueltig, aber Nutzdaten passen nicht zum Typ (Laenge oder Wertebereich)";
    break;
  case LVP_ERR_LENGTH:
    o.reason = "Rahmen laenger als das Protokoll erlaubt (hoechstens 250 Byte)";
    break;
  default:
    o.reason = "Dekodierung fehlgeschlagen";
    break;
  }
}

} // namespace dbg
