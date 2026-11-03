// Host-Tests: runder Controller (round_core) – Ein-Gateway-Regel, Farbrad, Sendedrossel.
#include <math.h>
#include "test_util.h"
#include "gateway_registry.h"
#include "round_core.h"

static void test_round_single_gateway() {
  MemKvStore kv;
  GatewayRegistry r(kv);
  r.load();
  CHECK_EQ(Round::activeGateway(r), -1);
  CHECK(Round::mayPair(r));
  uint8_t a[6] = {1, 2, 3, 4, 5, 6}, b[6] = {1, 2, 3, 4, 5, 7};
  int ia = r.addDiscovered(a, 0), ib = r.addDiscovered(b, 0);
  CHECK_EQ(Round::activeGateway(r), -1); // nur erkannt, nicht gekoppelt
  r.markPaired(ib);
  CHECK_EQ(Round::activeGateway(r), ib);
  CHECK(!Round::mayPair(r)); // zweites Gateway nicht koppelbar
  GatewayRegistry r2(kv);    // Neustart: Kopplung aus dem NVS
  r2.load();
  CHECK(r2.at(Round::activeGateway(r2)).id[5] == 7);
  r.unpair(ib); // nach dem Entkoppeln ist ein anderes waehlbar
  CHECK(Round::mayPair(r));
  CHECK_EQ(r.markPaired(ia), LVP_OK);
  CHECK_EQ(Round::activeGateway(r), ia);
}

static void test_round_wheel_mapping() {
  uint8_t c[3];
  Round::wheelToRgb(0, -50, 50, c); // oben = Rot
  CHECK(c[0] == 255 && c[1] == 0 && c[2] == 0);
  Round::wheelToRgb(0, 0, 50, c); // Mitte = Weiss
  CHECK(c[0] == 255 && c[1] == 255 && c[2] == 255);
  Round::wheelToRgb(0, -500, 50, c); // ausserhalb wird auf den Rand gezogen
  CHECK(c[0] == 255 && c[1] == 0 && c[2] == 0);
  // Hin und zurueck: Position -> Farbe -> Position
  for (int k = 0; k < 24; k++) {
    float a = k * 0.2618f, rr = 10 + k * 1.5f;
    float dx = sinf(a) * rr, dy = -cosf(a) * rr, x2, y2;
    Round::wheelToRgb(dx, dy, 50, c);
    Round::rgbToWheel(c, 50, &x2, &y2);
    CHECK(fabsf(x2 - dx) < 1.5f && fabsf(y2 - dy) < 1.5f);
  }
  const uint8_t grey[3] = {40, 40, 40};
  float x, y;
  Round::rgbToWheel(grey, 50, &x, &y);
  CHECK(fabsf(x) < 0.01f && fabsf(y) < 0.01f);
}

static void test_round_throttle() {
  Round::Throttle t(100);
  CHECK(t.update(1000));  // erste Bewegung sofort
  CHECK(!t.update(1050)); // zu schnell
  CHECK(!t.update(1090));
  CHECK(t.update(1100)); // Intervall erreicht
  CHECK(!t.update(1150));
  CHECK(t.release(1160)); // letzter Wert wird beim Loslassen gesendet
  CHECK(!t.release(1170));
  CHECK(t.update(5000)); // neue Beruehrung sofort
  CHECK(!t.release(5010)); // schon gesendet: nichts nachzuschicken
}

void tests_round(void) {
  RUN(test_round_single_gateway);
  RUN(test_round_wheel_mapping);
  RUN(test_round_throttle);
}
