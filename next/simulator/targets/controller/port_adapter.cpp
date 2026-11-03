// Desktop-Port des RockBase-Controllers: Gegenstueck zu next/controller/main/lvgl_port.cpp und den
// Touch-Kalibrierfunktionen aus main/board.cpp.
//  - Touch (XPT2046) -> Maus: Die "Rohwerte" sind die Mauskoordinaten in Displaypixeln. Sie laufen wie
//    in der Firmware durch TouchCal::map(). Die gespeicherte Kalibrierung (fluechtig) ist die Identitaet;
//    die Touch-Kalibrierung der Oberflaeche funktioniert damit unveraendert.
//  - Abschaltung der Hintergrundbeleuchtung nach CONFIG_LEVILUMA_DIM_MS und "erste Beruehrung weckt nur"
//    wie in der Firmware (abschaltbar mit --no-dim).
#include "board.h"
#include "lvgl_port.h"
#include "sdkconfig.h"
#include "sdl_backend.h"
#include "sim_clock.h"
#include "simulator_app.h"

static TouchCal touchCal;
static uint32_t lastTouchMs = 0;
static bool dimmed = false, blocked = false, dimEnabled = true;
static int32_t lastRawX = 0, lastRawY = 0;
static bool lastRawPressed = false;
static bool calStored = false; // "NVS" des Simulators
static TouchCal calSaved;

static void identity(TouchCal &c) {
  static const int32_t p[3][2] = {{0, 0}, {LCD_W - 1, 0}, {0, LCD_H - 1}};
  c.solve(p, p);
}

uint32_t lvglIdleMs() {
  return simMillis() - lastTouchMs;
}

void lvglSetTouchCal(const TouchCal &cal) {
  touchCal = cal;
}

bool lvglTouchRawPressed(int32_t *rx, int32_t *ry) {
  *rx = lastRawX;
  *ry = lastRawY;
  return lastRawPressed;
}

void boardBacklight(uint8_t percent) {
  SimBackend::setBacklight(percent);
}

bool boardTouchCalLoad(TouchCal &out) {
  if (!calStored) {
    return false;
  }
  out = calSaved;
  return true;
}

bool boardTouchCalSave(const TouchCal &cal) {
  calSaved = cal;
  calStored = true;
  return true;
}

void boardTouchCalDefault(TouchCal &out) {
  identity(out); // Maus liefert bereits Displaykoordinaten
}

// wie readTouch() in main/lvgl_port.cpp
static void readTouch(lv_indev_t *, lv_indev_data_t *d) {
  SimPointer r = SimBackend::devicePointer();
  lastRawPressed = r.pressed;
  if (r.pressed) {
    lastRawX = r.x;
    lastRawY = r.y;
    lastTouchMs = simMillis();
    if (dimmed) {
      dimmed = false;
      blocked = true;
      boardBacklight(100);
    }
  } else {
    blocked = false;
  }
  int32_t x = 0, y = 0;
  touchCal.map(lastRawX, lastRawY, &x, &y);
  d->point.x = x < 0 ? 0 : x >= LCD_W ? LCD_W - 1 : x;
  d->point.y = y < 0 ? 0 : y >= LCD_H ? LCD_H - 1 : y;
  d->state = (r.pressed && !blocked) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void simControllerPortInit(lv_display_t *display, const SimOptions &o) {
  dimEnabled = o.dim;
  // Simulierter Vorzustand: Touch bereits kalibriert (ausser --first-boot)
  if (!o.firstBoot) {
    TouchCal c;
    identity(c);
    boardTouchCalSave(c);
  }
  if (!boardTouchCalLoad(touchCal)) {
    boardTouchCalDefault(touchCal);
  }
  lv_indev_t *in = lv_indev_create();
  lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(in, readTouch);
  lv_indev_set_display(in, display);
  lastTouchMs = simMillis();
  boardBacklight(100);
}

void simControllerPortLoop() {
  if (dimEnabled && !dimmed && lvglIdleMs() > CONFIG_LEVILUMA_DIM_MS) {
    dimmed = true;
    boardBacklight(0);
  }
}
