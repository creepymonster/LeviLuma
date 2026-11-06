#include "dbg_cmd.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace dbg {

static constexpr int MAX_TOK = 6;

static int tokenize(char *line, char *tok[], int max) {
  int n = 0;
  char *p = line;

  while (*p && n < max) {
    while (*p && isspace((unsigned char)*p)) {
      *p++ = 0;
    }
    if (!*p) {
      break;
    }
    tok[n++] = p;
    while (*p && !isspace((unsigned char)*p)) {
      p++;
    }
  }
  while (*p && isspace((unsigned char)*p)) {
    *p++ = 0;
  }
  if (*p) {
    return max + 1; // weitere Woerter vorhanden
  }
  return n;
}

static bool ieq(const char *a, const char *b) {
  for (; *a && *b; a++, b++) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
      return false;
    }
  }
  return *a == *b;
}

// Zahl dezimal oder 0x.., nur ganze Zeichenkette, 0..max
static bool parse_num(const char *s, unsigned long max, unsigned long *v) {
  if (!s || !*s) {
    return false;
  }
  char *end = nullptr;
  unsigned long x = strtoul(s, &end, (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? 16 : 10);

  if (*end || x > max) {
    return false;
  }
  *v = x;
  return true;
}

// Typ als Name (DISCOVER, set_state ...) oder Zahl
static bool parse_type(const char *s, uint8_t *t) {
  unsigned long v;

  if (parse_num(s, 255, &v)) {
    *t = (uint8_t)v;
    return true;
  }
  for (int i = 1; i < 256; i++) {
    const char *nm = lvp_type_name((uint8_t)i);

    if (nm[0] != '?' && ieq(nm, s)) {
      *t = (uint8_t)i;
      return true;
    }
  }
  return false;
}

static bool parse_onoff(const char *s, bool *v) {
  if (ieq(s, "on") || ieq(s, "1") || ieq(s, "ein")) {
    *v = true;
    return true;
  }
  if (ieq(s, "off") || ieq(s, "0") || ieq(s, "aus")) {
    *v = false;
    return true;
  }
  return false;
}

void print_help(Out &out) {
  out.puts("Befehle (Zeile mit Enter abschliessen, 115200 Baud):\n"
           "  help                         diese Hilfe\n"
           "  stats [reset]                Zaehler anzeigen bzw. zuruecksetzen\n"
           "  channel [1..14]              Empfangskanal anzeigen bzw. aendern (nur laut Laenderregeln zulaessige)\n"
           "  scan [on [ms]|off]           Kanalsuchlauf (nur wenn per Kconfig erlaubt); verpasst Rahmen auf anderen Kanaelen\n"
           "  filter                       Filter anzeigen\n"
           "  filter clear                 alle Filter loeschen\n"
           "  filter mac add|del <MAC>     Absender-/Ziel-MAC (bis 4); 'filter mac clear' loescht alle\n"
           "  filter type add|del <Typ>    LeviLuma-Typ (Name oder Zahl, z. B. STATE oder 0x22); 'filter type clear'\n"
           "  filter levi on|off           nur Rahmen mit LeviLuma-Kennung\n"
           "  filter enc on|off            verschluesselte Rahmen anzeigen\n"
           "  raw on|off                   zusaetzlich den kompletten erfassten Rahmen als Hexdump\n"
           "  ascii on|off                 ASCII-Spalte im Hexdump\n");
}

static bool bad(Out &out, const char *msg) {
  out.printf("Fehler: %s (\"help\" zeigt die Befehle)\n", msg);
  return false;
}

static bool cmd_filter(char *tok[], int n, Settings &st, Out &out) {
  Filter &f = st.filter;

  if (n == 1) {
    f.describe(out);
    return true;
  }
  if (n == 2 && ieq(tok[1], "clear")) {
    f.clear();
    out.puts("Alle Filter geloescht.\n");
    return true;
  }
  if (n == 3 && ieq(tok[1], "levi")) {
    bool v;
    if (!parse_onoff(tok[2], &v)) {
      return bad(out, "on oder off erwartet");
    }
    f.levi_only = v;
    f.describe(out);
    return true;
  }
  if (n == 3 && ieq(tok[1], "enc")) {
    bool v;
    if (!parse_onoff(tok[2], &v)) {
      return bad(out, "on oder off erwartet");
    }
    f.show_encrypted = v;
    f.describe(out);
    return true;
  }
  if (n >= 3 && ieq(tok[1], "mac")) {
    if (n == 3 && ieq(tok[2], "clear")) {
      f.nmacs = 0;
      f.describe(out);
      return true;
    }
    if (n != 4) {
      return bad(out, "filter mac add|del <AA:BB:CC:DD:EE:FF>");
    }
    uint8_t mac[6];
    if (!mac_parse(tok[3], mac)) {
      return bad(out, "ungueltige MAC (AA:BB:CC:DD:EE:FF)");
    }
    if (ieq(tok[2], "add")) {
      if (!f.add_mac(mac)) {
        return bad(out, "Filterliste voll (4 MAC)");
      }
    } else if (ieq(tok[2], "del")) {
      if (!f.del_mac(mac)) {
        return bad(out, "MAC nicht im Filter");
      }
    } else {
      return bad(out, "add oder del erwartet");
    }
    f.describe(out);
    return true;
  }
  if (n >= 3 && ieq(tok[1], "type")) {
    if (n == 3 && ieq(tok[2], "clear")) {
      memset(f.type_bits, 0, sizeof(f.type_bits));
      f.describe(out);
      return true;
    }
    if (n != 4) {
      return bad(out, "filter type add|del <Typ>");
    }
    uint8_t t;
    if (!parse_type(tok[3], &t)) {
      return bad(out, "unbekannter Typ (Name aus dem Protokoll oder Zahl 0..255)");
    }
    if (ieq(tok[2], "add")) {
      f.set_type(t, true);
    } else if (ieq(tok[2], "del")) {
      f.set_type(t, false);
    } else {
      return bad(out, "add oder del erwartet");
    }
    f.describe(out);
    return true;
  }
  return bad(out, "unbekannter filter-Befehl");
}

bool run_command(const char *line, Settings &st, const Stats &stats, uint32_t uptime_ms, Out &out, CmdEffects &fx) {
  char buf[96];
  size_t len = line ? strlen(line) : 0;

  if (len >= sizeof(buf)) {
    return bad(out, "Zeile zu lang");
  }
  memcpy(buf, line ? line : "", len + 1);
  char *tok[MAX_TOK];
  int n = tokenize(buf, tok, MAX_TOK);

  if (n == 0) {
    return true;
  }
  if (n > MAX_TOK) {
    return bad(out, "zu viele Argumente");
  }
  if (ieq(tok[0], "help") || ieq(tok[0], "?")) {
    print_help(out);
    return true;
  }
  if (ieq(tok[0], "stats")) {
    if (n == 2 && ieq(tok[1], "reset")) {
      fx.reset_stats = true;
      out.puts("Zaehler werden zurueckgesetzt.\n");
      return true;
    }
    if (n != 1) {
      return bad(out, "stats [reset]");
    }
    format_stats(out, stats, uptime_ms);
    return true;
  }
  if (ieq(tok[0], "channel")) {
    if (n == 1) {
      out.printf("Kanal %u%s (zulaessig laut Laenderregeln: %u..%u)\n", st.plan.current, st.plan.scanning ? ", Suchlauf aktiv" : "", st.plan.first,
                 st.plan.last);
      return true;
    }
    unsigned long ch;
    if (n != 2 || !parse_num(tok[1], 14, &ch) || ch < 1) {
      return bad(out, "channel <1..14>");
    }
    if (!st.plan.valid((int)ch)) {
      out.printf("Kanal %lu nicht zulaessig (Laenderregeln des Funktreibers: %u..%u).\n", ch, st.plan.first, st.plan.last);
      return false;
    }
    fx.set_channel = true;
    fx.channel = (uint8_t)ch;
    fx.scan_stop = st.plan.scanning; // fester Kanal beendet den Suchlauf
    out.printf("Wechsle auf Kanal %lu%s.\n", ch, st.plan.scanning ? " (Suchlauf beendet)" : "");
    return true;
  }
  if (ieq(tok[0], "scan")) {
    if (n == 1) {
      out.printf("Suchlauf: %s%s, Verweilzeit %lu ms, Bereich %u..%u\n", st.plan.scan_available ? "erlaubt" : "NICHT erlaubt (Kconfig)",
                 st.plan.scanning ? ", aktiv" : ", aus", (unsigned long)st.plan.dwell_ms, st.plan.first, st.plan.last);
      return true;
    }
    if (!st.plan.scan_available) {
      out.puts("Suchlauf ist nicht erlaubt (Kconfig LEVILUMA_DEBUG_SCAN_ENABLE).\n");
      return false;
    }
    if (ieq(tok[1], "off") && n == 2) {
      fx.scan_stop = true;
      out.puts("Suchlauf wird beendet; Empfang bleibt auf dem aktuellen Kanal.\n");
      return true;
    }
    if (ieq(tok[1], "on") && n <= 3) {
      unsigned long ms = st.plan.dwell_ms;
      if (n == 3 && (!parse_num(tok[2], 5000, &ms) || ms < 100)) {
        return bad(out, "Verweilzeit 100..5000 ms");
      }
      fx.scan_start = true;
      fx.dwell_ms = (uint32_t)ms;
      out.printf("Suchlauf startet (%lu ms je Kanal). Waehrenddessen gehen Nachrichten auf den anderen Kanaelen verloren.\n", ms);
      return true;
    }
    return bad(out, "scan [on [ms]|off]");
  }
  if (ieq(tok[0], "filter")) {
    return cmd_filter(tok, n, st, out);
  }
  if (ieq(tok[0], "raw") || ieq(tok[0], "ascii")) {
    bool v;
    if (n != 2 || !parse_onoff(tok[1], &v)) {
      return bad(out, "on oder off erwartet");
    }
    bool &target = ieq(tok[0], "raw") ? st.fmt.full_frame : st.fmt.ascii;
    target = v;
    out.printf("%s: %s\n", tok[0], v ? "an" : "aus");
    return true;
  }
  out.printf("Unbekannter Befehl \"%s\".\n", tok[0]);
  print_help(out);
  return false;
}

} // namespace dbg
