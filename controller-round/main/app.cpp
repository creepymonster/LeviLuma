// ESP-NOW-Dienst des runden Controllers. Uebernimmt den Aufbau von controller/main/app.cpp
// (Radio-Implementierung, Link-Task, threadsichere Aktionen), mit zwei Unterschieden:
//  - kein esp_netif: nur der WLAN-Treiber im STA-Modus fuer ESP-NOW, kein TCP/IP, keine Verbindung,
//    kein Scan nach Access Points
//  - genau ein Gateway (Round::mayPair / Round::activeGateway)
#include "app.h"
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "kv_store.h"
#include "round_core.h"
#include "sdkconfig.h"

static const char *TAG = "levi-app";
static const uint8_t BCAST[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

class EspNowRadio : public Radio {
public:
  bool send(const uint8_t mac[LVP_ID_LEN], const uint8_t *data, size_t len) override {
    esp_err_t e = esp_now_send(mac, data, len);
    if (e != ESP_OK) {
      ESP_LOGW(TAG, "esp_now_send: %s", esp_err_to_name(e));
    }
    return e == ESP_OK;
  }
  bool broadcast(const uint8_t *data, size_t len) override {
    return send(BCAST, data, len);
  }
  bool setPeer(const uint8_t mac[LVP_ID_LEN], const uint8_t *lmk) override {
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, mac, ESP_NOW_ETH_ALEN);
    p.ifidx = WIFI_IF_STA;
    p.encrypt = lmk != nullptr;
    if (lmk) {
      memcpy(p.lmk, lmk, ESP_NOW_KEY_LEN);
    }
    esp_err_t e = esp_now_is_peer_exist(mac) ? esp_now_mod_peer(&p) : esp_now_add_peer(&p);
    if (e != ESP_OK) {
      ESP_LOGW(TAG, "Peer eintragen: %s", esp_err_to_name(e));
    }
    return e == ESP_OK;
  }
  void delPeer(const uint8_t mac[LVP_ID_LEN]) override {
    if (esp_now_is_peer_exist(mac)) {
      esp_now_del_peer(mac);
    }
  }
};

struct RxFrame {
  uint8_t src[ESP_NOW_ETH_ALEN];
  bool broadcast;
  uint8_t len;
  uint8_t data[LVP_MAX_FRAME];
};

// Diagnose im Log: Anfragen mit Versuch und Wartezeit, Antworten, Timeouts (laeuft im Link-Task unter appLock)
class LogTrace : public CtrlLink::Trace {
public:
  void onSend(const uint8_t peer[LVP_ID_LEN], uint8_t type, uint16_t seq, uint8_t tryNo, uint8_t maxTries, uint32_t waitMs) override {
    char id[18];
    lvp_id_to_str(peer, id, sizeof(id));
    ESP_LOGI(TAG, "%s an %s (Seq %u): Versuch %u/%u, warte %lu ms", lvp_type_name(type), id, seq, tryNo, maxTries, (unsigned long)waitMs);
  }
  void onAnswer(const uint8_t peer[LVP_ID_LEN], uint8_t reqType, uint8_t rspType, uint8_t error, uint16_t seq, uint8_t tries,
                uint32_t elapsedMs) override {
    char id[18];
    lvp_id_to_str(peer, id, sizeof(id));
    if (rspType == LVP_MSG_NACK) {
      ESP_LOGW(TAG, "%s an %s (Seq %u): abgelehnt (%s) nach %u Versuch(en), %lu ms", lvp_type_name(reqType), id, seq, lvp_err_name(error), tries,
               (unsigned long)elapsedMs);
    } else {
      ESP_LOGI(TAG, "%s an %s (Seq %u): Antwort %s nach %u Versuch(en), %lu ms", lvp_type_name(reqType), id, seq, lvp_type_name(rspType), tries,
               (unsigned long)elapsedMs);
    }
  }
  void onTimeout(const uint8_t peer[LVP_ID_LEN], uint8_t type, uint16_t seq, uint8_t tries, uint32_t elapsedMs) override {
    char id[18];
    lvp_id_to_str(peer, id, sizeof(id));
    ESP_LOGW(TAG, "%s an %s (Seq %u): keine Antwort nach %u Versuchen, %lu ms", lvp_type_name(type), id, seq, tries, (unsigned long)elapsedMs);
  }
};

// Ergebnis der MAC-Bestaetigung eines Unicast-Rahmens (aus dem Sende-Callback, Ausgabe im Link-Task)
struct TxResult {
  uint8_t dst[ESP_NOW_ETH_ALEN];
  bool ok;
};

static SemaphoreHandle_t lock;
static QueueHandle_t rxQueue;
static QueueHandle_t txQueue;
static LogTrace logTrace;
static NvsKvStore *kv;
static GatewayRegistry *reg;
static EspNowRadio radio;
static CtrlLink *link;

uint32_t appMillis() {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

static void appLock() {
  xSemaphoreTakeRecursive(lock, portMAX_DELAY);
}

static void appUnlock() {
  xSemaphoreGiveRecursive(lock);
}

static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (!info || !data || len <= 0 || len > LVP_MAX_FRAME) {
    return;
  }
  RxFrame f;
  memcpy(f.src, info->src_addr, ESP_NOW_ETH_ALEN);
  f.broadcast = info->des_addr && memcmp(info->des_addr, BCAST, ESP_NOW_ETH_ALEN) == 0;
  f.len = (uint8_t)len;
  memcpy(f.data, data, len);
  xQueueSend(rxQueue, &f, 0); // bei voller Queue verwerfen: Wiederholung und Beacons gleichen aus
}

// Laeuft im WLAN-Task: nur kopieren, keine Ausgabe. Broadcasts haben keine MAC-Bestaetigung.
static void onSent(const esp_now_send_info_t *info, esp_now_send_status_t status) {
  if (!info || !info->des_addr || memcmp(info->des_addr, BCAST, ESP_NOW_ETH_ALEN) == 0) {
    return;
  }
  TxResult r;
  memcpy(r.dst, info->des_addr, ESP_NOW_ETH_ALEN);
  r.ok = status == ESP_NOW_SEND_SUCCESS;
  xQueueSend(txQueue, &r, 0); // bei voller Queue verwerfen (nur Diagnose)
}

static void logTxResults() {
  TxResult r;
  while (xQueueReceive(txQueue, &r, 0) == pdTRUE) {
    char id[18];
    lvp_id_to_str(r.dst, id, sizeof(id));
    if (r.ok) {
      ESP_LOGD(TAG, "Rahmen an %s: MAC-Bestaetigung erhalten", id);
    } else {
      ESP_LOGW(TAG, "Rahmen an %s: keine MAC-Bestaetigung (Gateway hat den Rahmen nicht empfangen)", id);
    }
  }
}

static void linkTask(void *) {
  for (;;) {
    logTxResults();
    RxFrame f;
    bool got = xQueueReceive(rxQueue, &f, pdMS_TO_TICKS(20)) == pdTRUE;
    appLock();
    uint32_t now = appMillis();
    if (got) {
      link->onFrame(f.src, f.broadcast, f.data, f.len, now);
    }
    link->tick(now);
    appUnlock();
  }
}

void appInit() {
  lock = xSemaphoreCreateRecursiveMutex();
  rxQueue = xQueueCreate(12, sizeof(RxFrame));
  txQueue = xQueueCreate(8, sizeof(TxResult));
  kv = new NvsKvStore("levirund");
  reg = new GatewayRegistry(*kv);
  reg->load();
  CtrlLink::Config c;
  ESP_ERROR_CHECK(esp_read_mac(c.ownId, ESP_MAC_WIFI_STA));
  strncpy(c.ownName, CONFIG_LEVILUMA_CTRL_NAME, LVP_NAME_MAX);
  c.session = esp_random() | 1;
  c.timeoutMs = CONFIG_LEVILUMA_REQUEST_TIMEOUT_MS;
  c.tries = CONFIG_LEVILUMA_REQUEST_TRIES;
  c.beaconMs = CONFIG_LEVILUMA_BEACON_MS;
  link = new CtrlLink(*reg, radio, c);
  link->setTrace(&logTrace);
  char idStr[18];
  lvp_id_to_str(c.ownId, idStr, sizeof(idStr));
  ESP_LOGI(TAG, "Controller-ID %s, Gateway %s", idStr, Round::activeGateway(*reg) >= 0 ? "gekoppelt" : "noch nicht gekoppelt");
}

void appStart() {
  // Nur das WLAN-Funkteil fuer ESP-NOW: kein esp_netif (kein TCP/IP), kein esp_wifi_connect, kein Scan.
  esp_err_t e = esp_event_loop_create_default();
  if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(e);
  }
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(CONFIG_LEVILUMA_WIFI_TX_POWER));
  int8_t txPower = 0;
  ESP_ERROR_CHECK(esp_wifi_get_max_tx_power(&txPower));
  ESP_LOGI(TAG, "WLAN: max. Sendeleistung %.2f dBm (angefordert %.2f dBm)", txPower / 4.0f, CONFIG_LEVILUMA_WIFI_TX_POWER / 4.0f);
  ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LEVILUMA_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
  ESP_ERROR_CHECK(esp_now_init());
  ESP_ERROR_CHECK(esp_now_register_recv_cb(onRecv));
  ESP_ERROR_CHECK(esp_now_register_send_cb(onSent));
  static_assert(sizeof(CONFIG_LEVILUMA_ESPNOW_PMK) - 1 == ESP_NOW_KEY_LEN, "LEVILUMA_ESPNOW_PMK muss genau 16 Zeichen haben");
  ESP_ERROR_CHECK(esp_now_set_pmk((const uint8_t *)CONFIG_LEVILUMA_ESPNOW_PMK));
  esp_now_peer_info_t b = {};
  memcpy(b.peer_addr, BCAST, ESP_NOW_ETH_ALEN);
  b.ifidx = WIFI_IF_STA;
  ESP_ERROR_CHECK(esp_now_add_peer(&b));
  appLock();
  link->start(appMillis()); // gekoppeltes Gateway als Peer, DISCOVER und Zustandsabgleich
  appUnlock();
  ESP_LOGI(TAG, "ESP-NOW aktiv auf Kanal %d", CONFIG_LEVILUMA_ESPNOW_CHANNEL);
  xTaskCreate(linkTask, "levi_link", 6144, nullptr, 6, nullptr);
}

uint32_t appVersion() {
  appLock();
  uint32_t v = link->version();
  appUnlock();
  return v;
}

int appGatewayCount() {
  return GatewayRegistry::MAX_ENTRIES;
}

bool appView(int idx, GatewayView &v) {
  if (idx < 0 || idx >= GatewayRegistry::MAX_ENTRIES) {
    return false;
  }
  appLock();
  const GatewayEntry &g = reg->at(idx);
  uint32_t now = appMillis();
  v.used = g.used;
  v.paired = g.paired;
  v.online = link->online(idx, now);
  v.stateValid = g.stateValid;
  v.pending = g.used && link->commandPending(idx);
  v.hasKey = g.hasKey;
  v.lastTimeout = g.lastTimeout;
  v.pairPhase = g.pairPhase;
  v.lastError = g.lastError;
  memcpy(v.id, g.id, LVP_ID_LEN);
  strncpy(v.name, g.displayName(), LVP_NAME_MAX);
  v.name[LVP_NAME_MAX] = 0;
  v.caps = g.caps;
  v.fwVersion = g.fwVersion;
  v.lastSeenAgoMs = g.lastSeenMs ? now - g.lastSeenMs : UINT32_MAX;
  v.state = g.state;
  appUnlock();
  return v.used;
}

int appActive() {
  appLock();
  int i = Round::activeGateway(*reg);
  appUnlock();
  return i;
}

bool appMayPair() {
  appLock();
  bool ok = Round::mayPair(*reg);
  appUnlock();
  return ok;
}

void appDiscover() {
  appLock();
  link->discover(appMillis());
  appUnlock();
}

lvp_err_t appPair(int idx, const uint8_t *key) {
  appLock();
  lvp_err_t e = Round::mayPair(*reg) ? link->pair(idx, key, appMillis()) : LVP_ERR_FULL;
  appUnlock();
  return e;
}

bool appUnpair() {
  appLock();
  int i = Round::activeGateway(*reg);
  bool ok = i >= 0 && link->unpair(i, appMillis());
  appUnlock();
  return ok;
}

bool appResync() {
  appLock();
  int i = Round::activeGateway(*reg);
  link->discover(appMillis());
  bool ok = i >= 0 && link->requestState(i, appMillis());
  appUnlock();
  return ok;
}

static lvp_err_t setState(const lvp_set_state_t &c) {
  appLock();
  int i = Round::activeGateway(*reg);
  lvp_err_t e = i >= 0 ? link->setState(i, c, appMillis()) : LVP_ERR_NOT_PAIRED;
  appUnlock();
  return e;
}

lvp_err_t appSetPower(bool on) {
  return setState({LVP_SET_POWER, (uint8_t)(on ? 1 : 0), 0, 0, 0, 0, 0});
}

lvp_err_t appSetBrightness(uint8_t pct) {
  return setState({LVP_SET_BRIGHTNESS, 0, pct, 0, 0, 0, 0});
}

lvp_err_t appSetColor(uint8_t r, uint8_t g, uint8_t b) {
  return setState({LVP_SET_COLOR, 0, 0, r, g, b, 0}); // kalibriert: Kalibrierung wirkt im Gateway
}

const char *appErrorText(uint8_t err, bool timeout) {
  if (timeout && !err) {
    return "Keine Antwort";
  }
  switch (err) {
  case 0:
    return "";
  case LVP_ERR_PAIRING_CLOSED:
    return "Am Gateway BOOT druecken";
  case LVP_ERR_NOT_PAIRED:
    return "Gateway kennt uns nicht";
  case LVP_ERR_FULL:
    return "Kein Platz";
  case LVP_ERR_STORAGE:
    return "Speicherfehler";
  default:
    return lvp_err_name(err);
  }
}
