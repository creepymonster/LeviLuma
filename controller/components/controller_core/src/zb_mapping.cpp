#include "zb_mapping.h"
#include <math.h>

namespace ZbMap {

uint8_t assign(GatewayRegistry &reg, int idx) {
  GatewayEntry &g = reg.at(idx);

  if (!g.used || !g.paired) {
    return 0;
  }
  if (hasEndpoint(g)) {
    return g.zbEndpoint;
  }
  uint8_t old = g.zbEndpoint;
  for (uint8_t ep = FIRST_ENDPOINT; ep < FIRST_ENDPOINT + MAX_ENDPOINTS; ep++) {
    if (findByEndpoint(reg, ep) < 0) {
      g.zbEndpoint = ep;

      if (!reg.persist()) {
        g.zbEndpoint = old;
        return 0;
      }
      return ep;
    }
  }
  return 0;
}

bool release(GatewayRegistry &reg, int idx) {
  GatewayEntry &g = reg.at(idx);

  if (g.zbEndpoint == OPT_OUT) {
    return true;
  }
  uint8_t old = g.zbEndpoint;
  g.zbEndpoint = OPT_OUT;

  if (!reg.persist()) {
    g.zbEndpoint = old;
    return false;
  }
  return true;
}

int findByEndpoint(const GatewayRegistry &reg, uint8_t ep) {
  if (!ep || ep == OPT_OUT) {
    return -1;
  }
  for (int i = 0; i < reg.size(); i++) {
    if (reg.at(i).used && reg.at(i).paired && reg.at(i).zbEndpoint == ep) {
      return i;
    }
  }
  return -1;
}

bool assignAll(GatewayRegistry &reg) {
  bool changed = false;

  for (int i = 0; i < reg.size(); i++) {
    if (reg.at(i).used && reg.at(i).paired && !reg.at(i).zbEndpoint) {
      changed |= assign(reg, i) != 0;
    }
  }
  return changed;
}

uint32_t layoutSignature(const GatewayRegistry &reg) {
  uint32_t h = 2166136261u; // FNV-1a ueber (Endpoint, Gateway-ID)

  for (uint8_t ep = FIRST_ENDPOINT; ep < FIRST_ENDPOINT + MAX_ENDPOINTS; ep++) {
    int i = findByEndpoint(reg, ep);
    uint8_t buf[1 + LVP_ID_LEN] = {ep};

    if (i >= 0) {
      for (int k = 0; k < LVP_ID_LEN; k++) {
        buf[1 + k] = reg.at(i).id[k];
      }
    } else {
      buf[0] = 0;
    }
    for (uint8_t b : buf) {
      h = (h ^ b) * 16777619u;
    }
  }
  return h;
}

uint8_t levelToBrightness(uint8_t level) {
  int b = (level * 100 + 127) / 254;
  return (uint8_t)(b < 1 ? 1 : b > 100 ? 100 : b);
}

uint8_t brightnessToLevel(uint8_t brightness) {
  int l = (brightness * 254 + 50) / 100;
  return (uint8_t)(l < 1 ? 1 : l > 254 ? 254 : l);
}

static uint8_t to8(double v) {
  if (v < 0) {
    v = 0;
  }
  if (v > 1) {
    v = 1;
  }
  return (uint8_t)lround(v * 255.0);
}

void hueSatToRgb(uint8_t hue, uint8_t sat, uint8_t rgb[3]) {
  double h = hue / 254.0 * 6.0, s = sat / 254.0;
  int i = (int)floor(h) % 6;
  double f = h - floor(h);
  double p = 1 - s, q = 1 - s * f, t = 1 - s * (1 - f);
  double r, g, b;

  switch (i) {
  case 0:
    r = 1, g = t, b = p;
    break;
  case 1:
    r = q, g = 1, b = p;
    break;
  case 2:
    r = p, g = 1, b = t;
    break;
  case 3:
    r = p, g = q, b = 1;
    break;
  case 4:
    r = t, g = p, b = 1;
    break;
  default:
    r = 1, g = p, b = q;
    break;
  }
  rgb[0] = to8(r);
  rgb[1] = to8(g);
  rgb[2] = to8(b);
}

void rgbToHueSat(const uint8_t rgb[3], uint8_t *hue, uint8_t *sat) {
  double r = rgb[0] / 255.0, g = rgb[1] / 255.0, b = rgb[2] / 255.0;
  double mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b)), d = mx - mn;
  double h = 0;

  if (d > 0) {
    if (mx == r) {
      h = fmod((g - b) / d, 6.0);
    } else if (mx == g) {
      h = (b - r) / d + 2;
    } else {
      h = (r - g) / d + 4;
    }
    if (h < 0) {
      h += 6;
    }
  }
  *hue = (uint8_t)lround(h / 6.0 * 254.0) % 255;
  *sat = (uint8_t)(mx > 0 ? lround(d / mx * 254.0) : 0);
}

static double gammaEncode(double c) {
  return c <= 0.0031308 ? 12.92 * c : 1.055 * pow(c, 1 / 2.4) - 0.055;
}

static double gammaDecode(double c) {
  return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

void xyToRgb(uint16_t xi, uint16_t yi, uint8_t rgb[3]) {
  double x = xi / 65536.0, y = yi / 65536.0;

  if (y < 1e-4) {
    rgb[0] = rgb[1] = rgb[2] = 255;
    return;
  }
  double Y = 1.0, X = x * Y / y, Z = (1 - x - y) * Y / y;
  double r = 3.2406 * X - 1.5372 * Y - 0.4986 * Z;
  double g = -0.9689 * X + 1.8758 * Y + 0.0415 * Z;
  double b = 0.0557 * X - 0.2040 * Y + 1.0570 * Z;
  r = fmax(r, 0), g = fmax(g, 0), b = fmax(b, 0);
  double m = fmax(r, fmax(g, b));

  if (m > 0) { // volle Helligkeit; die Helligkeit kommt aus dem Level
    r /= m, g /= m, b /= m;
  }
  rgb[0] = to8(gammaEncode(r));
  rgb[1] = to8(gammaEncode(g));
  rgb[2] = to8(gammaEncode(b));
}

void rgbToXy(const uint8_t rgb[3], uint16_t *xo, uint16_t *yo) {
  double r = gammaDecode(rgb[0] / 255.0), g = gammaDecode(rgb[1] / 255.0), b = gammaDecode(rgb[2] / 255.0);
  double X = 0.4124 * r + 0.3576 * g + 0.1805 * b;
  double Y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
  double Z = 0.0193 * r + 0.1192 * g + 0.9505 * b;
  double s = X + Y + Z;
  double x = s > 0 ? X / s : 0.3127, y = s > 0 ? Y / s : 0.3290; // Schwarz -> Weisspunkt D65
  long xi = lround(x * 65536.0), yi = lround(y * 65536.0);
  *xo = (uint16_t)(xi > 65279 ? 65279 : xi);
  *yo = (uint16_t)(yi > 65279 ? 65279 : yi);
}

void miredToRgb(uint16_t mired, uint8_t rgb[3]) {
  if (mired < 153) {
    mired = 153;
  }
  if (mired > 500) {
    mired = 500;
  }
  double t = 1e6 / mired / 100.0; // Kelvin / 100
  double r, g, b;

  if (t <= 66) {
    r = 255;
    g = 99.4708025861 * log(t) - 161.1195681661;
    b = t <= 19 ? 0 : 138.5177312231 * log(t - 10) - 305.0447927307;
  } else {
    r = 329.698727446 * pow(t - 60, -0.1332047592);
    g = 288.1221695283 * pow(t - 60, -0.0755148492);
    b = 255;
  }
  rgb[0] = to8(r / 255.0);
  rgb[1] = to8(g / 255.0);
  rgb[2] = to8(b / 255.0);
}

} // namespace ZbMap
