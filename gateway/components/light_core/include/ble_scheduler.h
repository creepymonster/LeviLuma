#pragma once
// Auswahl des naechsten BLE-Befehls an das Band. Portiert aus dem Sendeteil von bleTask()
// in legacy/src/ble.cpp, ohne BLE-Aufrufe, damit die Reihenfolge auf dem Host testbar ist:
//   Musik -> Mikrofon-EQ -> Helligkeit -> Ein/Aus -> Farbe (mit Ueberblendung, rohe Farben gedrosselt).
// Ein Befehl pro Zeitfenster, nie Helligkeit und Farbe im selben Schritt.
// Die Pakete baut der BandDriver der erkannten Familie (Standard: ELK wie bisher). Familien ohne eigenes
// Helligkeitspaket bekommen die Helligkeit im Farbpaket: Eine Helligkeitsaenderung sendet dann die Farbe neu.
// Musik und Mikrofon-EQ gibt es nur bei Familien, die sie koennen (caps); sonst werden sie uebergangen.
#include <stddef.h>
#include <stdint.h>
#include "CalibCore.h"
#include "ColorMath.h"
#include "band_driver.h"
#include "light_model.h"

class BleScheduler {
public:
  enum class Cmd : uint8_t { None, Music, MicEq, Brightness, Power, Color };

  struct Config {
    uint8_t brightnessMax = 100;   // LEVI_BRIGHTNESS_MAX: 100 = Werte 0..100, 255 = Prozent auf 0..255
    uint8_t micEqMode = 0;         // LEVI_MIC_EQ_MODE
    int fadeSteps = 1;             // LEVI_FADE_STEPS
    uint32_t rawMinGapMs = 400;    // rohe Kalibrierfarben nicht zu schnell senden (Legacy: 400 ms)
  };

  struct Step {
    Cmd cmd = Cmd::None;
    uint8_t packet[BandDriver::MAX_PACKET] = {0};
    size_t len = 0;
    int fadeStep = 0, fadeN = 0; // nur fuer Farbe (Log)
    bool raw = false;
    uint8_t r = 0, g = 0, b = 0; // nur fuer Farbe (Log): gesendete Farbe vor dem Einrechnen der Helligkeit
  };

  BleScheduler() = default;
  explicit BleScheduler(const Config &cfg) : cfg_(cfg) {}

  // Familie des verbundenen Bands (vor onConnected setzen). Ohne Aufruf: ELK.
  void setDriver(const BandDriver &d) {
    drv_ = d;
  }
  const BandDriver &driver() const {
    return drv_;
  }

  // Nach erfolgreicher Verbindung und Characteristic-Suche: Bandzustand unbekannt, alles neu senden.
  void onConnected(const LightSnapshot &s);

  // Naechsten Befehl bestimmen. Rueckgabe false: nichts zu senden.
  // Wie Legacy aendert die Farbauswahl bereits hier den Ueberblendungszustand.
  bool next(const LightSnapshot &s, const LeviCal::Model &cal, uint32_t nowMs, Step &out);

  // Ergebnis des Schreibens melden (ok = an den BLE-Stack uebergeben).
  void commit(const Step &st, bool ok, const LightSnapshot &s, uint32_t nowMs);

  // Nichts offen (kein Befehl, keine Ueberblendung): alles bis s.revision ist gesendet.
  bool fadeDone() const {
    return fadeLeft_ == 0;
  }

  uint8_t bandBrightness(uint8_t pct) const {
    return cfg_.brightnessMax == 255 ? (uint8_t)((pct * 255 + 50) / 100) : pct;
  }

private:
  Config cfg_;
  BandDriver drv_;
  uint32_t lastColorRev_ = 0, lastRawSend_ = 0;
  int8_t lastPower_ = -1;
  bool lastMusic_ = false;
  int lastBright_ = -1;
  bool eqPending_ = false;
  ColorMath::HSV shown_ = {0.f, 0.f, 1.f}, fadeFrom_ = shown_, fadeTo_ = shown_;
  int fadeLeft_ = 0, fadeN_ = 1;
  bool fadeRaw_ = false, instantColor_ = true;
};
