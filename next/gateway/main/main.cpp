// LeviLuma Gateway (Seeed XIAO ESP32S3): steuert genau eine BLE-LED-Leiste und haelt deren
// autoritativen LightState. Steuerung kommt ausschliesslich per ESP-NOW vom Controller.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "gateway.h"

static const char *TAG = GW_LOG_TAG;
GatewayContext gw = {};

uint32_t gwMillis() {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

LightSnapshot gwSnapshot() {
  xSemaphoreTake(gw.lock, portMAX_DELAY);
  LightSnapshot s = gw.light->snapshot();
  xSemaphoreGive(gw.lock);
  return s;
}

LeviCal::Model gwCalibCopy() {
  xSemaphoreTake(gw.lock, portMAX_DELAY);
  LeviCal::Model m = gw.calib->model();
  xSemaphoreGive(gw.lock);
  return m;
}

void gwSetBle(bool connected) {
  xSemaphoreTake(gw.lock, portMAX_DELAY);
  bool changed = gw.light->snapshot().ble != connected;
  gw.light->setBle(connected);
  xSemaphoreGive(gw.lock);

  if (changed) {
    xEventGroupSetBits(gw.events, GW_EVT_STATE_CHANGED);
  }
}

void gwMarkSent(uint32_t rev) {
  xSemaphoreTake(gw.lock, portMAX_DELAY);
  bool changed = gw.light->snapshot().sentRevision < rev;
  gw.light->markSent(rev);
  xSemaphoreGive(gw.lock);

  if (changed) {
    xEventGroupSetBits(gw.events, GW_EVT_STATE_CHANGED);
  }
}

void gwOpenPairingWindow(uint32_t seconds) {
  xSemaphoreTake(gw.lock, portMAX_DELAY);
  gw.pairing->openWindow(gwMillis(), seconds * 1000u);
  xSemaphoreGive(gw.lock);
  ESP_LOGI(TAG, "Kopplungsfenster offen fuer %u s", (unsigned)seconds);
  xEventGroupSetBits(gw.events, GW_EVT_PAIRING_OPENED);
}

// Standard-Kalibrierung aus Kconfig (Gains in Promille oder 30 feste Werte)
static CalibStore::Defaults calibDefaults() {
  CalibStore::Defaults d;
  d.gr = CONFIG_LEVILUMA_CALIB_GAIN_R / 1000.0f;
  d.gg = CONFIG_LEVILUMA_CALIB_GAIN_G / 1000.0f;
  d.gb = CONFIG_LEVILUMA_CALIB_GAIN_B / 1000.0f;
  const char *p = CONFIG_LEVILUMA_CALIB_POINTS;
  int n = 0;

  while (*p && n < LeviCal::N * 3) {
    char *end = nullptr;
    long v = strtol(p, &end, 10);

    if (end == p || v < 0 || v > 255) {
      break;
    }
    d.points[n++] = (uint8_t)v;
    p = end;
    while (*p == ',' || *p == ' ') {
      p++;
    }
  }
  if (n == LeviCal::N * 3) {
    d.hasPoints = true;
  } else if (CONFIG_LEVILUMA_CALIB_POINTS[0]) {
    ESP_LOGW(TAG, "LEVILUMA_CALIB_POINTS ungueltig (%d Werte), Gains werden verwendet", n);
  }
  return d;
}

// Schluessel: NVS (Konsole) vor Kconfig
static void loadKey() {
  size_t len = LEVI_KEY_LEN;
  gw.hasKey = false;

  if (gw.kv->getBlob("key", gw.key, &len) && len == LEVI_KEY_LEN && !levi_key_is_zero(gw.key)) {
    gw.hasKey = true;
    ESP_LOGI(TAG, "Individueller Schluessel aus NVS aktiv");
  } else if (CONFIG_LEVILUMA_GW_KEY[0]) {
    if (levi_key_parse_hex(CONFIG_LEVILUMA_GW_KEY, gw.key)) {
      gw.hasKey = true;
      ESP_LOGI(TAG, "Individueller Schluessel aus Kconfig aktiv");
    } else {
      ESP_LOGE(TAG, "LEVILUMA_GW_KEY ungueltig (32 Hex-Zeichen erwartet), Gateway laeuft ohne Schluessel");
    }
  }
  if (!gw.hasKey) {
    memset(gw.key, 0, sizeof(gw.key));
    ESP_LOGW(TAG, "Ohne individuellen Schluessel: ESP-NOW unverschluesselt, Kopplung nur im Kopplungsfenster");
  }
}

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "LeviLuma Gateway startet");
  esp_err_t e = nvs_flash_init();

  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS: neu formatieren (%s)", esp_err_to_name(e));
    ESP_ERROR_CHECK(nvs_flash_erase());
    e = nvs_flash_init();
  }
  ESP_ERROR_CHECK(e);

  gw.lock = xSemaphoreCreateMutex();
  gw.events = xEventGroupCreate();
  gw.kv = new NvsKvStore("levigw");
  gw.light = new LightModel();
  gw.calib = new CalibStore(*gw.kv, calibDefaults());
  gw.pairing = new PairingTable(*gw.kv);
  gw.band = new BandMemory(*gw.kv);
  gw.calib->load();
  gw.pairing->load();
  gw.band->load();
  gw.light->setCalibRevision(gw.calib->revision());
  loadKey();

  GatewayHandler::Identity id;
  // Stabile Identitaet: WLAN-STA-MAC (aus der eFuse-Basis-MAC abgeleitet) = ESP-NOW-Adresse
  ESP_ERROR_CHECK(esp_read_mac(id.id, ESP_MAC_WIFI_STA));
  id.session = esp_random();
  id.keyConfigured = gw.hasKey;
  id.fwVersion = 0x0100;
  char name[LVP_NAME_MAX + 1] = {0};
  size_t nl = LVP_NAME_MAX;

  if (gw.kv->getBlob("name", name, &nl) && nl > 0) {
    name[nl] = 0;
  } else if (CONFIG_LEVILUMA_GW_NAME[0]) {
    strncpy(name, CONFIG_LEVILUMA_GW_NAME, LVP_NAME_MAX);
  } else {
    snprintf(name, sizeof(name), "LeviLuma-%02X%02X", id.id[4], id.id[5]);
  }
  strncpy(id.name, name, LVP_NAME_MAX);
  gw.handler = new GatewayHandler(*gw.light, *gw.calib, *gw.pairing, id);

  char idStr[18];
  lvp_id_to_str(id.id, idStr, sizeof(idStr));
  ESP_LOGI(TAG, "Gateway-ID %s, Name \"%s\", %d Controller gekoppelt, Kalibrierung %s (Rev. %u)", idStr, id.name, gw.pairing->count(),
           gw.calib->custom() ? "gespeichert" : "Standard", (unsigned)gw.calib->revision());

  if (gw.band->has()) {
    char a[18];
    BandMemory::addrToStr(gw.band->addr(), a, sizeof(a));
    ESP_LOGI(TAG, "Gemerktes Band %s: nur dieses Band wird verbunden (Konsole \"band forget\" zum Vergessen)", a);
  } else {
    ESP_LOGI(TAG, "Kein Band gemerkt: das staerkste passende Band wird verbunden und danach gemerkt");
  }
  if (CONFIG_LEVILUMA_PAIRING_WINDOW_S > 0) {
    gwOpenPairingWindow(CONFIG_LEVILUMA_PAIRING_WINDOW_S);
  }
  uint8_t bleOff = 0;
  size_t bl = sizeof(bleOff);
  gw.bleOff = gw.kv->getBlob("bleoff", &bleOff, &bl) && bl == 1 && bleOff == 1;
  uint8_t wr = 0;
  size_t wl = sizeof(wr);
  gw.bleWriteRsp = gw.kv->getBlob("blewrsp", &wr, &wl) && wl == 1 && wr == 1;
  if (gw.bleWriteRsp) {
    ESP_LOGW(TAG, "BLE: Schreiben mit Bestaetigung aktiv (Diagnose, \"ble rsp off\" schaltet zurueck)");
  }
  statusIoStart();
  linkStart();
  if (gw.bleOff) {
    // Diagnose: ohne BLE gibt es keine Funk-Koexistenz; zeigt, ob ESP-NOW-Empfang dann funktioniert
    ESP_LOGW(TAG, "BLE ist per Konsole abgeschaltet (Diagnose): kein Band wird verbunden. Wieder einschalten: \"ble on\"");
  } else {
    bleStart();
  }
  consoleStart();
}
