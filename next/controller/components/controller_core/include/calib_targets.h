#pragma once
// Zielfarben der Kalibrierung fuer die Oberflaeche des Controllers.
// Reihenfolge und Werte identisch mit LeviCal::TARGET (CalibCore.h, Gateway/Legacy),
// Namen wie im Kalibrierassistenten der Legacy-Web-UI (WebUi.h, const CAL). Ein Host-Test prueft die Gleichheit.
#include <stdint.h>

namespace CalTargets {
static const int N = 10;
struct Target {
  const char *name;
  uint8_t r, g, b;
};
static const Target list[N] = {
    {"D65", 255, 255, 255}, {"3000K", 255, 197, 143}, {"Rot", 255, 0, 0},  {"Koralle", 255, 64, 64}, {"Orange", 255, 128, 64},
    {"Gelb", 255, 255, 0},  {"Gruen", 0, 255, 0},     {"Cyan", 0, 255, 255}, {"Blau", 0, 0, 255},     {"Magenta", 255, 0, 255},
};
} // namespace CalTargets
