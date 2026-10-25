#pragma once
// Farbset des Farbwaehlers (LCD und Web-UI). Getrennt von den Kalibrierzielen (CalibCore.h).
// Config.h muss vorher eingebunden sein, damit LEVI_PICKER_LIST greift.
// Eintraege in Config.h: LEVI_PICKER_ENTRY("Name", R, G, B). 1 bis 24 Stueck, Name hoechstens 15 Zeichen.
#include <stdint.h>

#ifndef LEVI_PICKER_ENTRY
#define LEVI_PICKER_ENTRY(name, r, g, b) {name, (uint8_t)(r), (uint8_t)(g), (uint8_t)(b)}
#endif

#ifndef LEVI_PICKER_LIST
#define LEVI_PICKER_LIST \
  LEVI_PICKER_ENTRY("Weiß", 255, 255, 255), \
  LEVI_PICKER_ENTRY("Bernstein", 255, 176, 0), \
  LEVI_PICKER_ENTRY("Koralle", 255, 107, 107), \
  LEVI_PICKER_ENTRY("Rot", 230, 40, 50), \
  LEVI_PICKER_ENTRY("Orange", 255, 120, 40), \
  LEVI_PICKER_ENTRY("Gelb", 255, 230, 109), \
  LEVI_PICKER_ENTRY("Limette", 150, 230, 80), \
  LEVI_PICKER_ENTRY("Smaragd", 40, 200, 110), \
  LEVI_PICKER_ENTRY("Mint", 80, 230, 190), \
  LEVI_PICKER_ENTRY("Türkis", 0, 210, 210), \
  LEVI_PICKER_ENTRY("Himmelblau", 90, 170, 255), \
  LEVI_PICKER_ENTRY("Königsblau", 50, 90, 240), \
  LEVI_PICKER_ENTRY("Indigo", 110, 90, 240), \
  LEVI_PICKER_ENTRY("Flieder", 190, 140, 255), \
  LEVI_PICKER_ENTRY("Magenta", 230, 70, 200)
#endif

namespace LeviPalette {
struct Entry { const char *name; uint8_t r, g, b; };
static const Entry list[] = { LEVI_PICKER_LIST };
static constexpr int N = (int)(sizeof(list) / sizeof(list[0]));   // Anzahl ohne Musikmodus
static_assert(N >= 1 && N <= 24, "LEVI_PICKER_LIST: 1 bis 24 Eintraege");

static inline const char *name(int i) { return list[i].name; }
static inline void get(int i, uint8_t &r, uint8_t &g, uint8_t &b) {
  r = list[i].r; g = list[i].g; b = list[i].b;
}
}
