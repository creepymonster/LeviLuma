// LeviLuma Next Controller (RockBase NM-CYD-C5, ESP32-C5).
// Steuert BLE-LED-Leisten ausschliesslich per ESP-NOW ueber XIAO-ESP32S3-Gateways.
// Der LightState jedes Gateways ist dort autoritativ; der Controller zeigt nur bestaetigte Zustaende.
#include "app.h"
#include "esp_log.h"
#include "lvgl_port.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "ui.h"
#if CONFIG_LEVILUMA_ZIGBEE_ENABLED
#include "zigbee_bridge.h"
#endif

static const char *TAG = "levi-ctl";

extern "C" void app_main(void) {
#if CONFIG_LEVILUMA_ZIGBEE_ENABLED
  ESP_LOGI(TAG, "LeviLuma Controller startet (Zigbee aktiv)");
#else
  ESP_LOGI(TAG, "LeviLuma Controller startet (Zigbee nicht enthalten)");
#endif
  esp_err_t e = nvs_flash_init();

  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS: neu formatieren (%s)", esp_err_to_name(e));
    ESP_ERROR_CHECK(nvs_flash_erase());
    e = nvs_flash_init();
  }
  ESP_ERROR_CHECK(e);

  appInit();
  appStart();
#if CONFIG_LEVILUMA_ZIGBEE_ENABLED
  zbStart();
#endif
  lvglPortStart(uiBuild);
}
