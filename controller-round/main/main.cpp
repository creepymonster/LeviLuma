// LeviLuma Next – temporaerer runder Controller (Waveshare ESP32-S3-Touch-LCD-1.28).
// Steuert genau ein Next-Gateway per ESP-NOW; das Gateway haelt den autoritativen LightState.
// Kein BLE, keine WLAN-Verbindung, kein Zigbee, kein Webserver.
#include "app.h"
#include "esp_log.h"
#include "lvgl_port.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "ui.h"

#if CONFIG_BT_ENABLED
#error "Der runde Controller darf kein BLE enthalten (CONFIG_BT_ENABLED=n)"
#endif

static const char *TAG = "levi-rund";

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "LeviLuma Round Controller startet");
  esp_err_t e = nvs_flash_init();
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS: neu formatieren (%s)", esp_err_to_name(e));
    ESP_ERROR_CHECK(nvs_flash_erase());
    e = nvs_flash_init();
  }
  ESP_ERROR_CHECK(e);
  appInit();
  appStart();
  lvglPortStart(uiBuild);
}
