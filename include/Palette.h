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
  LEVI_PICKER_ENTRY("Warmweiß", 255, 243, 224), \
  LEVI_PICKER_ENTRY("Creme", 255, 228, 189), \
  LEVI_PICKER_ENTRY("Pfirsich", 255, 197, 143), \
  LEVI_PICKER_ENTRY("Apricot", 255, 180, 117), \
  LEVI_PICKER_ENTRY("Rot", 255, 64, 64), \
  LEVI_PICKER_ENTRY("Gelb", 255, 255, 64), \
  LEVI_PICKER_ENTRY("Hellgrün", 64, 255, 64), \
  LEVI_PICKER_ENTRY("Türkis", 64, 255, 255), \
  LEVI_PICKER_ENTRY("Blau", 64, 64, 255), \
  LEVI_PICKER_ENTRY("Magenta", 255, 64, 255), \
  LEVI_PICKER_ENTRY("Orange", 255, 128, 64), \
  LEVI_PICKER_ENTRY("Mint", 0, 255, 128), \
  LEVI_PICKER_ENTRY("Azurblau", 0, 128, 255), \
  LEVI_PICKER_ENTRY("Violett", 128, 0, 255), \
  LEVI_PICKER_ENTRY("Pink", 255, 0, 128), \
  LEVI_PICKER_ENTRY("Himmelblau", 0, 192, 255)
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
