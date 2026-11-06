// Kopplungstaste (BOOT, GPIO0) und Status-LED (GPIO21) des XIAO ESP32S3.
// Taste kurz druecken: Kopplungsfenster fuer 300 s oeffnen (nur ohne Schluessel noetig).
// LED: blinkt bei offenem Kopplungsfenster, leuchtet bei BLE-Verbindung, sonst aus.
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "gateway.h"

static void setLed(bool on) {
#if CONFIG_LEVILUMA_STATUS_LED_GPIO >= 0
  bool level = CONFIG_LEVILUMA_STATUS_LED_ACTIVE_LOW ? !on : on;
  gpio_set_level((gpio_num_t)CONFIG_LEVILUMA_STATUS_LED_GPIO, level ? 1 : 0);
#else
  (void)on;
#endif
}

static void ioTask(void *) {
  int pressedMs = 0;
  int tick = 0;

  for (;;) {
#if CONFIG_LEVILUMA_PAIR_BUTTON_GPIO >= 0
    if (gpio_get_level((gpio_num_t)CONFIG_LEVILUMA_PAIR_BUTTON_GPIO) == 0) {
      pressedMs += 50;
      if (pressedMs == 100) { // entprellt
        gwOpenPairingWindow(CONFIG_LEVILUMA_PAIRING_WINDOW_S > 0 ? CONFIG_LEVILUMA_PAIRING_WINDOW_S : 300);
      }
    } else {
      pressedMs = 0;
    }
#endif
    xSemaphoreTake(gw.lock, portMAX_DELAY);
    bool window = gw.pairing->windowOpen(gwMillis()) && !gw.hasKey;
    bool ble = gw.light->snapshot().ble;
    xSemaphoreGive(gw.lock);
    tick++;
    bool blink = (tick / 5) % 2 == 0; // 250 ms an, 250 ms aus
    setLed(window ? blink : ble);
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void statusIoStart() {
#if CONFIG_LEVILUMA_PAIR_BUTTON_GPIO >= 0
  gpio_config_t b = {};
  b.pin_bit_mask = 1ULL << CONFIG_LEVILUMA_PAIR_BUTTON_GPIO;
  b.mode = GPIO_MODE_INPUT;
  b.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&b);
#endif
#if CONFIG_LEVILUMA_STATUS_LED_GPIO >= 0
  gpio_config_t l = {};
  l.pin_bit_mask = 1ULL << CONFIG_LEVILUMA_STATUS_LED_GPIO;
  l.mode = GPIO_MODE_OUTPUT;
  gpio_config(&l);
  setLed(false);
#endif
  xTaskCreate(ioTask, "levi_io", 3072, nullptr, 2, nullptr);
}
