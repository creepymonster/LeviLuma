#pragma once
// Zaehler, Filter, Doppelerkennung und Kanalplan des Diagnosetools (alles ohne ESP-IDF, auf dem Host testbar).
#include <stddef.h>
#include <stdint.h>
#include "dbg_frame.h"
#include "dbg_levi.h"
#include "dbg_text.h"

namespace dbg {

// Zaehler. Jedes Feld hat genau einen schreibenden Kontext (Kommentar), gelesen wird ohne Sperre (32-Bit-Zugriffe).
struct Stats {
  // Nur im Empfangs-Callback (WLAN-Task) geschrieben:
  uint32_t mgmt_seen = 0;     // Management-Rahmen im Promiscuous-Callback
  uint32_t ignored = 0;       // davon kein ESP-NOW-Kandidat (verworfen, kein Datenverlust im Sinne der Diagnose)
  uint32_t cb_seen = 0;       // Rahmen aus dem ESP-NOW-Callback (nur mit Vergleichspfad)
  uint32_t queued = 0;        // in die Queue gestellt
  uint32_t queue_drops = 0;   // wegen voller Queue verworfen (verlorene Nachrichten!)
  uint32_t rx_cut = 0;        // beim Kopieren gekuerzt (Rahmen groesser als der Erfassungspuffer)
  // Nur im Auswertetask geschrieben:
  uint32_t processed = 0;     // aus der Queue verarbeitet
  uint32_t dup_dropped = 0;   // Doppelte aus dem zweiten Empfangsweg
  uint32_t filtered = 0;      // durch Filter ausgeblendet
  uint32_t printed = 0;       // ausgegeben
  uint32_t encrypted = 0;     // verschluesselt (Inhalt nicht lesbar)
  uint32_t not_espnow = 0;
  uint32_t truncated = 0;     // Erfassung unvollstaendig
  uint32_t malformed = 0;     // beschaedigt oder zu kurz
  uint32_t levi_ok = 0;       // gueltige LeviLuma-Nachrichten
  uint32_t levi_crc_bad = 0;
  uint32_t levi_other_err = 0; // LeviLuma-Kennung, aber unbekannter Typ/Version, falsche Laenge, ungueltige Nutzdaten
  uint32_t not_levi = 0;       // ESP-NOW ohne LeviLuma-Kennung
  uint32_t out_truncated = 0;  // Ausgabe wegen Puffergroesse gekuerzt
  uint32_t by_channel[15] = {0}; // gedruckte/verarbeitete Rahmen je Kanal (Index = Kanal, 0 = unbekannt)
};

void format_stats(Out &out, const Stats &s, uint32_t uptime_ms);

// ---- Filter ----
struct Filter {
  static constexpr int MAX_MACS = 4;
  uint8_t macs[MAX_MACS][6];
  int nmacs = 0;
  uint8_t type_bits[32] = {0}; // LeviLuma-Nachrichtentypen 0..255
  bool levi_only = false;      // nur LeviLuma-Rahmen
  bool show_encrypted = true;

  bool type_active() const;
  bool add_mac(const uint8_t mac[6]); // false: voll
  bool del_mac(const uint8_t mac[6]);
  void set_type(uint8_t t, bool on);
  bool has_type(uint8_t t) const { return type_bits[t >> 3] & (1u << (t & 7)); }
  void clear();
  // true = anzeigen
  bool match(const FrameInfo &fi, const LeviInfo &li) const;
  void describe(Out &out) const;
};

// ---- Doppelerkennung zwischen zwei Empfangswegen ----
// Derselbe Rahmen kann ueber Promiscuous und ueber den ESP-NOW-Callback eintreffen. Ein Treffer gilt nur, wenn Absender,
// Ziel und Body gleich sind, der Rahmen von einem ANDEREN Weg kam und innerhalb des Fensters liegt. Zwei Rahmen desselben
// Weges (z. B. MAC-Wiederholung mit Retry-Bit) sind echte Wiederholungen und werden nie unterdrueckt.
class Dedup {
public:
  static constexpr int N = 8;
  explicit Dedup(uint32_t window_ms = 50) : window_ms_(window_ms) {}
  // true = Duplikat (nicht ausgeben)
  bool seen(Source src, const FrameInfo &fi, uint32_t t_ms);
  static uint32_t key(const FrameInfo &fi);

private:
  struct E {
    bool used = false, matched = false;
    uint32_t key = 0, t_ms = 0;
    Source src = Source::Promiscuous;
  };
  E e_[N];
  int next_ = 0;
  uint32_t window_ms_;
};

// ---- Kanalplan ----
struct ChannelPlan {
  uint8_t first = 1, last = 11; // zulaessiger Bereich (Laenderregeln, vom Funktreiber)
  uint8_t current = 1;
  bool scan_available = false;  // Kconfig "Suchlauf erlauben"
  bool scanning = false;
  uint32_t dwell_ms = 500;
  bool stay_on_hit = true;

  bool valid(int ch) const { return ch >= first && ch <= last && ch >= 1 && ch <= 14; }
  uint8_t next_after(uint8_t ch) const { return (ch < first || ch >= last) ? first : (uint8_t)(ch + 1); }
};

} // namespace dbg
