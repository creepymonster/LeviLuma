#pragma once
// Hosttestbare Logik des runden Controllers (keine ESP-IDF-Abhaengigkeit):
//  - Ein-Gateway-Regel auf dem gemeinsamen GatewayRegistry
//  - Farbrad: Punkt im Kreis <-> RGB (Farbton = Winkel, Saettigung = Abstand, volle Helligkeit)
//  - Sendedrossel fuer fortlaufende Touch-Eingaben (Farbrad, Helligkeitsbogen)
#include <stdint.h>
#include "gateway_registry.h"

namespace Round {

// ---- genau ein Gateway ----
// Index des gekoppelten Gateways oder -1. Sind (z. B. durch alte Daten) mehrere gekoppelt, gilt das erste.
int activeGateway(const GatewayRegistry &reg);
// Neues Koppeln nur, wenn noch keines gekoppelt ist.
bool mayPair(const GatewayRegistry &reg);

// ---- Farbrad ----
// Punkt (dx, dy) relativ zur Radmitte, Radius r (Pixel). Farbton 0 = Rot oben, im Uhrzeigersinn.
// Punkte ausserhalb werden auf den Rand gezogen. Ergebnis: RGB mit voller Helligkeit.
void wheelToRgb(float dx, float dy, float r, uint8_t rgb[3]);
// Position einer Farbe im Rad (Helligkeit wird ignoriert, Grau/Weiss -> Mitte).
void rgbToWheel(const uint8_t rgb[3], float r, float *dx, float *dy);

// ---- Sendedrossel ----
// Waehrend einer Touch-Bewegung hoechstens alle intervalMs einen Befehl; der letzte Wert wird beim
// Loslassen immer gesendet. Der ESP-NOW-Link fasst zusaetzlich zusammen (ein Befehl unterwegs).
class Throttle {
public:
  explicit Throttle(uint32_t intervalMs) : interval_(intervalMs) {}
  // Neuer Eingabewert; true = jetzt senden
  bool update(uint32_t nowMs);
  // Eingabe beendet; true = letzten Wert noch senden (wenn er nicht schon gesendet wurde)
  bool release(uint32_t nowMs);
  void reset() {
    dirty_ = false;
    sentOnce_ = false;
  }

private:
  uint32_t interval_;
  uint32_t last_ = 0;
  bool dirty_ = false, sentOnce_ = false;
};

} // namespace Round
