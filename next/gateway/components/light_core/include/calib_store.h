#pragma once
// Kalibrierung des Gateways (Port von calibLoad/calibSave/calibReset aus legacy/src/state.cpp).
// 10 Zielfarben x R,G,B = 30 Werte 0..255, Reihenfolge wie LeviCal::TARGET. Persistenz ueber KvStore (NVS).
// Nicht threadsicher: der Aufrufer schuetzt die Instanz (Gateway: eigener Mutex, Leser holen eine Kopie).
#include <stdint.h>
#include "CalibCore.h"
#include "kv_store.h"

class CalibStore {
public:
  // Standard nach Reset: lineare Kanal-Gains (Legacy LEVI_CALIB_GR/GG/GB) oder feste Punkte (LEVI_CALIB_POINTS).
  struct Defaults {
    float gr = 1.000f, gg = 0.690f, gb = 0.940f; // FastLED TypicalSMD5050 (Legacy-Standard)
    bool hasPoints = false;
    uint8_t points[LeviCal::N * 3] = {0};
  };

  CalibStore(KvStore &kv, const Defaults &d) : kv_(kv), def_(d) {}

  // Laedt die gespeicherte Kalibrierung oder den Standard und berechnet die RBF-Gewichte.
  void load();
  // Speichert neue Punkte. false, wenn NVS nicht schreiben konnte (Modell bleibt dann unveraendert).
  bool save(const uint8_t pts[LeviCal::N * 3]);
  // Entfernt die gespeicherte Kalibrierung und setzt den Standard. false bei NVS-Fehler.
  bool reset();

  const LeviCal::Model &model() const {
    return model_;
  }
  bool custom() const {
    return custom_;
  }
  uint16_t revision() const {
    return revision_;
  }
  void points(uint8_t out[LeviCal::N * 3]) const;

  static constexpr const char *KEY_POINTS = "cal_pts";
  static constexpr const char *KEY_REV = "cal_rev";

private:
  void defaultPts(LeviCal::Model &m) const;
  void bumpRevision();

  KvStore &kv_;
  Defaults def_;
  LeviCal::Model model_{};
  bool custom_ = false;
  uint16_t revision_ = 0;
};
