#pragma once
// Kalibrierung des resistiven Touch (XPT2046): affine Abbildung Rohwert -> Bildschirmpunkt
// aus drei Referenzpunkten (Tippen auf Fadenkreuze). Gespeichert im NVS.
#include <stdbool.h>
#include <stdint.h>

struct TouchCal {
  // x = ax*rx + bx*ry + cx ; y = ay*rx + by*ry + cy (Festkomma: Werte * 65536)
  int32_t ax = 0, bx = 0, cx = 0, ay = 0, by = 0, cy = 0;
  bool valid = false;

  // Aus drei Paaren (Rohwert, Soll-Bildschirmpunkt). false, wenn die Punkte entartet sind.
  bool solve(const int32_t raw[3][2], const int32_t scr[3][2]);
  void map(int32_t rx, int32_t ry, int32_t *x, int32_t *y) const;
  // Standard ohne Kalibrierung: Rohbereich linear auf die Bildschirmgroesse (optional getauscht/gespiegelt)
  void setLinear(int32_t rxMin, int32_t rxMax, int32_t ryMin, int32_t ryMax, int32_t w, int32_t h, bool swapXY);
};
