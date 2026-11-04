// BLE-Anbindung des Gateways an genau ein Band der ELK-/Lotus-Familie (NimBLE, ESP-IDF).
// Portiert aus legacy/src/ble.cpp (NimBLE-Arduino): Scan (CONFIG_LEVILUMA_BLE_SCAN_MS, staerkstes passendes Geraet),
// Verbinden, 1 s warten, Service 0xFFF0 / Characteristic 0xFFF3 suchen, dann Befehle senden
// (ein Befehl pro Zeitfenster, 200 ms Pause nach jedem gesendeten Befehl). Neuer Versuch alle 4 s.
#include "freertos/FreeRTOS.h"
#include <string.h>
#include <string>
#include "esp_log.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "sdkconfig.h"
#include "ble_scheduler.h"
#include "gateway.h"

static const char *TAG = "levi-gw-ble";

// Namen der Lotus-Lantern-/ELK-Familie (gleiches Protokoll, Service 0xFFF0), wie Legacy bleNameMatches()
static bool bleNameMatches(const std::string &n) {
  static const char *prefixes[] = {"ELK-BLEDDM", "ELK-BLEDOM", "ELK-BLEDOB", "MELK", "LEDBLE"};

  for (const char *p : prefixes) {
    if (n.rfind(p, 0) == 0) {
      return true;
    }
  }
  return false;
}

static SemaphoreHandle_t evSem; // ein Ergebnis pro Schritt (Scan, Verbindung, Suche)
static volatile bool synced = false;
static uint8_t ownAddrType;

static constexpr int MAX_SEEN = 24;
struct Seen {
  ble_addr_t addr;
  int8_t rssi;
  char name[32];
};
static Seen seen[MAX_SEEN];
static int seenN = 0;

static volatile uint16_t connHandle = BLE_HS_CONN_HANDLE_NONE;
static volatile int connStatus = -1;
static uint16_t svcStart = 0, svcEnd = 0, chrHandle = 0;
static uint8_t chrProps = 0;

static void onSync() {
  ble_hs_id_infer_auto(0, &ownAddrType);
  synced = true;
}

static void onReset(int reason) {
  ESP_LOGW(TAG, "NimBLE-Reset (Grund %d)", reason);
  synced = false;
}

static Seen *seenFind(const ble_addr_t &a) {
  for (int i = 0; i < seenN; i++) {
    if (ble_addr_cmp(&seen[i].addr, &a) == 0) {
      return &seen[i];
    }
  }
  if (seenN >= MAX_SEEN) {
    return nullptr;
  }
  Seen *s = &seen[seenN++];
  memset(s, 0, sizeof(*s));
  s->addr = a;
  s->rssi = -128;
  return s;
}

static int gapEvent(struct ble_gap_event *ev, void *) {
  switch (ev->type) {
  case BLE_GAP_EVENT_DISC: {
    Seen *s = seenFind(ev->disc.addr);
    if (!s) {
      break;
    }
    s->rssi = ev->disc.rssi;
    struct ble_hs_adv_fields f;

    if (ble_hs_adv_parse_fields(&f, ev->disc.data, ev->disc.length_data) == 0 && f.name && f.name_len) {
      size_t n = f.name_len < sizeof(s->name) - 1 ? f.name_len : sizeof(s->name) - 1;
      memcpy(s->name, f.name, n);
      s->name[n] = 0;
    }
    break;
  }
  case BLE_GAP_EVENT_DISC_COMPLETE:
    xSemaphoreGive(evSem);
    break;
  case BLE_GAP_EVENT_CONNECT:
    connStatus = ev->connect.status;
    if (ev->connect.status == 0) {
      connHandle = ev->connect.conn_handle;
    }
    xSemaphoreGive(evSem);
    break;
  case BLE_GAP_EVENT_DISCONNECT:
    ESP_LOGI(TAG, "BLE: Verbindung getrennt (Grund 0x%x)", ev->disconnect.reason);
    connHandle = BLE_HS_CONN_HANDLE_NONE;
    chrHandle = 0;
    gwSetBle(false);
    break;
  default:
    break;
  }
  return 0;
}

static int onSvc(uint16_t, const struct ble_gatt_error *err, const struct ble_gatt_svc *svc, void *) {
  if (err->status == 0 && svc) {
    svcStart = svc->start_handle;
    svcEnd = svc->end_handle;
  } else {
    xSemaphoreGive(evSem); // BLE_HS_EDONE oder Fehler
  }
  return 0;
}

static int onChr(uint16_t, const struct ble_gatt_error *err, const struct ble_gatt_chr *chr, void *) {
  if (err->status == 0 && chr) {
    chrHandle = chr->val_handle;
    chrProps = chr->properties;
  } else {
    xSemaphoreGive(evSem);
  }
  return 0;
}

static void hostTask(void *) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

static bool waitEv(uint32_t ms) {
  return xSemaphoreTake(evSem, pdMS_TO_TICKS(ms)) == pdTRUE;
}

static bool scanAndConnect() {
  seenN = 0;
  struct ble_gap_disc_params dp = {};
  dp.passive = 0; // aktiver Scan wie Legacy (Namen in der Scan-Antwort)
  // Keine Duplikatfilterung im Controller: Sie kann die Scan-Antwort derselben Adresse verwerfen,
  // die bei vielen Baendern erst den Namen enthaelt. Doppelte Adressen fasst seenFind() zusammen.
  dp.filter_duplicates = 0;
  xSemaphoreTake(evSem, 0);
  int rc = ble_gap_disc(ownAddrType, CONFIG_LEVILUMA_BLE_SCAN_MS, &dp, gapEvent, nullptr);

  if (rc != 0) {
    ESP_LOGW(TAG, "BLE: Scan-Start fehlgeschlagen (%d)", rc);
    return false;
  }
  waitEv(CONFIG_LEVILUMA_BLE_SCAN_MS + 2000);
  ESP_LOGI(TAG, "BLE: Scan fertig, %d Geraete gefunden", seenN);
  int best = -1;

  for (int i = 0; i < seenN; i++) {
    char a[18];
    const uint8_t *v = seen[i].addr.val;
    snprintf(a, sizeof(a), "%02x:%02x:%02x:%02x:%02x:%02x", v[5], v[4], v[3], v[2], v[1], v[0]);
    if (seen[i].name[0]) {
      ESP_LOGI(TAG, "BLE:   %s  RSSI %d  Name \"%s\"", a, seen[i].rssi, seen[i].name);
    } else {
      ESP_LOGI(TAG, "BLE:   %s  RSSI %d  (kein Name empfangen)", a, seen[i].rssi);
    }

    if (bleNameMatches(seen[i].name) && (best < 0 || seen[i].rssi > seen[best].rssi)) {
      best = i;
    }
  }
  if (best < 0) {
    ESP_LOGI(TAG, "BLE: kein Geraet der ELK/Lotus-Familie gefunden, naechster Versuch in 4 s");
    return false;
  }
  ESP_LOGI(TAG, "BLE: staerkstes Geraet \"%s\" (RSSI %d dBm) gewaehlt", seen[best].name, seen[best].rssi);
  connStatus = -1;
  xSemaphoreTake(evSem, 0);
  rc = ble_gap_connect(ownAddrType, &seen[best].addr, 5000, nullptr, gapEvent, nullptr);

  if (rc != 0 || !waitEv(7000) || connStatus != 0) {
    ESP_LOGW(TAG, "BLE: Verbindung fehlgeschlagen (rc %d, Status %d)", rc, connStatus);
    return false;
  }
  ESP_LOGI(TAG, "BLE: verbunden");
  // Band braucht 1 s nach dem Verbinden, bevor die Services gelesen werden.
  vTaskDelay(pdMS_TO_TICKS(1000));
  svcStart = svcEnd = chrHandle = 0;
  chrProps = 0;
  ble_uuid16_t svcUuid = BLE_UUID16_INIT(0xFFF0);
  ble_uuid16_t chrUuid = BLE_UUID16_INIT(0xFFF3);
  xSemaphoreTake(evSem, 0);

  if (ble_gattc_disc_svc_by_uuid(connHandle, &svcUuid.u, onSvc, nullptr) == 0) {
    waitEv(5000);
  }
  if (svcStart) {
    xSemaphoreTake(evSem, 0);
    if (ble_gattc_disc_chrs_by_uuid(connHandle, svcStart, svcEnd, &chrUuid.u, onChr, nullptr) == 0) {
      waitEv(5000);
    }
  }
  // Legacy prueft canWriteNoResponse(): Schreiben ohne Antwort muss erlaubt sein
  if (!chrHandle || !(chrProps & BLE_GATT_CHR_PROP_WRITE_NO_RSP)) {
    ESP_LOGW(TAG, "BLE: Service 0xFFF0 oder Characteristic 0xFFF3 (ohne Antwort schreibbar) fehlt, trenne");
    if (connHandle != BLE_HS_CONN_HANDLE_NONE) {
      ble_gap_terminate(connHandle, BLE_ERR_REM_USER_CONN_TERM);
    }
    return false;
  }
  ESP_LOGI(TAG, "BLE: Service 0xFFF0 / Characteristic 0xFFF3 gefunden");
  return true;
}

static const char *cmdName(BleScheduler::Cmd c) {
  switch (c) {
  case BleScheduler::Cmd::Music:
    return "Musikmodus";
  case BleScheduler::Cmd::MicEq:
    return "Mikrofon-EQ";
  case BleScheduler::Cmd::Brightness:
    return "Helligkeit";
  case BleScheduler::Cmd::Power:
    return "Power";
  case BleScheduler::Cmd::Color:
    return "Farbe";
  default:
    return "-";
  }
}

static void bleTask(void *) {
  BleScheduler::Config cfg;
  cfg.brightnessMax = CONFIG_LEVILUMA_BRIGHTNESS_MAX == 255 ? 255 : 100;
  cfg.micEqMode = CONFIG_LEVILUMA_MIC_EQ_MODE;
  cfg.fadeSteps = CONFIG_LEVILUMA_FADE_STEPS;
  BleScheduler sched(cfg);
  uint32_t lastAttempt = 0;
  bool ready = false;

  while (!synced) {
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  ESP_LOGI(TAG, "NimBLE bereit");

  for (;;) {
    LightSnapshot s = gwSnapshot();
    bool connected = connHandle != BLE_HS_CONN_HANDLE_NONE && chrHandle != 0;

    if (!connected) {
      ready = false;
      gwSetBle(false);

      if (synced && (lastAttempt == 0 || gwMillis() - lastAttempt > 4000)) {
        lastAttempt = gwMillis();

        if (scanAndConnect()) {
          s = gwSnapshot();
          sched.onConnected(s);
          gwSetBle(true);
          ready = true;
        } else {
          lastAttempt = gwMillis();
        }
      }
    } else if (ready) {
      LeviCal::Model cal = gwCalibCopy();
      BleScheduler::Step st;
      uint32_t now = gwMillis();

      if (sched.next(s, cal, now, st)) {
        int rc = ble_gattc_write_no_rsp_flat(connHandle, chrHandle, st.packet, sizeof(st.packet));
        bool ok = rc == 0;
        sched.commit(st, ok, s, now);

        if (!ok) {
          ESP_LOGW(TAG, "BLE: Schreiben fehlgeschlagen (%d)", rc);
        } else if (st.cmd == BleScheduler::Cmd::Color) {
          ESP_LOGI(TAG, "BLE: Farbe Schritt %d/%d -> %u,%u,%u%s", st.fadeStep, st.fadeN, st.packet[4], st.packet[5], st.packet[6],
                   st.raw ? " (raw)" : "");
        } else {
          ESP_LOGI(TAG, "BLE: %s gesendet", cmdName(st.cmd));
        }
        if (ok) {
          vTaskDelay(pdMS_TO_TICKS(200));
        }
      } else if (sched.fadeDone()) {
        gwMarkSent(s.revision); // nichts mehr offen: alles bis zu dieser Revision ist gesendet
      }
    }
    vTaskDelay(pdMS_TO_TICKS(25));
  }
}

void bleStart() {
  evSem = xSemaphoreCreateBinary();
  ESP_ERROR_CHECK(nimble_port_init());
  ble_hs_cfg.sync_cb = onSync;
  ble_hs_cfg.reset_cb = onReset;
  nimble_port_freertos_init(hostTask);
  xTaskCreatePinnedToCore(bleTask, "levi_ble", 6144, nullptr, 4, nullptr, 0);
}
