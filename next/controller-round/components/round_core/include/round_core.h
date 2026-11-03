#pragma once
// Hosttestbare Logik des runden Controllers (keine ESP-IDF-Abhaengigkeit):
//  - Ein-Gateway-Regel auf dem gemeinsamen GatewayRegistry
//  - Sendedrossel fuer fortlaufende Touch-Eingaben (Helligkeitsregler)
#include <stdint.h>
#include "gateway_registry.h"

namespace Round {

// ---- genau ein Gateway ----
// Index des gekoppelten Gateways oder -1. Sind (z. B. durch alte Daten) mehrere gekoppelt, gilt das erste.
int activeGateway(const GatewayRegistry &reg);
// Neues Koppeln nur, wenn noch keines gekoppelt ist.
bool mayPair(const GatewayRegistry &reg);

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
