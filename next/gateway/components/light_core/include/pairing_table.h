#pragma once
// Gekoppelte Controller des Gateways (persistent) und Kopplungsfenster.
// Regeln:
//  - Mit individuellem Schluessel (LMK): Kopplung jederzeit, der Schluessel ist der Nachweis
//    (ohne passenden Schluessel kann ESP-NOW den Rahmen nicht entschluesseln).
//  - Ohne Schluessel: Kopplung nur im Kopplungsfenster (nach dem Start und nach Druck auf die BOOT-Taste).
#include <stddef.h>
#include <stdint.h>
#include "kv_store.h"
#include "leviluma_proto.h"

class PairingTable {
public:
  static constexpr int MAX = 4; // ESP-NOW erlaubt nur wenige verschluesselte Peers
  static constexpr const char *KEY = "paired";

  struct Entry {
    uint8_t mac[LVP_ID_LEN];
    char name[LVP_NAME_MAX + 1];
  };

  explicit PairingTable(KvStore &kv) : kv_(kv) {}

  void load();
  bool contains(const uint8_t mac[LVP_ID_LEN]) const;
  // Fuegt hinzu oder aktualisiert den Namen. Gibt LVP_ERR_FULL oder LVP_ERR_STORAGE zurueck.
  lvp_err_t add(const uint8_t mac[LVP_ID_LEN], const char *name);
  bool remove(const uint8_t mac[LVP_ID_LEN]);
  bool clear();
  int count() const {
    return n_;
  }
  const Entry &at(int i) const {
    return e_[i];
  }

  // Kopplungsfenster (nur relevant ohne Schluessel)
  void openWindow(uint32_t nowMs, uint32_t durationMs) {
    windowUntil_ = nowMs + durationMs;
    windowOpen_ = durationMs > 0;
  }
  bool windowOpen(uint32_t nowMs) {
    if (windowOpen_ && (int32_t)(nowMs - windowUntil_) >= 0) {
      windowOpen_ = false;
    }
    return windowOpen_;
  }
  // Darf dieser Controller koppeln?
  lvp_err_t mayPair(bool keyConfigured, uint32_t nowMs) {
    return (keyConfigured || windowOpen(nowMs)) ? LVP_OK : LVP_ERR_PAIRING_CLOSED;
  }

  // Serialisierung (Format: Version 1, Anzahl, je Eintrag MAC[6] + Namenslaenge + Name)
  static size_t serialize(const Entry *e, int n, uint8_t *buf, size_t cap);
  static int deserialize(const uint8_t *buf, size_t len, Entry *out, int max);

private:
  bool persist();
  KvStore &kv_;
  Entry e_[MAX] = {};
  int n_ = 0;
  uint32_t windowUntil_ = 0;
  bool windowOpen_ = false;
};
