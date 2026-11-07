#include "band_driver.h"
#include <stdio.h>
#include <initializer_list>
#include <string.h>
#include "LotusProtocol.h"

// Quellen der Pakete: hardware/led-band/ (je Familie eine Datei mit Herkunft und Stand).

namespace {
size_t put(uint8_t *out, std::initializer_list<uint8_t> b) {
  size_t n = 0;
  for (uint8_t v : b) {
    out[n++] = v;
  }
  return n;
}

uint8_t sum8(const uint8_t *d, size_t n) {
  unsigned s = 0;
  for (size_t i = 0; i < n; i++) {
    s += d[i];
  }
  return (uint8_t)(s & 0xFF);
}

// Wie Python colorsys.rgb_to_hsv mit int(h*360), int(s*100) (Vorlage lednetwf_ble protocol.rgb_to_hsv)
void hueSat(uint8_t r8, uint8_t g8, uint8_t b8, uint16_t &h, uint8_t &s) {
  // double wie Python, damit Grenzwerte (z. B. Blau = 240 Grad) gleich abgeschnitten werden
  double r = r8 / 255.0, g = g8 / 255.0, b = b8 / 255.0;
  double maxc = r > g ? (r > b ? r : b) : (g > b ? g : b);
  double minc = r < g ? (r < b ? r : b) : (g < b ? g : b);
  h = 0;
  s = 0;
  if (maxc == minc) {
    return;
  }
  double d = maxc - minc;
  s = (uint8_t)(d / maxc * 100.0);
  double rc = (maxc - r) / d, gc = (maxc - g) / d, bc = (maxc - b) / d;
  double hf;
  if (r == maxc) {
    hf = bc - gc;
  } else if (g == maxc) {
    hf = 2.0 + rc - bc;
  } else {
    hf = 4.0 + gc - rc;
  }
  hf /= 6.0;
  hf -= (double)(int)hf;
  if (hf < 0) {
    hf += 1.0;
  }
  int hi = (int)(hf * 360.0);
  h = (uint16_t)(hi >= 360 ? 359 : hi);
}

bool inList(uint16_t v, const uint16_t *list, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (list[i] == v) {
      return true;
    }
  }
  return false;
}
} // namespace

BandDriver::BandDriver(BandFamily f, const BandFam::ZenggeInfo &zi) : family_(f), zi_(zi) {
  if (f == BandFamily::Zengge) {
    zg_ = zenggeColorFor(zi);
  }
}

BandDriver::ZgColor BandDriver::zenggeColorFor(const BandFam::ZenggeInfo &zi) {
  if (!zi.valid) {
    return ZgColor::Hsv3B; // wie lednetwf_ble bei unbekanntem Produkt (Symphony-Annahme)
  }
  // IOTBT (Produkt 0x00), CCT, Dimmer, Schalter/Steckdosen, Deckenleuchten: andere Befehle bzw. kein RGB
  static const uint16_t unsupported[] = {0x00, 9, 22, 28, 82, 98, 23, 33, 65, 11, 147, 148, 149, 150, 151, 225, 226};
  // EffectType.SIMPLE in lednetwf_ble const.py: Farbe ueber 0x31
  static const uint16_t simple[] = {4, 6, 7, 8, 14, 16, 26, 27, 30, 32, 37, 38, 39, 41, 51, 53, 59, 60, 68, 72, 84, 91};
  if (inList(zi.productId, unsupported, sizeof(unsupported) / sizeof(unsupported[0]))) {
    return ZgColor::None;
  }
  if (zi.productId == 39 && zi.bleVersion >= 5) {
    return ZgColor::Hsv3BV2; // uses_color_v2
  }
  if (inList(zi.productId, simple, sizeof(simple) / sizeof(simple[0]))) {
    return ZgColor::Rgb31;
  }
  return ZgColor::Hsv3B;
}

bool BandDriver::supported() const {
  switch (family_) {
  case BandFamily::Elk:
  case BandFamily::Triones:
  case BandFamily::BjLed:
    return true;
  case BandFamily::Zengge:
    return zg_ != ZgColor::None;
  default:
    return false;
  }
}

void BandDriver::describe(char *out, size_t cap) const {
  if (!out || !cap) {
    return;
  }
  if (family_ != BandFamily::Zengge) {
    snprintf(out, cap, "%s", BandFam::name(family_));
    return;
  }
  const char *v = zg_ == ZgColor::Rgb31 ? "Farbe 0x31" : zg_ == ZgColor::Hsv3B ? "Farbe 0x3B" : zg_ == ZgColor::Hsv3BV2 ? "Farbe 0x3B v2" : "nicht unterstuetzt";
  if (zi_.valid) {
    snprintf(out, cap, "Zengge, Produkt 0x%02X, BLE v%u, %s", (unsigned)zi_.productId, (unsigned)zi_.bleVersion, v);
  } else {
    snprintf(out, cap, "Zengge, Produkt unbekannt, %s", v);
  }
}

BandCaps BandDriver::caps() const {
  BandCaps c;
  if (family_ == BandFamily::Elk) {
    c.nativeBrightness = true;
    c.music = true;
    c.micEq = true;
  }
  return c;
}

size_t BandDriver::zenggeWrap(const uint8_t *cmd, size_t n, uint8_t seq, uint8_t *out) {
  if (n + 8 > MAX_PACKET) {
    return 0;
  }
  out[0] = 0x00; // Version 0, nicht segmentiert
  out[1] = seq;
  out[2] = 0x80; // ein vollstaendiges Segment
  out[3] = 0x00;
  out[4] = (uint8_t)(n >> 8);
  out[5] = (uint8_t)n;
  out[6] = (uint8_t)(n + 1);
  out[7] = 0x0B; // ohne Antwort
  memcpy(out + 8, cmd, n);
  return n + 8;
}

size_t BandDriver::zengge(const uint8_t *cmd, size_t n, uint8_t *out) {
  return zenggeWrap(cmd, n, seq_++, out);
}

size_t BandDriver::power(bool on, uint8_t *out) {
  switch (family_) {
  case BandFamily::Elk: {
    uint8_t p[9];
    Lotus::power(on, p);
    memcpy(out, p, sizeof(p));
    return sizeof(p);
  }
  case BandFamily::Triones:
    return put(out, {0xCC, (uint8_t)(on ? 0x23 : 0x24), 0x33});
  case BandFamily::BjLed:
    return put(out, {0x69, 0x96, 0x02, 0x01, (uint8_t)(on ? 0x01 : 0x00)});
  case BandFamily::Zengge: {
    if (!supported()) {
      return 0;
    }
    // 0x3B wie lednetwf_ble fuer alle Nicht-IOTBT-Geraete; Bytes 7-9 = 0x000032 (0,5 s weiches Schalten)
    uint8_t c[13] = {0x3B, (uint8_t)(on ? 0x23 : 0x24), 0, 0, 0, 0, 0, 0x00, 0x00, 0x32, 0, 0, 0};
    c[12] = sum8(c, 12);
    return zengge(c, sizeof(c), out);
  }
  default:
    return 0;
  }
}

size_t BandDriver::color(uint8_t r, uint8_t g, uint8_t b, uint8_t brightPct, uint8_t *out) {
  switch (family_) {
  case BandFamily::Elk: {
    uint8_t p[9];
    Lotus::color(r, g, b, p);
    memcpy(out, p, sizeof(p));
    return sizeof(p);
  }
  case BandFamily::Triones:
    return put(out, {0x56, scale(r, brightPct), scale(g, brightPct), scale(b, brightPct), 0x00, 0xF0, 0xAA});
  case BandFamily::BjLed:
    return put(out, {0x69, 0x96, 0x05, 0x02, scale(r, brightPct), scale(g, brightPct), scale(b, brightPct)});
  case BandFamily::Zengge: {
    if (zg_ == ZgColor::Rgb31) {
      // 0x31 R G B WW CW Modus(F0 = nur RGB) Persist(0F = nicht speichern) Pruefsumme
      uint8_t c[9] = {0x31, scale(r, brightPct), scale(g, brightPct), scale(b, brightPct), 0x00, 0x00, 0xF0, 0x0F, 0};
      c[8] = sum8(c, 8);
      return zengge(c, sizeof(c), out);
    }
    if (zg_ == ZgColor::Hsv3B || zg_ == ZgColor::Hsv3BV2) {
      uint16_t h;
      uint8_t s;
      hueSat(r, g, b, h, s);
      uint16_t packed = (uint16_t)((h << 7) | s);
      // Helligkeit des Pakets: gewaehlte Helligkeit mal Hellwert der (kalibrierten) Farbe
      uint8_t maxc = r > g ? (r > b ? r : b) : (g > b ? g : b);
      unsigned pct = brightPct > 100 ? 100 : brightPct;
      unsigned bri = (pct * maxc + 127) / 255;
      if (bri == 0 && pct > 0 && maxc > 0) {
        bri = 1; // wie lednetwf_ble: sehr dunkel heisst nicht aus
      }
      bool v2 = zg_ == ZgColor::Hsv3BV2;
      uint8_t c[13] = {0x3B, 0xA1, (uint8_t)(packed >> 8), (uint8_t)packed, (uint8_t)bri, 0x00, 0x00,
                       (uint8_t)(v2 ? 0 : r), (uint8_t)(v2 ? 0 : g), (uint8_t)(v2 ? 0 : b), 0x00, 0x00, 0};
      c[12] = sum8(c, 12);
      return zengge(c, sizeof(c), out);
    }
    return 0;
  }
  default:
    return 0;
  }
}

size_t BandDriver::brightness(uint8_t value, uint8_t *out) {
  if (family_ != BandFamily::Elk) {
    return 0; // Helligkeit steckt im Farbpaket
  }
  uint8_t p[9];
  Lotus::brightness(value, p);
  memcpy(out, p, sizeof(p));
  return sizeof(p);
}

size_t BandDriver::music(bool on, uint8_t *out) {
  if (family_ != BandFamily::Elk) {
    return 0;
  }
  uint8_t p[9];
  Lotus::music(on, p);
  memcpy(out, p, sizeof(p));
  return sizeof(p);
}

size_t BandDriver::micEq(uint8_t mode, uint8_t *out) {
  if (family_ != BandFamily::Elk) {
    return 0;
  }
  uint8_t p[9];
  Lotus::micEq(mode, p);
  memcpy(out, p, sizeof(p));
  return sizeof(p);
}
