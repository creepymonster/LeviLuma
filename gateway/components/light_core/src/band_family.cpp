#include "band_family.h"
#include <string.h>

namespace BandFam {

const char *name(BandFamily f) {
  switch (f) {
  case BandFamily::Elk:
    return "ELK";
  case BandFamily::Triones:
    return "Triones";
  case BandFamily::Zengge:
    return "Zengge";
  case BandFamily::BjLed:
    return "BJ_LED";
  default:
    return "unbekannt";
  }
}

namespace {
struct NamePrefix {
  const char *prefix;
  BandFamily family;
};

// Reihenfolge egal: keine Praefixe ueberschneiden sich. Quellen: hardware/led-band/.
// "LEDBLE" bleibt bei ELK (wie Legacy bleNameMatches()); dieselben Namen nutzen auch Triones-Geraete,
// das klaert nach dem Verbinden matchGatt().
// Nicht aufgenommen: "IOTBT" (Zengge-Variante mit anderem Protokoll, siehe hardware/led-band/zengge.md).
const NamePrefix kNames[] = {
    {"ELK-BLEDDM", BandFamily::Elk}, {"ELK-BLEDOM", BandFamily::Elk},  {"ELK-BLEDOB", BandFamily::Elk},
    {"MELK", BandFamily::Elk},       {"LEDBLE", BandFamily::Elk},      {"Triones", BandFamily::Triones},
    {"LEDBlue", BandFamily::Triones}, {"Dream~", BandFamily::Triones}, {"QHM-", BandFamily::Triones},
    {"LEDnetWF", BandFamily::Zengge}, {"IOTWF", BandFamily::Zengge},   {"BJ_LED", BandFamily::BjLed},
};

const Profile kProfiles[] = {
    {BandFamily::Elk, 0xFFF0, 0xFFF3, true},
    {BandFamily::Triones, 0xFFD5, 0xFFD9, false},
    {BandFamily::Triones, 0xFFE5, 0xFFE9, false},
    {BandFamily::Zengge, 0xFFFF, 0xFF01, false},
    {BandFamily::BjLed, 0x0000, 0xEE01, false},
};
constexpr int kProfileN = sizeof(kProfiles) / sizeof(kProfiles[0]);

bool fits(const Profile &p, const GattChr &c) {
  if (c.chr != p.chr || (p.svc != 0 && c.svc != p.svc)) {
    return false;
  }
  if (p.needWriteNoRsp) {
    return (c.props & PROP_WRITE_NO_RSP) != 0;
  }
  return (c.props & (PROP_WRITE | PROP_WRITE_NO_RSP)) != 0;
}

int hexVal(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}
} // namespace

BandFamily fromName(const char *n) {
  if (!n || !n[0]) {
    return BandFamily::Unknown;
  }
  for (const NamePrefix &p : kNames) {
    if (strncmp(n, p.prefix, strlen(p.prefix)) == 0) {
      return p.family;
    }
  }
  return BandFamily::Unknown;
}

const Profile *profiles(int *count) {
  if (count) {
    *count = kProfileN;
  }
  return kProfiles;
}

bool matchGatt(BandFamily hint, const GattChr *list, int n, Match &out) {
  out = Match();
  if (!list || n <= 0) {
    return false;
  }
  // Zwei Durchgaenge: zuerst die Profile der Hinweisfamilie, dann alle anderen.
  for (int pass = 0; pass < 2; pass++) {
    for (const Profile &p : kProfiles) {
      bool hinted = p.family == hint;
      if ((pass == 0) != hinted) {
        continue;
      }
      for (int i = 0; i < n; i++) {
        if (fits(p, list[i])) {
          out.family = p.family;
          out.profile = &p;
          out.index = i;
          return true;
        }
      }
    }
  }
  return false;
}

bool parseZenggeMfg(const uint8_t *d, size_t len, ZenggeInfo &out) {
  out = ZenggeInfo();
  // Company-ID (2) + sta + Version + MAC (6) + Produkt-ID (2) = 12 Bytes
  if (!d || len < 12 || d[1] != 0x5A) {
    return false;
  }
  const uint8_t *p = d + 2;
  out.bleVersion = p[1];
  out.productId = (uint16_t)((p[8] << 8) | p[9]);
  out.valid = true;
  return true;
}

bool parseZenggeName(const char *n, ZenggeInfo &out) {
  out = ZenggeInfo();
  if (!n || strlen(n) != 20 || strncmp(n, "LEDnetWF", 8) != 0) {
    return false;
  }
  int v[6];
  for (int i = 0; i < 6; i++) {
    v[i] = hexVal(n[8 + i]);
    if (v[i] < 0) {
      return false;
    }
  }
  // Version dezimal ("07" -> 7), wie lednetwf_ble; Produkt-ID hexadezimal
  if (v[0] > 9 || v[1] > 9) {
    return false;
  }
  out.bleVersion = (uint8_t)(v[0] * 10 + v[1]);
  out.productId = (uint16_t)((v[2] << 12) | (v[3] << 8) | (v[4] << 4) | v[5]);
  out.valid = true;
  return true;
}

} // namespace BandFam
