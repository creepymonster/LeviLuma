#pragma once
// Monitorbefehle (Zeilenparser und Ausfuehrung auf den Einstellungen). Keine ESP-IDF-Abhaengigkeit.
// Die Funkseite (Kanal setzen, Suchlauf, Zaehler zuruecksetzen) fuehrt die Firmware anhand von CmdEffects aus.
#include <stddef.h>
#include <stdint.h>
#include "dbg_format.h"
#include "dbg_state.h"

namespace dbg {

struct Settings {
  Filter filter;
  FormatOptions fmt;
  ChannelPlan plan;
};

struct CmdEffects {
  bool set_channel = false; // Firmware soll auf channel umschalten (bereits gegen den Kanalplan geprueft)
  uint8_t channel = 0;
  bool scan_start = false;
  bool scan_stop = false;
  uint32_t dwell_ms = 0;
  bool reset_stats = false;
};

// Fuehrt eine Zeile aus und schreibt die Antwort nach out. Gibt false zurueck, wenn der Befehl unbekannt oder fehlerhaft war.
bool run_command(const char *line, Settings &st, const Stats &stats, uint32_t uptime_ms, Out &out, CmdEffects &fx);

void print_help(Out &out);

} // namespace dbg
