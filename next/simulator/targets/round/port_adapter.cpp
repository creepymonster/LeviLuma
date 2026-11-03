// Desktop-Port des runden Controllers: Gegenstueck zu next/controller-round/main/lvgl_port.cpp.
//  - Touch (CST816S) -> Maus, Beruehrungen beginnen nur innerhalb des Displaykreises (SdlBackend).
//  - Ueberbrueckung kurzer Touch-Aussetzer (CONFIG_LEVILUMA_TOUCH_BRIDGE_MS), Wecken bei abgeschalteter
//    Beleuchtung und Abschaltung nach CONFIG_LEVILUMA_DIM_MS wie in der Firmware (--no-dim schaltet ab).
// Helligkeit der Beleuchtung: Firmware 0..255 (boardBacklight), hier in Prozent an das Fenster.
#include "board.h"
#include "lvgl_port.h"
#include "sdkconfig.h"
#include "sdl_backend.h"
#include "sim_clock.h"
#include "simulator_app.h"

static uint32_t lastTouchMs = 0;
static bool screenDim = false, touchBlocked = false, dimEnabled = true;

uint32_t lvglIdleMs() {
  return simMillis() - lastTouchMs;
}

static void backlight(uint8_t value) {
  SimBackend::setBacklight(value * 100 / 255);
}

// wie readTouch() in main/lvgl_port.cpp
static void readTouch(lv_indev_t *, lv_indev_data_t *d) {
  static uint32_t seenMs = 0;
  static int32_t tx = LCD_W / 2, ty = LCD_H / 2;
  SimPointer p = SimBackend::devicePointer();
  bool raw = p.pressed;

  if (raw) {
    tx = p.x;
    ty = p.y;
    seenMs = simMillis();
    lastTouchMs = seenMs;
  }
  bool down = raw || (simMillis() - seenMs < CONFIG_LEVILUMA_TOUCH_BRIDGE_MS);

  if (!down) {
    touchBlocked = false;
  } else if (raw && screenDim) {
    screenDim = false;
    backlight(255);
    touchBlocked = true;
  }
  d->state = (down && !touchBlocked) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  d->point.x = tx;
  d->point.y = ty;
}

void simRoundPortInit(lv_display_t *display, const SimOptions &o) {
  dimEnabled = o.dim;
  lv_indev_t *in = lv_indev_create();
  lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(in, readTouch);
  lv_indev_set_display(in, display);
  lastTouchMs = simMillis();
  backlight(255);
}

void simRoundPortLoop() {
  if (dimEnabled && !screenDim && lvglIdleMs() > CONFIG_LEVILUMA_DIM_MS) {
    screenDim = true;
    backlight(0);
  }
}
