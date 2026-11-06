// LVGL-Anbindung des runden Controllers, portiert aus Legacy (legacy/src/main.cpp):
//  - synchroner Flush (pushPixels), zwei Teilpuffer 240x24
//  - Touch mit Ueberbrueckung kurzer CST816S-Aussetzer (LEVI_TOUCH_BRIDGE_MS)
//  - Beruehrung bei abgeschalteter Beleuchtung weckt nur und wird verworfen
//  - Beleuchtung nach LEVILUMA_DIM_MS ohne Beruehrung aus
// LVGL laeuft ausschliesslich in diesem Task.
#include "lvgl_port.h"
#include "board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sdkconfig.h"

static const char *TAG = "levi-lvgl";
alignas(4) static uint8_t drawBuf[LCD_W * 24 * 2];
alignas(4) static uint8_t drawBuf2[LCD_W * 24 * 2];
static uint32_t lastTouchMs = 0;
static bool screenDim = false, touchBlocked = false;
static void (*buildFn)() = nullptr;

static uint32_t tickMs() {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

uint32_t lvglIdleMs() {
  return tickMs() - lastTouchMs;
}

static void flush(lv_display_t *disp, const lv_area_t *a, uint8_t *px) {
  boardPush(a->x1, a->y1, lv_area_get_width(a), lv_area_get_height(a), (const uint16_t *)px);
  lv_display_flush_ready(disp);
}

static void readTouch(lv_indev_t *, lv_indev_data_t *d) {
  static uint32_t seenMs = 0;
  static int32_t tx = 120, ty = 120;
  int32_t x, y;
  bool raw = boardTouch(&x, &y);

  if (raw) {
    tx = x;
    ty = y;
    seenMs = tickMs();
    lastTouchMs = seenMs;
  }
  bool down = raw || (tickMs() - seenMs < CONFIG_LEVILUMA_TOUCH_BRIDGE_MS);

  if (!down) {
    touchBlocked = false;
  } else if (raw && screenDim) {
    screenDim = false;
    boardBacklight(255);
    touchBlocked = true;
  }
  d->state = (down && !touchBlocked) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  d->point.x = tx;
  d->point.y = ty;
}

static void lvglTask(void *) {
  boardInit();
  lv_init();
  lv_tick_set_cb(tickMs);
  lv_display_t *display = lv_display_create(LCD_W, LCD_H);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, drawBuf, drawBuf2, sizeof(drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, flush);
  lv_indev_t *in = lv_indev_create();
  lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(in, readTouch);
  lv_indev_set_display(in, display);

  lastTouchMs = tickMs();
  buildFn();
  lv_timer_handler();
  boardBacklight(255);
  ESP_LOGI(TAG, "LVGL %d.%d.%d laeuft", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

  for (;;) {
    uint32_t wait = lv_timer_handler();

    if (!screenDim && lvglIdleMs() > CONFIG_LEVILUMA_DIM_MS) {
      screenDim = true;
      boardBacklight(0);
    }
    vTaskDelay(pdMS_TO_TICKS(wait > 10 ? 10 : (wait ? wait : 1)));
  }
}

void lvglPortStart(void (*buildUi)()) {
  buildFn = buildUi;
  xTaskCreatePinnedToCore(lvglTask, "levi_lvgl", 12288, nullptr, 4, nullptr, 1);
}
