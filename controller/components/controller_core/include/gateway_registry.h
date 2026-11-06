#pragma once
// Gateway-Verwaltung des Controllers: bekannte (gekoppelte) und unbekannte (nur erkannte) Gateways.
// Persistent sind nur gekoppelte Gateways: ID, Anzeigename, optionaler Schluessel, Zigbee-Endpoint.
// Der LightState ist eine synchronisierte Kopie; autoritativ ist das Gateway.
#include <stddef.h>
#include <stdint.h>
#include "kv_store.h"
#include "levi_key.h"
#include "leviluma_proto.h"

enum class PairPhase : uint8_t { None, Pending, Failed };

struct GatewayEntry {
  bool used = false;
  uint8_t id[LVP_ID_LEN] = {0}; // = ESP-NOW-Adresse des Gateways

  // persistent (nur wenn paired)
  bool paired = false;
  char alias[LVP_NAME_MAX + 1] = {0}; // eigener Anzeigename, leer = gemeldeter Name
  bool hasKey = false;
  uint8_t key[LEVI_KEY_LEN] = {0};
  uint8_t zbEndpoint = 0; // 0 = keine Zigbee-Zuordnung

  // Laufzeit
  char announcedName[LVP_NAME_MAX + 1] = {0};
  uint8_t caps = 0;
  uint16_t fwVersion = 0;
  uint32_t lastSeenMs = 0;
  uint32_t gwSession = 0;
  bool sessionKnown = false;
  uint32_t announcedRevision = 0;
  bool stateValid = false;
  lvp_state_t state = {};
  bool calibValid = false;
  lvp_calib_t calib = {};
  PairPhase pairPhase = PairPhase::None;
  uint8_t lastError = 0; // lvp_err_t der letzten Ablehnung, 0 = keiner
  bool lastTimeout = false;
  bool syncPending = false;

  const char *displayName() const {
    return alias[0] ? alias : (announcedName[0] ? announcedName : "Gateway");
  }
};

class GatewayRegistry {
public:
  static constexpr int MAX_PAIRED = 8;
  static constexpr int MAX_ENTRIES = 16;
  static constexpr const char *KEY = "gw_reg";

  explicit GatewayRegistry(KvStore &kv) : kv_(kv) {}

  void load();
  bool persist();

  int find(const uint8_t id[LVP_ID_LEN]) const;
  // Neues unbekanntes Gateway aus der Erkennung. Verdraengt bei Bedarf das am laengsten
  // nicht gesehene unbekannte Gateway. Rueckgabe Index oder -1.
  int addDiscovered(const uint8_t id[LVP_ID_LEN], uint32_t nowMs);
  // Gekoppelt markieren und speichern. LVP_ERR_FULL, wenn bereits MAX_PAIRED gekoppelt sind.
  lvp_err_t markPaired(int idx);
  // Kopplung loeschen (Eintrag bleibt als unbekannt erhalten, Schluessel und Zuordnung entfallen).
  bool unpair(int idx);
  bool setAlias(int idx, const char *alias);

  int pairedCount() const;
  int size() const {
    return MAX_ENTRIES;
  }
  GatewayEntry &at(int i) {
    return e_[i];
  }
  const GatewayEntry &at(int i) const {
    return e_[i];
  }
  bool online(int i, uint32_t nowMs, uint32_t timeoutMs) const;

  // Serialisierung der gekoppelten Eintraege (Format 1)
  static size_t serialize(const GatewayEntry *e, int n, uint8_t *buf, size_t cap);
  static int deserialize(const uint8_t *buf, size_t len, GatewayEntry *out, int max);

private:
  KvStore &kv_;
  GatewayEntry e_[MAX_ENTRIES];
};
