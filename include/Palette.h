#pragma once
// Farbset des Farbwaehlers (LCD und Web-UI). Getrennt von den Kalibrierzielen (CalibCore.h),
// weil die Kalibrierfarben fuer die Auswahl nicht taugen.
// Anpassbar in Config.h ueber LEVI_PICKER_RGB: flache Liste R,G,B,R,G,B,... (1 bis 24 Farben).
#include <stdint.h>
// Config.h muss vorher eingebunden sein, damit LEVI_PICKER_RGB greift.

#ifndef LEVI_PICKER_RGB
#define LEVI_PICKER_RGB 255,255,255, 255,243,224, 255,228,189, 255,197,143, 255,180,117, 255,64,64, 255,255,64, 64,255,64, 64,255,255, 64,64,255, 255,64,255, 255,128,64, 0,255,128, 0,128,255, 128,0,255, 255,0,128, 0,192,255
#endif

namespace LeviPalette {
static const uint8_t flat[] = {LEVI_PICKER_RGB};
static constexpr int N = (int)(sizeof(flat) / 3);   // Anzahl der Farben, ohne Musikmodus
static_assert(sizeof(flat) % 3 == 0, "LEVI_PICKER_RGB braucht Tripel R,G,B");
static_assert(N >= 1 && N <= 24, "LEVI_PICKER_RGB: 1 bis 24 Farben");

static inline void get(int i, uint8_t &r, uint8_t &g, uint8_t &b) {
  r = flat[i * 3]; g = flat[i * 3 + 1]; b = flat[i * 3 + 2];
}
}
