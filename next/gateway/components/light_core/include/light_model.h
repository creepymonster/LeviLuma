#pragma once
// Autoritativer LightState des Gateways (Nachfolger von State/Settern aus legacy/src/state.cpp).
// Reine Logik ohne Locks: der Aufrufer (gateway main) schuetzt die Instanz mit einem Mutex.
#include <stdint.h>
#include "leviluma_proto.h"

struct LightSnapshot {
  bool ble = false;
  bool music = false;
  bool raw = false;
  int8_t power = -1; // -1 unbekannt (nach dem Start, wie Legacy)
  uint8_t r = 139, g = 92, b = 246, brightness = 100;
  uint32_t revision = 0;     // jede angenommene Aenderung
  uint32_t colorRev = 0;     // nur Farbaenderungen (fuer die Ueberblendung)
  uint32_t sentRevision = 0; // bis hier an das Band gesendet (keine Ausfuehrungsbestaetigung)
  uint16_t calibRevision = 0;
};

class LightModel {
public:
  // Steuerbefehl anwenden. Pruefung wie lvp_validate_set_state. Eine Revision pro Befehl.
  // Reihenfolge: Ein/Aus, Helligkeit, Farbe (kalibriert oder roh), Musik.
  lvp_err_t apply(const lvp_set_state_t &cmd);

  void setBle(bool connected);
  void markSent(uint32_t rev);
  void setCalibRevision(uint16_t rev);

  const LightSnapshot &snapshot() const {
    return s_;
  }
  // Zustand als Protokollstruktur
  lvp_state_t toProto() const;

private:
  LightSnapshot s_;
};
