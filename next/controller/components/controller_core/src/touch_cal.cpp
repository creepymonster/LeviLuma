#include "touch_cal.h"
#include <math.h>

bool TouchCal::solve(const int32_t raw[3][2], const int32_t scr[3][2]) {
  double x1 = raw[0][0], y1 = raw[0][1], x2 = raw[1][0], y2 = raw[1][1], x3 = raw[2][0], y3 = raw[2][1];
  double det = x1 * (y2 - y3) - y1 * (x2 - x3) + (x2 * y3 - x3 * y2);

  if (fabs(det) < 1.0) {
    valid = false;
    return false;
  }
  // Cramersche Regel fuer [rx ry 1] * [a b c]^T = s
  auto coef = [&](int k, double *a, double *b, double *c) {
    double s1 = scr[0][k], s2 = scr[1][k], s3 = scr[2][k];
    *a = (s1 * (y2 - y3) - y1 * (s2 - s3) + (s2 * y3 - s3 * y2)) / det;
    *b = (x1 * (s2 - s3) - s1 * (x2 - x3) + (x2 * s3 - x3 * s2)) / det;
    *c = (x1 * (y2 * s3 - y3 * s2) - y1 * (x2 * s3 - x3 * s2) + s1 * (x2 * y3 - x3 * y2)) / det;
  };
  double a, b, c;
  coef(0, &a, &b, &c);
  ax = (int32_t)lround(a * 65536.0);
  bx = (int32_t)lround(b * 65536.0);
  cx = (int32_t)lround(c * 65536.0);
  coef(1, &a, &b, &c);
  ay = (int32_t)lround(a * 65536.0);
  by = (int32_t)lround(b * 65536.0);
  cy = (int32_t)lround(c * 65536.0);
  valid = true;
  return true;
}

void TouchCal::map(int32_t rx, int32_t ry, int32_t *x, int32_t *y) const {
  int64_t vx = (int64_t)ax * rx + (int64_t)bx * ry + cx;
  int64_t vy = (int64_t)ay * rx + (int64_t)by * ry + cy;
  *x = (int32_t)((vx + 32768) >> 16);
  *y = (int32_t)((vy + 32768) >> 16);
}

void TouchCal::setLinear(int32_t rxMin, int32_t rxMax, int32_t ryMin, int32_t ryMax, int32_t w, int32_t h, bool swapXY) {
  int32_t dx = rxMax - rxMin, dy = ryMax - ryMin;

  if (dx == 0 || dy == 0) {
    valid = false;
    return;
  }
  ax = bx = ay = by = 0;

  if (!swapXY) {
    ax = (int32_t)((int64_t)w * 65536 / dx);
    cx = -ax * rxMin;
    by = (int32_t)((int64_t)h * 65536 / dy);
    cy = -by * ryMin;
  } else {
    bx = (int32_t)((int64_t)w * 65536 / dy);
    cx = -bx * ryMin;
    ay = (int32_t)((int64_t)h * 65536 / dx);
    cy = -ay * rxMin;
  }
  valid = true;
}
