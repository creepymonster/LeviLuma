// Zigbee-Bruecke (esp-zigbee-lib 2.x, nativer IEEE-802.15.4-Funk des ESP32-C5).
// Nebenlaeufigkeit: Der Zigbee-Task haelt den Zigbee-Lock, der Link-Task den App-Lock. Beide werden
// nie gleichzeitig gehalten: Zigbee-Befehle gehen ueber eine Queue an einen eigenen Task, Zustands-
// aenderungen der Gateways setzen nur ein Merkerbit, das der Sync-Task abarbeitet.
#include "zigbee_bridge.h"
#include <string.h>
#include "app.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_zigbee.h"
#include "ezbee/zha.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "zb_mapping.h"

#if !CONFIG_ZB_ENABLED
#error "CONFIG_LEVILUMA_ZIGBEE_ENABLED braucht CONFIG_ZB_ENABLED (sdkconfig.defaults.zigbee verwenden)"
#endif

static const char *TAG = "levi-zb";
static constexpr int N = GatewayRegistry::MAX_ENTRIES;

static ZbStatus status;
static portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t bootSignature = 0;
static uint8_t epOfGw[N] = {0}; // beim Start registrierte Endpoints
static volatile uint32_t dirty = 0;  // Bit je Gateway-Index: Attribute aktualisieren

enum class CmdKind : uint8_t { Power, Level, Xy, HueSat, Mired };
struct ZbCmd {
  int gw;
  CmdKind kind;
  uint16_t a, b;
};
static QueueHandle_t cmdQueue;

// Teilwerte von Farbbefehlen (X und Y bzw. Hue und Saturation kommen getrennt)
struct ColorAcc {
  uint16_t x = 0, y = 0;
  uint8_t hue = 0, sat = 0;
};
static ColorAcc acc[N];

static void setSignal(const char *s) {
  portENTER_CRITICAL(&statusMux);
  strncpy(status.lastSignal, s, sizeof(status.lastSignal) - 1);
  portEXIT_CRITICAL(&statusMux);
}

ZbStatus zbStatus() {
  portENTER_CRITICAL(&statusMux);
  ZbStatus s = status;
  portEXIT_CRITICAL(&statusMux);
  appLock();
  s.restartRequired = status.started && ZbMap::layoutSignature(appRegistry()) != bootSignature;
  appUnlock();
  return s;
}

static int gwOfEndpoint(uint8_t ep) {
  for (int i = 0; i < N; i++) {
    if (epOfGw[i] == ep) {
      return i;
    }
  }
  return -1;
}

// ---- Rueckmeldungen der Gateways -> Merker ----

class ZbListener : public CtrlLink::Listener {
  void onGatewayState(int idx) override {
    if (idx >= 0 && idx < N) {
      __atomic_fetch_or(&dirty, 1u << idx, __ATOMIC_RELAXED);
    }
  }
};
static ZbListener listener;

// ---- Zigbee-Ereignisse ----

static void updateNetworkInfo() {
  // Aufruf im Zigbee-Task (Lock gehalten)
  portENTER_CRITICAL(&statusMux);
  status.joined = ezb_bdb_dev_joined();
  status.panId = ezb_nwk_get_panid();
  status.channel = ezb_nwk_get_current_channel();
  status.shortAddr = ezb_nwk_get_short_address();
  portEXIT_CRITICAL(&statusMux);
}

static bool onSignal(const ezb_app_signal_t *sig) {
  ezb_app_signal_type_t t = ezb_app_signal_get_type(sig);
  setSignal(ezb_app_signal_to_string(t));

  switch (t) {
  case EZB_ZDO_SIGNAL_SKIP_STARTUP:
    ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
    break;
  case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
  case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
    ezb_bdb_comm_status_t st = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(sig));
    ESP_LOGI(TAG, "Start: Status 0x%02x, %s", st, ezb_bdb_is_factory_new() ? "noch keinem Netz beigetreten" : "Netz bekannt");
    updateNetworkInfo();
    if (st != EZB_BDB_STATUS_SUCCESS) {
      status.errors++;
    }
    for (int i = 0; i < N; i++) {
      __atomic_fetch_or(&dirty, 1u << i, __ATOMIC_RELAXED); // Attribute auf bestaetigten Zustand setzen
    }
    break;
  }
  case EZB_BDB_SIGNAL_STEERING: {
    ezb_bdb_comm_status_t st = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(sig));
    status.steering = false;
    updateNetworkInfo();
    if (st == EZB_BDB_STATUS_SUCCESS) {
      ESP_LOGI(TAG, "Netzwerk beigetreten: PAN 0x%04x, Kanal %d, Adresse 0x%04x", status.panId, status.channel, status.shortAddr);
      setSignal("Kopplung erfolgreich");
    } else {
      ESP_LOGW(TAG, "Kopplung fehlgeschlagen (0x%02x)", st);
      setSignal("Kopplung fehlgeschlagen");
      status.errors++;
    }
    break;
  }
  case EZB_ZDO_SIGNAL_LEAVE:
    updateNetworkInfo();
    status.joined = false;
    setSignal("Netzwerk verlassen");
    break;
  default:
    break;
  }
  return true;
}

// ---- Befehle der FRITZ!Box (Zigbee-Task) -> Queue ----

static void queueCmd(int gw, CmdKind k, uint16_t a, uint16_t b) {
  ZbCmd c = {gw, k, a, b};
  if (xQueueSend(cmdQueue, &c, 0) != pdTRUE) {
    status.errors++;
  }
}

static void onSetAttr(ezb_zcl_set_attr_value_message_t *m) {
  int gw = gwOfEndpoint(m->info.dst_ep);
  if (gw < 0 || !m->in.attribute.data.value) {
    return;
  }
  status.commandsRx++;
  const void *v = m->in.attribute.data.value;
  uint16_t id = m->in.attribute.id;

  switch (m->info.cluster_id) {
  case EZB_ZCL_CLUSTER_ID_ON_OFF:
    if (id == EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID) {
      queueCmd(gw, CmdKind::Power, *(const uint8_t *)v, 0);
    }
    break;
  case EZB_ZCL_CLUSTER_ID_LEVEL:
    if (id == EZB_ZCL_ATTR_LEVEL_CURRENT_LEVEL_ID) {
      queueCmd(gw, CmdKind::Level, *(const uint8_t *)v, 0);
    }
    break;
  case EZB_ZCL_CLUSTER_ID_COLOR_CONTROL:
    if (id == EZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID) {
      acc[gw].x = *(const uint16_t *)v;
      queueCmd(gw, CmdKind::Xy, acc[gw].x, acc[gw].y);
    } else if (id == EZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_Y_ID) {
      acc[gw].y = *(const uint16_t *)v;
      queueCmd(gw, CmdKind::Xy, acc[gw].x, acc[gw].y);
    } else if (id == EZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_HUE_ID) {
      acc[gw].hue = *(const uint8_t *)v;
      queueCmd(gw, CmdKind::HueSat, acc[gw].hue, acc[gw].sat);
    } else if (id == EZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_SATURATION_ID) {
      acc[gw].sat = *(const uint8_t *)v;
      queueCmd(gw, CmdKind::HueSat, acc[gw].hue, acc[gw].sat);
    } else if (id == EZB_ZCL_ATTR_COLOR_CONTROL_COLOR_TEMPERATURE_MIREDS_ID) {
      queueCmd(gw, CmdKind::Mired, *(const uint16_t *)v, 0);
    }
    break;
  default:
    break;
  }
}

static void onZclAction(ezb_zcl_core_action_callback_id_t id, void *msg) {
  if (id == EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID && msg) {
    onSetAttr((ezb_zcl_set_attr_value_message_t *)msg);
  }
}

// Befehle ausfuehren (eigener Task, nur App-Lock). Die Wirkung kommt als STATE vom Gateway zurueck.
static void cmdTask(void *) {
  for (;;) {
    ZbCmd c;
    if (xQueueReceive(cmdQueue, &c, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    uint8_t rgb[3];
    lvp_err_t e = LVP_OK;
    switch (c.kind) {
    case CmdKind::Power:
      e = appSetPower(c.gw, c.a != 0);
      break;
    case CmdKind::Level:
      e = appSetBrightness(c.gw, ZbMap::levelToBrightness((uint8_t)c.a));
      break;
    case CmdKind::Xy:
      ZbMap::xyToRgb(c.a, c.b, rgb);
      e = appSetColor(c.gw, rgb[0], rgb[1], rgb[2]);
      break;
    case CmdKind::HueSat:
      ZbMap::hueSatToRgb((uint8_t)c.a, (uint8_t)c.b, rgb);
      e = appSetColor(c.gw, rgb[0], rgb[1], rgb[2]);
      break;
    case CmdKind::Mired:
      ZbMap::miredToRgb(c.a, rgb);
      e = appSetColor(c.gw, rgb[0], rgb[1], rgb[2]);
      break;
    }
    if (e != LVP_OK) {
      status.errors++;
      __atomic_fetch_or(&dirty, 1u << c.gw, __ATOMIC_RELAXED); // Attribute zuruecksetzen
    }
  }
}

// ---- Bestaetigter Zustand -> Zigbee-Attribute (eigener Task) ----

static void setAttr(uint8_t ep, uint16_t cluster, uint16_t attr, void *value) {
  ezb_zcl_status_t s = ezb_zcl_set_attr_value(ep, cluster, EZB_ZCL_CLUSTER_SERVER, attr, EZB_ZCL_STD_MANUF_CODE, value, false);
  if (s != EZB_ZCL_STATUS_SUCCESS) {
    ESP_LOGD(TAG, "Attribut 0x%04x/0x%04x an EP %u: Status 0x%02x", cluster, attr, ep, s);
  }
}

static void syncTask(void *) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(100));
    uint32_t d = __atomic_exchange_n(&dirty, 0, __ATOMIC_RELAXED);
    bool assignChanged = false;

    // Neu gekoppelte Gateways bekommen sofort eine dauerhafte Zuordnung (wirksam nach Neustart)
    appLock();
    assignChanged = ZbMap::assignAll(appRegistry());
    appUnlock();
    if (assignChanged) {
      ESP_LOGI(TAG, "Neue Endpoint-Zuordnung gespeichert, Neustart erforderlich");
    }
    if (!d || !status.started) {
      continue;
    }
    for (int i = 0; i < N; i++) {
      if (!(d & (1u << i)) || !epOfGw[i]) {
        continue;
      }
      GatewayView v;
      if (!appView(i, v) || !v.paired || !v.stateValid) {
        continue;
      }
      uint8_t on = v.state.power == 1 ? 1 : 0;
      uint8_t level = ZbMap::brightnessToLevel(v.state.brightness);
      uint8_t rgb[3] = {v.state.r, v.state.g, v.state.b};
      uint16_t x, y;
      uint8_t hue, sat;
      ZbMap::rgbToXy(rgb, &x, &y);
      ZbMap::rgbToHueSat(rgb, &hue, &sat);
      acc[i].x = x;
      acc[i].y = y;
      acc[i].hue = hue;
      acc[i].sat = sat;

      if (esp_zigbee_lock_acquire(pdMS_TO_TICKS(500))) {
        uint8_t ep = epOfGw[i];
        setAttr(ep, EZB_ZCL_CLUSTER_ID_ON_OFF, EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &on);
        setAttr(ep, EZB_ZCL_CLUSTER_ID_LEVEL, EZB_ZCL_ATTR_LEVEL_CURRENT_LEVEL_ID, &level);
        setAttr(ep, EZB_ZCL_CLUSTER_ID_COLOR_CONTROL, EZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_X_ID, &x);
        setAttr(ep, EZB_ZCL_CLUSTER_ID_COLOR_CONTROL, EZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_Y_ID, &y);
        setAttr(ep, EZB_ZCL_CLUSTER_ID_COLOR_CONTROL, EZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_HUE_ID, &hue);
        setAttr(ep, EZB_ZCL_CLUSTER_ID_COLOR_CONTROL, EZB_ZCL_ATTR_COLOR_CONTROL_CURRENT_SATURATION_ID, &sat);
        esp_zigbee_lock_release();
        status.attrUpdates++;
      } else {
        __atomic_fetch_or(&dirty, 1u << i, __ATOMIC_RELAXED);
      }
    }
  }
}

// ---- Stack ----

// ZCL-Zeichenkette: 1 Byte Laenge + Zeichen
static void zclString(char *out, size_t cap, const char *s) {
  size_t n = strlen(s);
  if (n > cap - 2) {
    n = cap - 2;
  }
  out[0] = (char)n;
  memcpy(out + 1, s, n);
  out[n + 1] = 0;
}

static void zigbeeTask(void *) {
  esp_zigbee_config_t cfg = {};
#if CONFIG_ZB_ZED
  cfg.device_config.device_type = EZB_NWK_DEVICE_TYPE_END_DEVICE;
  cfg.device_config.zed_config.ed_timeout = EZB_NWK_ED_TIMEOUT_64MIN;
  cfg.device_config.zed_config.keep_alive = CONFIG_LEVILUMA_ZB_ED_KEEPALIVE_MS;
  status.deviceType = "Endgeraet (ZED)";
#else
  cfg.device_config.device_type = EZB_NWK_DEVICE_TYPE_ROUTER;
  cfg.device_config.zczr_config.max_children = 10;
  status.deviceType = "Router (ZR)";
#endif
  cfg.device_config.install_code_policy = false;
  cfg.platform_config.storage_partition_name = "nvs";
  cfg.platform_config.radio_config.radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE;
  ESP_ERROR_CHECK(esp_zigbee_init(&cfg));

  ezb_aps_secur_enable_distributed_security(false);
  ezb_bdb_set_primary_channel_set(CONFIG_LEVILUMA_ZB_CHANNEL_MASK);
  ezb_bdb_set_secondary_channel_set(CONFIG_LEVILUMA_ZB_CHANNEL_MASK);
  ezb_app_signal_add_handler(onSignal);
#if CONFIG_ZB_ZED
  ezb_nwk_set_rx_on_when_idle(true); // nicht schlafendes Endgeraet: Befehle kommen sofort an
#endif

  static char manuf[34], model[34];
  zclString(manuf, sizeof(manuf), CONFIG_LEVILUMA_ZB_MANUFACTURER);
  zclString(model, sizeof(model), CONFIG_LEVILUMA_ZB_MODEL);
  ezb_af_device_desc_t dev = ezb_af_create_device_desc();

  for (int i = 0; i < N; i++) {
    if (!epOfGw[i]) {
      continue;
    }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers" // Makro aus esp-zigbee-lib
    ezb_zha_color_dimmable_light_config_t lc = EZB_ZHA_COLOR_DIMMABLE_LIGHT_CONFIG();
#pragma GCC diagnostic pop
    lc.color_cfg.color_capabilities = EZB_ZCL_COLOR_CONTROL_COLOR_CAPABILITIES_HUE_SATURATION_SUPPORTED |
                                      EZB_ZCL_COLOR_CONTROL_COLOR_CAPABILITIES_XY_SUPPORTED |
                                      EZB_ZCL_COLOR_CONTROL_COLOR_CAPABILITIES_COLOR_TEMPERATURE_SUPPORTED;
    ezb_af_ep_desc_t ep = ezb_zha_create_color_dimmable_light(epOfGw[i], &lc);
    ezb_zcl_cluster_desc_t basic = ezb_af_endpoint_get_cluster_desc(ep, EZB_ZCL_CLUSTER_ID_BASIC, EZB_ZCL_CLUSTER_SERVER);
    ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, manuf);
    ezb_zcl_basic_cluster_desc_add_attr(basic, EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, model);
    ESP_ERROR_CHECK(ezb_af_device_add_endpoint_desc(dev, ep));
  }
  ESP_ERROR_CHECK(ezb_af_device_desc_register(dev));
  ezb_zcl_core_action_handler_register(onZclAction);
  ESP_ERROR_CHECK(esp_zigbee_start(false));
  status.started = true;
  ESP_LOGI(TAG, "Zigbee gestartet (%s), %d Light-Endpoints", status.deviceType, status.endpoints);
  esp_zigbee_launch_mainloop();
  esp_zigbee_deinit();
  status.started = false;
  vTaskDelete(nullptr);
}

void zbStart() {
  cmdQueue = xQueueCreate(16, sizeof(ZbCmd));
  appLock();
  GatewayRegistry &reg = appRegistry();
  ZbMap::assignAll(reg); // gekoppelte Gateways ohne Endpoint bekommen jetzt einen (persistent)
  int n = 0;
  for (int i = 0; i < N; i++) {
    epOfGw[i] = reg.at(i).used && reg.at(i).paired && ZbMap::hasEndpoint(reg.at(i)) ? reg.at(i).zbEndpoint : 0;
    n += epOfGw[i] ? 1 : 0;
  }
  bootSignature = ZbMap::layoutSignature(reg);
  appUnlock();
  status.endpoints = n;
  appSetListener(&listener);
  xTaskCreate(cmdTask, "levi_zb_cmd", 4096, nullptr, 5, nullptr);
  xTaskCreate(syncTask, "levi_zb_sync", 4096, nullptr, 4, nullptr);

  if (n == 0) {
    status.noEndpoints = true;
    setSignal("Kein Gateway gekoppelt");
    ESP_LOGW(TAG, "Kein gekoppeltes Gateway: Zigbee-Stack startet erst nach Kopplung und Neustart");
    return;
  }
  xTaskCreate(zigbeeTask, "levi_zb", 8192, nullptr, 5, nullptr);
}

void zbStartSteering() {
  if (!status.started) {
    return;
  }
  if (esp_zigbee_lock_acquire(pdMS_TO_TICKS(1000))) {
    status.steering = true;
    setSignal("Kopplung laeuft ...");
    ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
    esp_zigbee_lock_release();
  }
}

void zbLeave() {
  if (!status.started) {
    return;
  }
  if (esp_zigbee_lock_acquire(pdMS_TO_TICKS(1000))) {
    ezb_bdb_reset_via_local_action();
    esp_zigbee_lock_release();
  }
}

bool zbAssign(int gwIdx, bool assign) {
  appLock();
  bool ok = assign ? ZbMap::assign(appRegistry(), gwIdx) != 0 : ZbMap::release(appRegistry(), gwIdx);
  appUnlock();
  return ok;
}
