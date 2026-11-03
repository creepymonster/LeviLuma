#include "round_core.h"
#include <math.h>

namespace Round {

int activeGateway(const GatewayRegistry &reg) {
  for (int i = 0; i < reg.size(); i++) {
    if (reg.at(i).used && reg.at(i).paired) {
      return i;
    }
  }
  return -1;
}

bool mayPair(const GatewayRegistry &reg) {
  return reg.pairedCount() == 0;
}

static const float PI = 3.14159265f;

static uint8_t to8(float v) {
  if (v < 0) {
    v = 0;
  }
  if (v > 1) {
    v = 1;
  }
  return (uint8_t)lroundf(v * 255.f);
}

void wheelToRgb(float dx, float dy, float r, uint8_t rgb[3]) {
  float d = sqrtf(dx * dx + dy * dy);
  float s = r > 0 ? d / r : 0;
  if (s > 1) {
    s = 1;
  }
  // Winkel ab oben, im Uhrzeigersinn (Bildschirm: y nach unten)
  float a = atan2f(dx, -dy);
  if (a < 0) {
    a += 2 * PI;
  }
  float h = a / (2 * PI) * 6.f;
  int i = (int)floorf(h) % 6;
  float f = h - floorf(h);
  float p = 1 - s, q = 1 - s * f, t = 1 - s * (1 - f);
  float c[3];
  switch (i) {
  case 0:
    c[0] = 1, c[1] = t, c[2] = p;
    break;
  case 1:
    c[0] = q, c[1] = 1, c[2] = p;
    break;
  case 2:
    c[0] = p, c[1] = 1, c[2] = t;
    break;
  case 3:
    c[0] = p, c[1] = q, c[2] = 1;
    break;
  case 4:
    c[0] = t, c[1] = p, c[2] = 1;
    break;
  default:
    c[0] = 1, c[1] = p, c[2] = q;
    break;
  }
  for (int k = 0; k < 3; k++) {
    rgb[k] = to8(c[k]);
  }
}

void rgbToWheel(const uint8_t rgb[3], float r, float *dx, float *dy) {
  float R = rgb[0] / 255.f, G = rgb[1] / 255.f, B = rgb[2] / 255.f;
  float mx = fmaxf(R, fmaxf(G, B)), mn = fminf(R, fminf(G, B)), d = mx - mn;
  float h = 0;
  if (d > 0) {
    if (mx == R) {
      h = fmodf((G - B) / d, 6.f);
    } else if (mx == G) {
      h = (B - R) / d + 2;
    } else {
      h = (R - G) / d + 4;
    }
    if (h < 0) {
      h += 6;
    }
  }
  float s = mx > 0 ? d / mx : 0;
  float a = h / 6.f * 2 * PI;
  *dx = sinf(a) * s * r;
  *dy = -cosf(a) * s * r;
}

bool Throttle::update(uint32_t nowMs) {
  dirty_ = true;
  if (!sentOnce_ || (uint32_t)(nowMs - last_) >= interval_) {
    last_ = nowMs;
    sentOnce_ = true;
    dirty_ = false;
    return true;
  }
  return false;
}

bool Throttle::release(uint32_t nowMs) {
  bool send = dirty_;
  dirty_ = false;
  sentOnce_ = false;
  if (send) {
    last_ = nowMs;
  }
  return send;
}

} // namespace Round
