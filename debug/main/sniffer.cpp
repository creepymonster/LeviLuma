// Funkseite: WLAN-Promiscuous-Empfang (und optional ESP-NOW-Callback), Kanalwechsel, Suchlauf.
// Die Callbacks laufen im WLAN-Task: nur pruefen, begrenzt kopieren, in die Queue stellen. Keine Formatierung, keine Ausgabe.
// Das Geraet sendet nie: kein esp_now_send, kein esp_now_add_peer, kein esp_wifi_connect, kein esp_wifi_scan_start.
#include "freertos/FreeRTOS.h"
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "app.h"
#if CONFIG_LEVILUMA_DEBUG_ESPNOW_CB
#include "esp_now.h"
#endif

static const char *TAG = "levi-dbg-rf";

static void enqueue(dbg::Capture &c) {
  if (xQueueSend(g_app.queue, &c, 0) == pdTRUE) {
    g_app.stats.queued++;
  } else {
    g_app.stats.queue_drops++; // sichtbar im Befehl "stats"
  }
}

// Management-Rahmen aller Geraete auf dem Kanal. Laut ESP-IDF-Doku laeuft der Callback direkt im WLAN-Task.
static void promiscCb(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT || !buf) {
    return;
  }
  const wifi_promiscuous_pkt_t *p = (const wifi_promiscuous_pkt_t *)buf;
  g_app.stats.mgmt_seen++;
  size_t sig = p->rx_ctrl.sig_len; // Laenge des Rahmens inkl. FCS
  size_t n = sig < dbg::MAX_CAPTURE ? sig : dbg::MAX_CAPTURE;

  if (!dbg::is_espnow_candidate(p->payload, n)) {
    g_app.stats.ignored++; // Beacons, Probe-Requests usw.
    return;
  }
  dbg::Capture c;
  c.source = dbg::Source::Promiscuous;
  c.t_ms = appMillis();
  c.has_rx_us = true;
  c.rx_us = p->rx_ctrl.timestamp;
  c.has_rssi = true;
  c.rssi = (int8_t)p->rx_ctrl.rssi;
  c.channel = (uint8_t)p->rx_ctrl.channel;
  c.fcs_included = true;
  c.orig_len = (uint16_t)(sig > 0xFFFF ? 0xFFFF : sig);
  c.len = (uint16_t)n;
  c.has_addr = false;

  if (n < sig) {
    g_app.stats.rx_cut++;
  }
  memcpy(c.data, p->payload, n);
  enqueue(c);
}

#if CONFIG_LEVILUMA_DEBUG_ESPNOW_CB
// Vergleichspfad: normaler ESP-NOW-Empfang (nur Broadcast und an die eigene MAC gerichtet, entschluesselt nur mit Peer+Schluessel,
// den dieses Geraet nie eintraegt). Liefert Nutzdaten ohne 802.11-Kopf.
static void espnowCb(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  g_app.stats.cb_seen++;

  if (!info || !data || len <= 0 || !info->src_addr || !info->des_addr) {
    return;
  }
  size_t n = (size_t)len < dbg::MAX_CAPTURE ? (size_t)len : dbg::MAX_CAPTURE;
  dbg::Capture c;
  c.source = dbg::Source::EspNowCallback;
  c.t_ms = appMillis();
  c.has_rx_us = false;
  c.rx_us = 0;
  c.has_rssi = info->rx_ctrl != nullptr;
  c.rssi = info->rx_ctrl ? (int8_t)info->rx_ctrl->rssi : 0;
  c.channel = info->rx_ctrl ? (uint8_t)info->rx_ctrl->channel : 0;
  c.fcs_included = false;
  c.orig_len = (uint16_t)len;
  c.len = (uint16_t)n;
  c.has_addr = true;
  memcpy(c.src, info->src_addr, 6);
  memcpy(c.dst, info->des_addr, 6);

  if (n < (size_t)len) {
    g_app.stats.rx_cut++;
  }
  memcpy(c.data, data, n);
  enqueue(c);
}
#endif

bool snifferSetChannel(uint8_t ch) {
  xSemaphoreTake(g_app.lock, portMAX_DELAY);
  bool ok = g_app.settings.plan.valid(ch);
  xSemaphoreGive(g_app.lock);

  if (!ok) {
    return false;
  }
  esp_err_t e = esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

  if (e != ESP_OK) {
    ESP_LOGW(TAG, "Kanal %u abgelehnt (%s)", ch, esp_err_to_name(e));
    return false;
  }
  xSemaphoreTake(g_app.lock, portMAX_DELAY);
  g_app.settings.plan.current = ch;
  xSemaphoreGive(g_app.lock);
  return true;
}

#if CONFIG_LEVILUMA_DEBUG_SCAN_ENABLE
static void scanTask(void *) {
  for (;;) {
    xSemaphoreTake(g_app.lock, portMAX_DELAY);
    bool on = g_app.settings.plan.scanning;
    uint32_t dwell = g_app.settings.plan.dwell_ms;
    uint8_t next = g_app.settings.plan.next_after(g_app.settings.plan.current);
    xSemaphoreGive(g_app.lock);

    if (!on) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    snifferSetChannel(next);
    vTaskDelay(pdMS_TO_TICKS(dwell));
  }
}
#endif

void snifferScan(bool on, uint32_t dwell_ms) {
#if CONFIG_LEVILUMA_DEBUG_SCAN_ENABLE
  xSemaphoreTake(g_app.lock, portMAX_DELAY);
  g_app.settings.plan.scanning = on;
  if (on && dwell_ms) {
    g_app.settings.plan.dwell_ms = dwell_ms;
  }
  xSemaphoreGive(g_app.lock);
#else
  (void)on;
  (void)dwell_ms;
#endif
}

void snifferStart() {
  ESP_ERROR_CHECK(esp_netif_init());
  esp_err_t e = esp_event_loop_create_default();

  if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(e);
  }
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); // nur Station ohne Verbindung; es wird nie esp_wifi_connect aufgerufen
  ESP_ERROR_CHECK(esp_wifi_start());

  if (CONFIG_LEVILUMA_DEBUG_COUNTRY[0]) {
    esp_err_t ce = esp_wifi_set_country_code(CONFIG_LEVILUMA_DEBUG_COUNTRY, false);

    if (ce != ESP_OK) {
      ESP_LOGW(TAG, "Laendercode \"%s\" nicht uebernommen (%s)", CONFIG_LEVILUMA_DEBUG_COUNTRY, esp_err_to_name(ce));
    }
  }
  // Zulaessige Kanaele laut Funktreiber (Laenderregeln)
  wifi_country_t country;
  xSemaphoreTake(g_app.lock, portMAX_DELAY);
  dbg::ChannelPlan &plan = g_app.settings.plan;

  if (esp_wifi_get_country(&country) == ESP_OK && country.nchan > 0) {
    plan.first = country.schan < 1 ? 1 : country.schan;
    unsigned last = (unsigned)country.schan + country.nchan - 1;
    plan.last = last > 14 ? 14 : (uint8_t)last;
    printf("Laenderregeln des Funktreibers: Land \"%.2s\", Kanaele %u..%u\n", country.cc, plan.first, plan.last);
  } else {
    plan.first = 1;
    plan.last = 11;
    printf("Laenderregeln nicht lesbar, nehme vorsichtig Kanaele 1..11 an\n");
  }
  plan.scan_available = LEVI_DBG_SCAN;
  plan.dwell_ms =
#if CONFIG_LEVILUMA_DEBUG_SCAN_ENABLE
      CONFIG_LEVILUMA_DEBUG_SCAN_DWELL_MS;
#else
      500;
#endif
  plan.stay_on_hit =
#if CONFIG_LEVILUMA_DEBUG_SCAN_STAY_ON_HIT
      true;
#else
      false;
#endif
  plan.current = plan.first;
  xSemaphoreGive(g_app.lock);

#if CONFIG_LEVILUMA_DEBUG_CHANNEL_FOLLOW
  int want = CONFIG_LEVILUMA_ESPNOW_CHANNEL;
#else
  int want = CONFIG_LEVILUMA_DEBUG_CHANNEL;
#endif
  xSemaphoreTake(g_app.lock, portMAX_DELAY);
  bool allowed = plan.valid(want);
  uint8_t first = plan.first;
  xSemaphoreGive(g_app.lock);

  if (!allowed || !snifferSetChannel((uint8_t)want)) {
    printf("WARNUNG: Kanal %d ist nach den Laenderregeln nicht zulaessig; weiche auf Kanal %u aus.\n", want, first);
    if (!snifferSetChannel(first)) {
      printf("FEHLER: Kanal konnte nicht gesetzt werden.\n");
    }
  }
  wifi_promiscuous_filter_t filt = {};
  filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT; // ESP-NOW wird als Management-Action-Rahmen gesendet; Daten-/Steuerrahmen irrelevant
  ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filt));
  ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(promiscCb));
  ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));

#if CONFIG_LEVILUMA_DEBUG_ESPNOW_CB
  ESP_ERROR_CHECK(esp_now_init());
  ESP_ERROR_CHECK(esp_now_register_recv_cb(espnowCb)); // keine Peers, kein Senden
  printf("Zusaetzlicher Empfangsweg: ESP-NOW-Callback (Doppelte werden unterdrueckt)\n");
#endif
#if CONFIG_LEVILUMA_DEBUG_SCAN_ENABLE
  xTaskCreate(scanTask, "levi_scan", 3072, nullptr, 3, nullptr);
  printf("Kanalsuchlauf erlaubt (Befehl \"scan on\"); er verpasst Nachrichten auf den uebrigen Kanaelen.\n");
#if CONFIG_LEVILUMA_DEBUG_SCAN_AT_BOOT
  snifferScan(true, 0);
#endif
#endif
  xSemaphoreTake(g_app.lock, portMAX_DELAY);
  uint8_t cur = plan.current;
  xSemaphoreGive(g_app.lock);
  printf("Promiscuous-Empfang aktiv auf Kanal %u. \"help\" zeigt die Befehle.\n", cur);
}
