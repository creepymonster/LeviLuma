#pragma once
// LeviLuma-Dekodierung fuer das Diagnosetool. Nutzt ausschliesslich next/shared/protocol (lvp_decode, lvp_crc16,
// Namenstabellen); es gibt keine eigene Kopie des Protokolls. Neue Nachrichtentypen: siehe AGENTS.md ("Diagnose").
#include <stddef.h>
#include <stdint.h>
#include "leviluma_proto.h"

namespace dbg {

struct Field {
  char name[24];
  char value[96];
};

struct FieldList {
  static constexpr int MAX = 24;
  Field f[MAX];
  int n = 0;
  bool overflow = false;
  void add(const char *name, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
};

struct LeviInfo {
  enum class Kind : uint8_t {
    NotLevi = 0, // keine LeviLuma-Kennung ("LV")
    Decoded,     // Kopf, Laenge, CRC und Nutzdaten gueltig
    Unsupported, // Kopf und CRC gueltig, aber Typ unbekannt (Nutzdaten nicht dekodierbar)
    Invalid,     // LeviLuma-Kennung, aber Rahmen fehlerhaft oder nicht pruefbar
  };
  Kind kind = Kind::NotLevi;
  lvp_err_t err = LVP_OK; // Ergebnis von lvp_decode bzw. lokale Ursache (Invalid/Unsupported)
  const char *reason = "";

  bool hdr_read = false;     // 16 Kopfbytes vorhanden und gelesen (nur mit hdr_verified geprueft)
  bool hdr_verified = false; // CRC stimmt: Kopffelder sind bestaetigt
  uint8_t version = 0, type = 0, flags = 0, len = 0;
  uint16_t seq = 0, ref_seq = 0;
  uint32_t session = 0;

  bool crc_checked = false; // nur wenn die vollstaendigen Nutzdaten vorlagen
  bool crc_ok = false;
  uint16_t crc_rx = 0, crc_calc = 0;

  size_t payload_avail = 0; // vorhandene Nutzdatenbytes hinter dem Kopf
  FieldList fields;         // nur bei Decoded
};

// Wertet die ESP-NOW-Nutzdaten aus. complete = false, wenn die Erfassung unvollstaendig ist (dann keine CRC-Pruefung).
void decode_levi(const uint8_t *body, size_t len, bool complete, LeviInfo &out);

// true, wenn dieses Tool fuer den Typ eine Feldausgabe kennt. Test: jeder Typ, den lvp_type_name kennt, muss true liefern.
bool levi_type_supported(uint8_t type);

// "ANNOUNCE (0x02)" bzw. "unbekannt (0x55)"
void type_label(uint8_t type, char *out, size_t cap);
// "0x03 [RESPONSE|NOTIFY]" (unbekannte Bits werden ausgewiesen)
void flags_label(uint8_t flags, char *out, size_t cap);

} // namespace dbg
