#pragma once
// Paketbau je Bandfamilie (ohne BLE-Aufrufe, hosttestbar). Der BleScheduler entscheidet, WAS gesendet wird
// (Reihenfolge, Ueberblendung, Drosselung), der BandDriver, WIE das Paket fuer die erkannte Familie aussieht.
// Umfang: nur die Funktionen, die das Gateway heute nutzt (Ein/Aus, Farbe, Helligkeit; Musik/Mikrofon-EQ nur ELK).
// Alle weiteren Befehle der Familien sind in hardware/led-band/ dokumentiert, aber nicht eingebaut.
// Stand: nur ELK ist am Geraet getestet. Triones, Zengge und BJ_LED sind aus Fremdquellen uebernommen und ungetestet.
#include <stddef.h>
#include <stdint.h>
#include "band_family.h"

struct BandCaps {
  bool nativeBrightness = false; // eigenes Helligkeitspaket; sonst steckt die Helligkeit im Farbpaket
  bool music = false;            // Musikmodus (Mikrofon) per Paket schaltbar
  bool micEq = false;            // Mikrofon-EQ-Modus
};

class BandDriver {
public:
  static constexpr size_t MAX_PACKET = 24;

  // Zengge: Farbpaket je nach Produkt-ID und BLE-Version
  enum class ZgColor : uint8_t {
    None,    // kein RGB-Geraet oder anderes Protokoll (IOTBT, CCT, Dimmer, Schalter): nicht unterstuetzt
    Rgb31,   // 0x31 mit RGB, Helligkeit in RGB eingerechnet (einfache Controller)
    Hsv3B,   // 0x3B A1 mit HSV + Helligkeit, Bytes 7-9 = RGB (Symphony, unbekannte Produkte)
    Hsv3BV2, // 0x3B A1 mit HSV + Helligkeit, Bytes 7-11 = 0 (colour_data_v2, Produkt 0x27 ab BLE v5)
  };

  BandDriver() = default; // ELK wie bisher
  explicit BandDriver(BandFamily f, const BandFam::ZenggeInfo &zi = BandFam::ZenggeInfo());

  BandFamily family() const {
    return family_;
  }
  // false: Familie erkannt, aber diese Variante kann das Gateway nicht steuern (Grund: variant())
  bool supported() const;
  // Kurzbeschreibung der Variante fuer das Log ("ELK", "Zengge 0x31 (Produkt 0x33, BLE v5)" ...)
  void describe(char *out, size_t cap) const;
  BandCaps caps() const;
  ZgColor zgColor() const {
    return zg_;
  }

  // Pakete. Rueckgabe: Laenge (0 = nicht unterstuetzt). out muss MAX_PACKET Bytes fassen.
  size_t power(bool on, uint8_t *out);
  // brightPct (0..100) wird nur benutzt, wenn die Familie kein eigenes Helligkeitspaket hat
  size_t color(uint8_t r, uint8_t g, uint8_t b, uint8_t brightPct, uint8_t *out);
  // value: Bandwert (ELK: 0..100 bzw. 0..255 je nach CONFIG_LEVILUMA_BRIGHTNESS_MAX)
  size_t brightness(uint8_t value, uint8_t *out);
  size_t music(bool on, uint8_t *out);
  size_t micEq(uint8_t mode, uint8_t *out);

  // Hilfen (oeffentlich fuer Tests)
  static uint8_t scale(uint8_t c, uint8_t pct) {
    return (uint8_t)((c * (pct > 100 ? 100 : pct) + 50) / 100);
  }
  static ZgColor zenggeColorFor(const BandFam::ZenggeInfo &zi);
  // Zengge-Transportschicht (Version 0, ein Segment, cmdId 0x0B = ohne Antwort). Rueckgabe: Gesamtlaenge.
  static size_t zenggeWrap(const uint8_t *cmd, size_t n, uint8_t seq, uint8_t *out);

private:
  size_t zengge(const uint8_t *cmd, size_t n, uint8_t *out);

  BandFamily family_ = BandFamily::Elk;
  BandFam::ZenggeInfo zi_;
  ZgColor zg_ = ZgColor::None;
  uint8_t seq_ = 0;
};
