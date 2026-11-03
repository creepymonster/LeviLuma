#pragma once
// Minimales Test-Geruest fuer die Host-Tests (ohne externe Abhaengigkeiten).
#include <stdio.h>
#include <string.h>

extern int g_checks, g_failures;

// Gemeinsame Testregistrierung: RUN/RUN_D/SKIP melden jeden Test an test_main.cpp. Dort entstehen die Konsolenausgabe
// und (mit LEVI_TEST_RESULTS=<Datei> oder Argument 1) das maschinenlesbare Ergebnis (JSON Lines), aus dem
// tools/gen_report.py die Auswertung next/tests/README.md erzeugt. Es gibt keine zweite Liste der Tests.
void levi_group(const char *name, const char *desc);                 // neue Testgruppe (Bereich) mit Beschreibung
void levi_test_begin(const char *name, const char *desc);            // desc darf nullptr sein
void levi_test_end(void);
void levi_test_skip(const char *name, const char *desc, const char *reason); // uebersprungen (Grund Pflicht)
void levi_check_failed(const char *file, int line, const char *text);

#define CHECK(cond)                                                                                                                            \
  do {                                                                                                                                         \
    g_checks++;                                                                                                                                \
    if (!(cond)) {                                                                                                                             \
      g_failures++;                                                                                                                            \
      levi_check_failed(__FILE__, __LINE__, #cond);                                                                                            \
    }                                                                                                                                          \
  } while (0)

#define CHECK_EQ(a, b)                                                                                                                         \
  do {                                                                                                                                         \
    long long _a = (long long)(a), _b = (long long)(b);                                                                                        \
    g_checks++;                                                                                                                                \
    if (_a != _b) {                                                                                                                            \
      g_failures++;                                                                                                                            \
      char _m[256];                                                                                                                            \
      snprintf(_m, sizeof(_m), "%s == %s (%lld != %lld)", #a, #b, _a, _b);                                                                     \
      levi_check_failed(__FILE__, __LINE__, _m);                                                                                               \
    }                                                                                                                                          \
  } while (0)

// Test ausfuehren und registrieren; RUN_D mit Kurzbeschreibung (abgedeckte Funktion/Fehlerfall)
#define RUN_D(fn, desc)                                                                                                                        \
  do {                                                                                                                                         \
    levi_test_begin(#fn, desc);                                                                                                                \
    fn();                                                                                                                                      \
    levi_test_end();                                                                                                                           \
  } while (0)
#define RUN(fn) RUN_D(fn, nullptr)
// Bewusst uebersprungener Test: erscheint im Bericht als "uebersprungen" mit Grund, nie als bestanden
#define SKIP(fn, reason)                                                                                                                       \
  do {                                                                                                                                         \
    (void)&fn;                                                                                                                                 \
    levi_test_skip(#fn, nullptr, reason);                                                                                                      \
  } while (0)

// Testgruppen (je Datei eine)
void tests_protocol(void);
void tests_link(void);
void tests_gateway(void);
void tests_controller(void);
void tests_round(void);
