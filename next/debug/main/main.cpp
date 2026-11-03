// LeviLuma Diagnosegeraet (Seeed XIAO ESP32S3): beobachtet ESP-NOW-Verkehr passiv und gibt ihn im seriellen Monitor aus.
// Sendet nichts, koppelt nichts, steuert nichts. Siehe next/debug/README.md.
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "app.h"

App g_app;

uint32_t appMillis() {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

extern "C" void app_main(void) {
  g_app.lock = xSemaphoreCreateMutex();
  g_app.queue = xQueueCreate(CONFIG_LEVILUMA_DEBUG_QUEUE_LEN, sizeof(dbg::Capture));

  if (!g_app.lock || !g_app.queue) {
    ESP_LOGE("levi-dbg", "Speicher fuer Queue/Sperre fehlt");
    return;
  }
  g_app.settings.fmt.ascii = LEVI_DBG_ASCII;
  g_app.settings.fmt.full_frame = LEVI_DBG_FULL_FRAME;
  g_app.settings.filter.show_encrypted = LEVI_DBG_SHOW_ENC;

  printf("\n==== LeviLuma ESP-NOW-Diagnose ====\n"
         "Passiver Empfaenger: sendet nichts, koppelt nichts, steuert nichts.\n"
         "Grenzen: nur der eingestellte Kanal; verschluesselte Unicasts bleiben ohne Schluessel unlesbar; Rahmen koennen verloren gehen.\n");
  reportStart();
  snifferStart();
  consoleStart();
}
