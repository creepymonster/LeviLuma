// ESP-NOW-Anbindung des Gateways: Empfang (Callback -> Queue -> Link-Task), Antworten,
// Beacons (ANNOUNCE) und Zustandsmeldungen an gekoppelte Controller.
#include "freertos/FreeRTOS.h"
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "gateway.h"

static const char *TAG = "levi-gw-link";
static const uint8_t BCAST[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct RxFrame {
  uint8_t src[ESP_NOW_ETH_ALEN];
  bool broadcast;
  uint8_t len;
  uint8_t data[LVP_MAX_FRAME];
};

static QueueHandle_t rxQueue;

// Laeuft im WLAN-Task: nur kopieren, keine Verarbeitung.
static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (!info || !data || len <= 0 || len > LVP_MAX_FRAME) {
    return;
  }
  RxFrame f;
  memcpy(f.src, info->src_addr, ESP_NOW_ETH_ALEN);
  f.broadcast = info->des_addr && memcmp(info->des_addr, BCAST, ESP_NOW_ETH_ALEN) == 0;
  f.len = (uint8_t)len;
  memcpy(f.data, data, len);

  if (xQueueSend(rxQueue, &f, 0) != pdTRUE) {
    // Queue voll: Rahmen verwerfen, der Controller wiederholt
  }
}

static void onSent(const esp_now_send_info_t *, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) {
    ESP_LOGD(TAG, "Senden ohne MAC-Bestaetigung");
  }
}

// Peer eintragen oder aktualisieren. Paired Controller und Erkennungs-Absender bekommen die LMK,
// wenn ein Schluessel gesetzt ist; sonst unverschluesselt.
static bool ensurePeer(const uint8_t mac[ESP_NOW_ETH_ALEN]) {
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, mac, ESP_NOW_ETH_ALEN);
  p.channel = 0; // aktueller Kanal
  p.ifidx = WIFI_IF_STA;
  p.encrypt = gw.hasKey;

  if (gw.hasKey) {
    memcpy(p.lmk, gw.key, ESP_NOW_KEY_LEN);
  }
  esp_err_t e = esp_now_is_peer_exist(mac) ? esp_now_mod_peer(&p) : esp_now_add_peer(&p);

  if (e == ESP_ERR_ESPNOW_FULL) {
    // Platz schaffen: einen nicht gekoppelten Peer entfernen
    esp_now_peer_info_t it;
    bool from_head = true;

    while (esp_now_fetch_peer(from_head, &it) == ESP_OK) {
      from_head = false;
      xSemaphoreTake(gw.lock, portMAX_DELAY);
      bool paired = gw.pairing->contains(it.peer_addr);
      xSemaphoreGive(gw.lock);

      if (!paired && memcmp(it.peer_addr, BCAST, ESP_NOW_ETH_ALEN) != 0 && memcmp(it.peer_addr, mac, ESP_NOW_ETH_ALEN) != 0) {
        esp_now_del_peer(it.peer_addr);
        e = esp_now_add_peer(&p);
        break;
      }
    }
  }
  if (e != ESP_OK) {
    ESP_LOGW(TAG, "Peer eintragen fehlgeschlagen (%s)", esp_err_to_name(e));
  }
  return e == ESP_OK;
}

static void sendTo(const uint8_t *mac, const uint8_t *data, size_t len) {
  if (!len) {
    return;
  }
  esp_err_t e = esp_now_send(mac, data, len);

  if (e != ESP_OK) {
    ESP_LOGW(TAG, "esp_now_send fehlgeschlagen (%s)", esp_err_to_name(e));
  }
}

// Unaufgeforderte Zustandsmeldung an alle gekoppelten Controller
static void notifyPaired() {
  uint8_t buf[LVP_MAX_FRAME];
  uint8_t macs[PairingTable::MAX][LVP_ID_LEN];
  int n;
  xSemaphoreTake(gw.lock, portMAX_DELAY);
  size_t len = gw.handler->buildStateNotify(buf, sizeof(buf));
  n = gw.pairing->count();
  for (int i = 0; i < n; i++) {
    memcpy(macs[i], gw.pairing->at(i).mac, LVP_ID_LEN);
  }
  xSemaphoreGive(gw.lock);

  for (int i = 0; i < n; i++) {
    sendTo(macs[i], buf, len);
  }
}

static void addPairedPeers() {
  xSemaphoreTake(gw.lock, portMAX_DELAY);
  int n = gw.pairing->count();
  uint8_t macs[PairingTable::MAX][LVP_ID_LEN];
  for (int i = 0; i < n; i++) {
    memcpy(macs[i], gw.pairing->at(i).mac, LVP_ID_LEN);
  }
  xSemaphoreGive(gw.lock);

  for (int i = 0; i < n; i++) {
    ensurePeer(macs[i]);
  }
}

static void linkTask(void *) {
  uint32_t lastBeacon = 0, lastNotify = 0;
  bool notifyPending = false;
  uint8_t buf[LVP_MAX_FRAME];

  for (;;) {
    RxFrame f;

    if (xQueueReceive(rxQueue, &f, pdMS_TO_TICKS(50)) == pdTRUE) {
      GatewayHandler::Output out;
      xSemaphoreTake(gw.lock, portMAX_DELAY);
      gw.handler->handle(f.src, f.broadcast, f.data, f.len, gwMillis(), out);
      xSemaphoreGive(gw.lock);

      if (out.ensurePeer) {
        ensurePeer(f.src);
      }
      if (out.replyLen) {
        sendTo(out.replyBroadcast ? BCAST : f.src, out.reply, out.replyLen);
      }
      if (out.removePeer) {
        xSemaphoreTake(gw.lock, portMAX_DELAY);
        gw.handler->forgetPeer(f.src);
        xSemaphoreGive(gw.lock);
        esp_now_del_peer(f.src);
        ESP_LOGI(TAG, "Controller entkoppelt");
      }
      if (out.pairedChanged && !out.removePeer) {
        ESP_LOGI(TAG, "Controller gekoppelt");
      }
      if (out.stateChanged) {
        notifyPending = true; // andere Controller erfahren die Aenderung
      }
      if (out.dropReason) {
        ESP_LOGD(TAG, "Rahmen verworfen (%s)", lvp_err_name(out.dropReason));
      }
    }

    EventBits_t ev = xEventGroupClearBits(gw.events, GW_EVT_STATE_CHANGED | GW_EVT_PAIRING_OPENED | GW_EVT_KEY_CHANGED);

    if (ev & GW_EVT_KEY_CHANGED) {
      addPairedPeers(); // Peers mit neuer LMK (oder ohne) neu eintragen
    }
    if (ev & GW_EVT_STATE_CHANGED) {
      notifyPending = true;
    }
    uint32_t now = gwMillis();

    // Zustandsmeldungen hoechstens alle 100 ms
    if (notifyPending && now - lastNotify >= 100) {
      notifyPending = false;
      lastNotify = now;
      notifyPaired();
    }
    if (now - lastBeacon >= CONFIG_LEVILUMA_BEACON_MS || (ev & GW_EVT_PAIRING_OPENED)) {
      lastBeacon = now;
      xSemaphoreTake(gw.lock, portMAX_DELAY);
      size_t len = gw.handler->buildAnnounce(buf, sizeof(buf), now);
      xSemaphoreGive(gw.lock);
      sendTo(BCAST, buf, len);
    }
  }
}

void linkStart() {
  rxQueue = xQueueCreate(8, sizeof(RxFrame));
  ESP_ERROR_CHECK(esp_netif_init());
  esp_err_t e = esp_event_loop_create_default();

  if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(e);
  }
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LEVILUMA_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

  ESP_ERROR_CHECK(esp_now_init());
  ESP_ERROR_CHECK(esp_now_register_recv_cb(onRecv));
  ESP_ERROR_CHECK(esp_now_register_send_cb(onSent));
  static_assert(sizeof(CONFIG_LEVILUMA_ESPNOW_PMK) - 1 == ESP_NOW_KEY_LEN, "LEVILUMA_ESPNOW_PMK muss genau 16 Zeichen haben");
  ESP_ERROR_CHECK(esp_now_set_pmk((const uint8_t *)CONFIG_LEVILUMA_ESPNOW_PMK));

  esp_now_peer_info_t b = {};
  memcpy(b.peer_addr, BCAST, ESP_NOW_ETH_ALEN);
  b.channel = 0;
  b.ifidx = WIFI_IF_STA;
  b.encrypt = false;
  ESP_ERROR_CHECK(esp_now_add_peer(&b));
  addPairedPeers();
  ESP_LOGI(TAG, "ESP-NOW aktiv auf Kanal %d", CONFIG_LEVILUMA_ESPNOW_CHANNEL);
  xTaskCreate(linkTask, "levi_link", 6144, nullptr, 5, nullptr);
}
