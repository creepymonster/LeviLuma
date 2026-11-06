#pragma once
// Gemerktes Band des Gateways (persistent, NVS ueber KvStore) und Auswahl nach dem BLE-Scan.
// Regeln:
//  - Ohne gemerktes Band: das staerkste Geraet mit passendem Namen der ELK-/Lotus-Familie (wie Legacy).
//    Nach der ersten erfolgreichen Verbindung (Service 0xFFF0 / Characteristic 0xFFF3 gefunden) wird es gemerkt.
//  - Mit gemerktem Band: nur dieses Band (Adresse und Adresstyp), unabhaengig von Name und Signalstaerke.
//    Andere passende Baender werden ignoriert, damit ein Gateway nie das Band eines anderen Gateways uebernimmt.
//  - Vergessen nur ausdruecklich (Konsole "band forget").
#include <stddef.h>
#include <stdint.h>
#include "kv_store.h"

class BandMemory {
public:
  static constexpr const char *KEY = "band";

  // BLE-Adresse wie NimBLE (ble_addr_t): val[0] ist das niederwertigste Byte
  struct Addr {
    uint8_t type = 0;
    uint8_t val[6] = {0};
  };
  struct Candidate {
    Addr addr;
    int8_t rssi = -128;
    const char *name = ""; // leer, wenn kein Name empfangen wurde
  };
  enum class Reason : uint8_t {
    None,              // nichts gemerkt und kein passendes Geraet gefunden
    Strongest,         // nichts gemerkt: staerkstes passendes Geraet
    Remembered,        // gemerktes Band gefunden
    RememberedMissing, // gemerktes Band nicht gefunden
  };
  struct Choice {
    int index = -1;     // Index in der Kandidatenliste oder -1
    Reason reason = Reason::None;
    int ignored = 0;    // passende Baender, die wegen des gemerkten Bands nicht gewaehlt wurden
  };

  explicit BandMemory(KvStore &kv) : kv_(kv) {}

  void load();
  bool has() const {
    return has_;
  }
  const Addr &addr() const {
    return addr_;
  }
  bool remember(const Addr &a); // false bei Speicherfehler (dann bleibt der alte Stand)
  bool forget();

  Choice choose(const Candidate *c, int n) const;

  // Namen der Lotus-Lantern-/ELK-Familie (gleiches Protokoll, Service 0xFFF0), wie Legacy bleNameMatches()
  static bool nameMatches(const char *name);
  static bool sameAddr(const Addr &a, const Addr &b);
  // "aa:bb:cc:dd:ee:ff" (hoechstwertiges Byte zuerst, wie NimBLE-Ausgaben und Handy-Apps)
  static void addrToStr(const Addr &a, char *out, size_t cap);

  // Serialisierung (Format: Version 1, Adresstyp, 6 Adressbytes)
  static constexpr size_t BLOB_LEN = 8;
  static size_t serialize(const Addr &a, uint8_t *buf, size_t cap);
  static bool deserialize(const uint8_t *buf, size_t len, Addr &out);

private:
  KvStore &kv_;
  Addr addr_;
  bool has_ = false;
};
