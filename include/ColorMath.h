#pragma once
// Farbraum-Hilfen ohne Arduino-Abhaengigkeit (testbar auf dem Host).
// Ueberblendung: Farbton ueber den kuerzesten Weg auf dem Farbkreis, Saettigung und Wert linear.
#include <stdint.h>
#include <math.h>

namespace ColorMath {

struct HSV { float h, s, v; };  // h, s, v jeweils 0..1

inline HSV fromRgb(uint8_t r, uint8_t g, uint8_t b) {
    float rf = r / 255.f, gf = g / 255.f, bf = b / 255.f;
    float mx = fmaxf(rf, fmaxf(gf, bf)), mn = fminf(rf, fminf(gf, bf)), d = mx - mn;
    HSV c{0.f, 0.f, mx};
    if (mx > 0) c.s = d / mx;
    if (d > 0) {
        if (mx == rf)      c.h = fmodf((gf - bf) / d, 6.f);
        else if (mx == gf) c.h = (bf - rf) / d + 2.f;
        else               c.h = (rf - gf) / d + 4.f;
        c.h /= 6.f;
        if (c.h < 0) c.h += 1.f;
    }
    return c;
}

inline void toRgb(HSV c, uint8_t out[3]) {
    float h = c.h - floorf(c.h);
    float x = h * 6.f;
    int i = (int)floorf(x);
    float f = x - i;
    float p = c.v * (1 - c.s), q = c.v * (1 - c.s * f), t = c.v * (1 - c.s * (1 - f));
    float r, g, b;
    switch (i % 6) {
        case 0:  r = c.v; g = t;   b = p;   break;
        case 1:  r = q;   g = c.v; b = p;   break;
        case 2:  r = p;   g = c.v; b = t;   break;
        case 3:  r = p;   g = q;   b = c.v; break;
        case 4:  r = t;   g = p;   b = c.v; break;
        default: r = c.v; g = p;   b = q;   break;
    }
    out[0] = (uint8_t)(r * 255.f + 0.5f);
    out[1] = (uint8_t)(g * 255.f + 0.5f);
    out[2] = (uint8_t)(b * 255.f + 0.5f);
}

// t = 0..1 zwischen a und b, Farbton auf dem kuerzesten Weg.
inline HSV mix(HSV a, HSV b, float t) {
    float d = b.h - a.h;
    if (d > 0.5f) d -= 1.f;
    else if (d < -0.5f) d += 1.f;
    float h = a.h + d * t;
    h -= floorf(h);
    return HSV{h, a.s + (b.s - a.s) * t, a.v + (b.v - a.v) * t};
}

} // namespace ColorMath
