#include "lvgl_port.h"
#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "levi-lvgl";
static lv_display_t *display = nullptr;
static TouchCal touchCal;
static volatile uint32_t lastTouchMs = 0;
static bool dimmed = false, blocked = false;
static int32_t lastRawX = 0, lastRawY = 0;
static bool lastRawPressed = false;
static void (*buildFn)() = nullptr;

static uint32_t tickMs() {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

uint32_t lvglIdleMs() {
  return tickMs() - lastTouchMs;
}

void lvglSetTouchCal(const TouchCal &cal) {
  touchCal = cal;
}

bool lvglTouchRawPressed(int32_t *rx, int32_t *ry) {
  *rx = lastRawX;
  *ry = lastRawY;
  return lastRawPressed;
}

// Ende einer DMA-Uebertragung (ISR): LVGL darf den Puffer wieder beschreiben
static bool onFlushDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *ctx) {
  lv_display_flush_ready((lv_display_t *)ctx);
  return false;
}

static void flush(lv_display_t *disp, const lv_area_t *a, uint8_t *px) {
  uint32_t n = lv_area_get_width(a) * lv_area_get_height(a);
  lv_draw_rgb565_swap(px, n); // ST7789 erwartet High-Byte zuerst
  esp_lcd_panel_draw_bitmap(boardPanel(), a->x1, a->y1, a->x2 + 1, a->y2 + 1, px);
}

// Touch: eine Beruehrung bei abgeschalteter Beleuchtung weckt nur das Display und wird nicht
// an die Oberflaeche gegeben, bis der Finger wieder oben ist (wie Legacy).
static void readTouch(lv_indev_t *, lv_indev_data_t *d) {
  TouchRaw r = boardTouchRaw();
  lastRawPressed = r.pressed;

  if (r.pressed) {
    lastRawX = r.x;
    lastRawY = r.y;
    lastTouchMs = tickMs();

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

static void lvglTask(void *) {
  lv_init();
  lv_tick_set_cb(tickMs);
  display = lv_display_create(LCD_W, LCD_H);
  boardInit(onFlushDone, display);
  size_t bufBytes = LCD_W * 40 * 2;
  void *b1 = heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void *b2 = heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  configASSERT(b1 && b2);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, b1, b2, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, flush);

  if (!boardTouchCalLoad(touchCal)) {
    boardTouchCalDefault(touchCal);
  }
  lv_indev_t *in = lv_indev_create();
  lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(in, readTouch);
  lv_indev_set_display(in, display);

  lastTouchMs = tickMs();
  buildFn();
  lv_timer_handler();
  boardBacklight(100);
  ESP_LOGI(TAG, "LVGL %d.%d.%d laeuft", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

  for (;;) {
    uint32_t wait = lv_timer_handler();

    if (!dimmed && lvglIdleMs() > CONFIG_LEVILUMA_DIM_MS) {
      dimmed = true;
      boardBacklight(0);
    }
    if (wait > 20) {
      wait = 20;
    }
    vTaskDelay(pdMS_TO_TICKS(wait ? wait : 1));
  }
}

void lvglPortStart(void (*buildUi)()) {
  buildFn = buildUi;
  xTaskCreate(lvglTask, "levi_lvgl", 12288, nullptr, 4, nullptr);
}
