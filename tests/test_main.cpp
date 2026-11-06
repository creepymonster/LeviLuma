// Host-Testlauf fuer LeviLuma Next (Protokoll, Gateway- und Controller-Logik).
// Die Bereiche (Gruppen) und ihre Beschreibungen stehen hier; die einzelnen Tests melden sich ueber RUN/RUN_D/SKIP.
// Maschinenlesbares Ergebnis: JSON Lines in die Datei aus Argument 1 oder $LEVI_TEST_RESULTS (Format siehe tools/gen_report.py).
// Jedes Ereignis wird sofort geschrieben, damit auch bei einem Abbruch (ASan/UBSan) ein Teilergebnis bleibt.
#include <stdlib.h>
#include <time.h>
#include <string>
#include <vector>
#include "test_util.h"

int g_checks = 0, g_failures = 0;

#ifndef LEVI_SANITIZERS
#define LEVI_SANITIZERS "keine"
#endif

static FILE *g_out = nullptr;
static std::string g_group, g_name, g_desc;
static int g_checks0 = 0, g_failures0 = 0;
static std::vector<std::string> g_msgs;
static int g_tests = 0, g_passed = 0, g_failed = 0, g_skipped = 0;

static std::string esc(const char *s) {
  std::string r;
  for (; s && *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') {
      r += '\\';
      r += (char)c;
    } else if (c < 0x20) {
      char b[8];
      snprintf(b, sizeof(b), "\\u%04x", c);
      r += b;
    } else {
      r += (char)c;
    }
  }
  return r;
}

static void emit(const std::string &line) {
  if (g_out) {
    fputs(line.c_str(), g_out);
    fputc('\n', g_out);
    fflush(g_out);
  }
}

void levi_group(const char *name, const char *desc) {
  g_group = name;
  printf("== %s ==\n", name);
  emit("{\"ev\":\"group\",\"name\":\"" + esc(name) + "\",\"desc\":\"" + esc(desc) + "\"}");
}

void levi_test_begin(const char *name, const char *desc) {
  g_name = name;
  g_desc = desc ? desc : "";
  g_checks0 = g_checks;
  g_failures0 = g_failures;
  g_msgs.clear();
  emit("{\"ev\":\"begin\",\"group\":\"" + esc(g_group.c_str()) + "\",\"test\":\"" + esc(name) + "\"}");
}

void levi_check_failed(const char *file, int line, const char *text) {
  printf("  FEHLER %s:%d: %s\n", file, line, text);
  if (g_msgs.size() < 5) {
    g_msgs.push_back(std::string(file) + ":" + std::to_string(line) + ": " + text);
  }
}

static void emit_test(const char *status, int checks, int failures, const char *reason) {
  std::string l = "{\"ev\":\"test\",\"group\":\"" + esc(g_group.c_str()) + "\",\"name\":\"" + esc(g_name.c_str()) + "\",\"desc\":\"" + esc(g_desc.c_str()) +
                  "\",\"status\":\"" + status + "\",\"checks\":" + std::to_string(checks) + ",\"failures\":" + std::to_string(failures);
  if (reason) {
    l += ",\"reason\":\"" + esc(reason) + "\"";
  }
  l += ",\"messages\":[";
  for (size_t i = 0; i < g_msgs.size(); i++) {
    l += (i ? ",\"" : "\"") + esc(g_msgs[i].c_str()) + "\"";
  }
  emit(l + "]}");
}

void levi_test_end(void) {
  int checks = g_checks - g_checks0, failures = g_failures - g_failures0;
  bool ok = failures == 0;
  g_tests++;
  (ok ? g_passed : g_failed)++;
  printf("%s %s\n", ok ? "ok  " : "FAIL", g_name.c_str());
  emit_test(ok ? "passed" : "failed", checks, failures, nullptr);
}

void levi_test_skip(const char *name, const char *desc, const char *reason) {
  g_name = name;
  g_desc = desc ? desc : "";
  g_msgs.clear();
  g_tests++;
  g_skipped++;
  printf("SKIP %s (%s)\n", name, reason);
  emit_test("skipped", 0, 0, reason);
}

int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : getenv("LEVI_TEST_RESULTS");
  if (path && *path) {
    g_out = fopen(path, "w");
    if (!g_out) {
      fprintf(stderr, "Ergebnisdatei nicht beschreibbar: %s\n", path);
      return 2; // lieber abbrechen als einen Lauf ohne Bericht als erfolgreich auszugeben
    }
  }
  time_t t = time(nullptr);
  char ts[40];
  strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
  emit(std::string("{\"ev\":\"meta\",\"format\":1,\"started_utc\":\"") + ts + "\",\"compiler\":\"" + esc(__VERSION__) + "\",\"sanitizers\":\"" +
       esc(LEVI_SANITIZERS) + "\"}");

  levi_group("Protokoll", "ESP-NOW-Rahmen: Kodierung, Pruefsumme, Grenzen, Fehlerfaelle beim Dekodieren, Validierung, Sequenz-Ueberlauf, Fuzzing.");
  tests_protocol();
  levi_group("Zuverlaessigkeit", "Wiederholung mit Zeitablauf, Zeitueberlauf, Duplikaterkennung beim Empfaenger, Online-Erkennung.");
  tests_link();
  levi_group("Gateway", "Lichtzustand, BLE-Befehlsreihenfolge, Kalibrierung und Speicher, Kopplungstabelle, Schluessel, Gateway-Handler (Anfragen, Antworten, Ablehnungen).");
  tests_gateway();
  levi_group("Controller", "Gateway-Register, Zigbee-Zuordnung, Touch-Kalibrierung und End-to-End Controller<->Gateway ueber simuliertes ESP-NOW (echter Gateway-Code): Kopplung, Verlust, Wiederholung, Warteregel, Neustart, Kalibrierung.");
  tests_controller();
  levi_group("Runder Controller", "Ein-Gateway-Regel und Sendedrossel des runden Controllers.");
  tests_round();
  levi_group("Diagnose", "ESP-NOW-Diagnosegeraet (debug/): Rahmenauswertung, LeviLuma-Dekodierung aller Nachrichtentypen, Ausgabe, Filter, Doppelerkennung, Monitorbefehle, Fuzzing. Alle Rahmen sind synthetisch.");
  tests_debug();

  printf("\n%d Pruefungen, %d Fehler\n", g_checks, g_failures);
  emit("{\"ev\":\"summary\",\"tests\":" + std::to_string(g_tests) + ",\"passed\":" + std::to_string(g_passed) + ",\"failed\":" + std::to_string(g_failed) +
       ",\"skipped\":" + std::to_string(g_skipped) + ",\"checks\":" + std::to_string(g_checks) + ",\"failures\":" + std::to_string(g_failures) + "}");
  if (g_out) {
    fclose(g_out);
  }
  return g_failures != 0;
}
