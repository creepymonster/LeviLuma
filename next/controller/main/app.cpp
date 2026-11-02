// Controller-Dienst: ESP-NOW-Funk (Radio-Implementierung), Link-Task und threadsichere Aktionen.
#include "app.h"
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
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
    p.channel = 0;
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

static SemaphoreHandle_t lock;
static QueueHandle_t rxQueue;
static NvsKvStore *kv;
static GatewayRegistry *reg;
static EspNowRadio radio;
static CtrlLink *link;
static uint8_t ownId[LVP_ID_LEN];

uint32_t appMillis() {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

void appLock() {
  xSemaphoreTakeRecursive(lock, portMAX_DELAY);
}

void appUnlock() {
  xSemaphoreGiveRecursive(lock);
}

GatewayRegistry &appRegistry() {
  return *reg;
}

const uint8_t *appOwnId() {
  return ownId;
}

void appSetListener(CtrlLink::Listener *l) {
  appLock();
  link->setListener(l);
  appUnlock();
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

static void linkTask(void *) {
  for (;;) {
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
  kv = new NvsKvStore("levictl");
  reg = new GatewayRegistry(*kv);
  reg->load();
  ESP_ERROR_CHECK(esp_read_mac(ownId, ESP_MAC_WIFI_STA));
  CtrlLink::Config c;
  memcpy(c.ownId, ownId, LVP_ID_LEN);
  strncpy(c.ownName, CONFIG_LEVILUMA_CTRL_NAME, LVP_NAME_MAX);
  c.session = esp_random() | 1;
  c.timeoutMs = CONFIG_LEVILUMA_REQUEST_TIMEOUT_MS;
  c.tries = CONFIG_LEVILUMA_REQUEST_TRIES;
  c.beaconMs = CONFIG_LEVILUMA_BEACON_MS;
  link = new CtrlLink(*reg, radio, c);
  char idStr[18];
  lvp_id_to_str(ownId, idStr, sizeof(idStr));
  ESP_LOGI(TAG, "Controller-ID %s, %d Gateways gekoppelt", idStr, reg->pairedCount());
}

void appStart() {
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
  // ESP32-C5 ist Dualband: ESP-NOW laeuft fest auf dem 2,4-GHz-Kanal aus Kconfig
  ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LEVILUMA_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
  ESP_ERROR_CHECK(esp_now_init());
  ESP_ERROR_CHECK(esp_now_register_recv_cb(onRecv));
  static_assert(sizeof(CONFIG_LEVILUMA_ESPNOW_PMK) - 1 == ESP_NOW_KEY_LEN, "LEVILUMA_ESPNOW_PMK muss genau 16 Zeichen haben");
  ESP_ERROR_CHECK(esp_now_set_pmk((const uint8_t *)CONFIG_LEVILUMA_ESPNOW_PMK));
  esp_now_peer_info_t b = {};
  memcpy(b.peer_addr, BCAST, ESP_NOW_ETH_ALEN);
  b.ifidx = WIFI_IF_STA;
  ESP_ERROR_CHECK(esp_now_add_peer(&b));
  appLock();
  link->start(appMillis());
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
  v.calibValid = g.calibValid;
  v.pending = g.used && link->commandPending(idx);
  v.calibPending = g.used && link->calibPending(idx);
  v.hasKey = g.hasKey;
  v.lastTimeout = g.lastTimeout;
  v.pairPhase = g.pairPhase;
  v.lastError = g.lastError;
  memcpy(v.id, g.id, LVP_ID_LEN);
  strncpy(v.name, g.displayName(), LVP_NAME_MAX);
  v.name[LVP_NAME_MAX] = 0;
  memcpy(v.announcedName, g.announcedName, sizeof(v.announcedName));
  memcpy(v.alias, g.alias, sizeof(v.alias));
  v.caps = g.caps;
  v.fwVersion = g.fwVersion;
  v.lastSeenAgoMs = g.lastSeenMs ? now - g.lastSeenMs : UINT32_MAX;
  v.state = g.state;
  v.calib = g.calib;
  v.zbEndpoint = g.zbEndpoint;
  appUnlock();
  return v.used;
}

CtrlLink::Stats appStats() {
  appLock();
  CtrlLink::Stats s = link->stats();
  appUnlock();
  return s;
}

void appDiscover() {
  appLock();
  link->discover(appMillis());
  appUnlock();
}

lvp_err_t appPair(int idx, const uint8_t *key) {
  appLock();
  lvp_err_t e = link->pair(idx, key, appMillis());
  appUnlock();
  return e;
}

bool appUnpair(int idx) {
  appLock();
  bool ok = link->unpair(idx, appMillis());
  appUnlock();
  return ok;
}

bool appSetAlias(int idx, const char *alias) {
  appLock();
  bool ok = reg->setAlias(idx, alias);
  appUnlock();
  return ok;
}

lvp_err_t appSetState(int idx, const lvp_set_state_t &cmd) {
  appLock();
  lvp_err_t e = link->setState(idx, cmd, appMillis());
  appUnlock();
  return e;
}

lvp_err_t appSetPower(int idx, bool on) {
  lvp_set_state_t c = {LVP_SET_POWER, (uint8_t)(on ? 1 : 0), 0, 0, 0, 0, 0};
  return appSetState(idx, c);
}

lvp_err_t appSetBrightness(int idx, uint8_t pct) {
  lvp_set_state_t c = {LVP_SET_BRIGHTNESS, 0, pct, 0, 0, 0, 0};
  return appSetState(idx, c);
}

lvp_err_t appSetColor(int idx, uint8_t r, uint8_t g, uint8_t b) {
  lvp_set_state_t c = {LVP_SET_COLOR, 0, 0, r, g, b, 0};
  return appSetState(idx, c);
}

lvp_err_t appSetRaw(int idx, uint8_t r, uint8_t g, uint8_t b) {
  lvp_set_state_t c = {LVP_SET_RAW, 0, 0, r, g, b, 0};
  return appSetState(idx, c);
}

lvp_err_t appSetMusic(int idx, bool on) {
  lvp_set_state_t c = {LVP_SET_MUSIC, 0, 0, 0, 0, 0, (uint8_t)(on ? 1 : 0)};
  return appSetState(idx, c);
}

bool appRequestState(int idx) {
  appLock();
  bool ok = link->requestState(idx, appMillis());
  appUnlock();
  return ok;
}

bool appRequestCalib(int idx) {
  appLock();
  bool ok = link->requestCalib(idx, appMillis());
  appUnlock();
  return ok;
}

bool appSendCalib(int idx, const uint8_t points[LVP_CAL_BYTES]) {
  appLock();
  bool ok = link->sendCalib(idx, points, appMillis());
  appUnlock();
  return ok;
}

bool appResetCalib(int idx) {
  appLock();
  bool ok = link->resetCalib(idx, appMillis());
  appUnlock();
  return ok;
}

const char *appErrorText(uint8_t err, bool timeout) {
  if (timeout && !err) {
    return "Keine Antwort vom Gateway";
  }
  switch (err) {
  case 0:
    return "";
  case LVP_ERR_PAIRING_CLOSED:
    return "Kopplung am Gateway nicht freigegeben (BOOT-Taste druecken)";
  case LVP_ERR_NOT_PAIRED:
    return "Gateway kennt diesen Controller nicht (neu koppeln)";
  case LVP_ERR_FULL:
    return "Keine freien Plaetze";
  case LVP_ERR_STORAGE:
    return "Speicherfehler";
  case LVP_ERR_OUT_OF_RANGE:
    return "Wert ausserhalb des Bereichs";
  case LVP_ERR_VERSION:
    return "Protokollversion passt nicht";
  default:
    return lvp_err_name(err);
  }
}
