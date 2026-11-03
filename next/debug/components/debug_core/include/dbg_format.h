#pragma once
// Textausgabe einer erfassten Nachricht (reine Formatierung, keine Ein-/Ausgabe).
#include <stdint.h>
#include "dbg_frame.h"
#include "dbg_levi.h"
#include "dbg_text.h"

namespace dbg {

struct FormatOptions {
  bool ascii = true;       // ASCII-Spalte im Hexdump
  bool full_frame = false; // zusaetzlich den kompletten erfassten 802.11-Rahmen als Hexdump
};

// Eine Nachricht mit laufender Nummer index. Nicht verfuegbare Angaben stehen als "nicht verfuegbar" da.
void format_record(Out &out, uint32_t index, const Capture &c, const FrameInfo &fi, const LeviInfo &li, const FormatOptions &opt);

} // namespace dbg
