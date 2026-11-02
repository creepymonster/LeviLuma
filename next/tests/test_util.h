#pragma once
// Minimales Test-Geruest fuer die Host-Tests (ohne externe Abhaengigkeiten).
#include <stdio.h>
#include <string.h>

extern int g_checks, g_failures;

#define CHECK(cond)                                                                                                                            \
  do {                                                                                                                                         \
    g_checks++;                                                                                                                                \
    if (!(cond)) {                                                                                                                             \
      g_failures++;                                                                                                                            \
      printf("  FEHLER %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                                              \
    }                                                                                                                                          \
  } while (0)

#define CHECK_EQ(a, b)                                                                                                                         \
  do {                                                                                                                                         \
    long long _a = (long long)(a), _b = (long long)(b);                                                                                        \
    g_checks++;                                                                                                                                \
    if (_a != _b) {                                                                                                                            \
      g_failures++;                                                                                                                            \
      printf("  FEHLER %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b);                                                \
    }                                                                                                                                          \
  } while (0)

#define RUN(fn)                                                                                                                                \
  do {                                                                                                                                         \
    int _f = g_failures;                                                                                                                       \
    fn();                                                                                                                                      \
    printf("%s %s\n", g_failures == _f ? "ok  " : "FAIL", #fn);                                                                              \
  } while (0)

// Testgruppen (je Datei eine)
void tests_protocol(void);
void tests_link(void);
void tests_gateway(void);
void tests_controller(void);
