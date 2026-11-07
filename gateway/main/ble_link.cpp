// BLE-Anbindung des Gateways an genau ein Band (NimBLE, ESP-IDF).
// Portiert aus legacy/src/ble.cpp (NimBLE-Arduino): Scan (CONFIG_LEVILUMA_BLE_SCAN_MS), Auswahl ueber BandMemory
// (gemerktes Band, sonst staerkstes Geraet mit Namen einer unterstuetzten Familie, das nach erfolgreicher Verbindung
// gemerkt wird), Verbinden, 1 s warten, alle Services/Characteristics lesen und daraus die Bandfamilie bestimmen
// (BandFam::matchGatt, z. B. ELK: Service 0xFFF0 / Characteristic 0xFFF3), dann Befehle senden
// (ein Befehl pro Zeitfenster, 200 ms Pause nach jedem gesendeten Befehl). Neuer Versuch alle 4 s.
// Familien und Pakete: light_core (band_family.h, band_driver.h), Doku hardware/led-band/.
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "sdkconfig.h"
#include "band_driver.h"
#include "band_family.h"
#include "ble_scheduler.h"
#include "gateway.h"

static const char *TAG = "levi-gw-ble";

// Ohne GATT-Server beantwortet NimBLE Anfragen des Bands nicht (ble_att.c verwirft sie stumm); das Band nimmt dann
// 30 s nach dem Verbinden keine Befehle mehr an. Bestehende sdkconfig-Dateien uebernehmen sdkconfig.defaults nicht,
// deshalb hier ausdruecklich pruefen.
#if !CONFIG_BT_NIMBLE_GATT_SERVER
#error "CONFIG_BT_NIMBLE_GATT_SERVER muss an sein (menuconfig: Bluetooth > NimBLE Options > Roles and Profiles: Peripheral und GATT Server). Siehe sdkconfig.defaults."
#endif

extern "C" void ble_store_config_init(void); // NimBLE-Schluesselspeicher (ESP-IDF, ohne eigenen Header wie in den Beispielen)

static SemaphoreHandle_t evSem; // ein Ergebnis pro Schritt (Scan, Verbindung, Suche)
static volatile bool synced = false;
static uint8_t ownAddrType;

static constexpr int MAX_SEEN = 64;
struct Seen {
  ble_addr_t addr;
  int8_t rssi;
  char name[32];
  uint8_t mfg[32]; // Herstellerdaten (AD 0xFF, mit Company-ID), fuer Zengge-Produkt-ID und BLE-Version
  uint8_t mfgLen;
};
static Seen seen[MAX_SEEN];
static int seenN = 0;
static bool scanHasTarget = false; // gemerktes Band: bekommt auch bei voller Liste einen Platz
static ble_addr_t scanTarget;

static volatile uint16_t connHandle = BLE_HS_CONN_HANDLE_NONE;
static volatile bool writePending = false; // Schreiben mit Bestaetigung unterwegs: kein weiteres starten
static volatile int connStatus = -1;
static uint16_t chrHandle = 0;
static uint8_t chrProps = 0;

// GATT-Suche nach dem Verbinden: alle Services, dann alle Characteristics je Service
static constexpr int MAX_SVCS = 16;
static constexpr int MAX_CHRS = 48;
struct SvcRange {
  uint16_t start, end, uuid16;
};
static SvcRange svcs[MAX_SVCS];
static int svcN = 0;
static BandFam::GattChr chrs[MAX_CHRS];
static int chrN = 0;
static BandDriver connDriver; // Familie des verbundenen Bands, uebernimmt bleTask nach scanAndConnect()

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
    if (!scanHasTarget || ble_addr_cmp(&a, &scanTarget) != 0) {
      return nullptr;
    }
    // Liste voll: letzten Eintrag fuer das gemerkte Band ueberschreiben
    Seen *s = &seen[MAX_SEEN - 1];
    memset(s, 0, sizeof(*s));
    s->addr = a;
    s->rssi = -128;
    return s;
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

    if (ble_hs_adv_parse_fields(&f, ev->disc.data, ev->disc.length_data) == 0) {
      if (f.name && f.name_len) {
        size_t n = f.name_len < sizeof(s->name) - 1 ? f.name_len : sizeof(s->name) - 1;
        memcpy(s->name, f.name, n);
        s->name[n] = 0;
      }
      if (f.mfg_data && f.mfg_data_len) {
        size_t n = f.mfg_data_len < sizeof(s->mfg) ? f.mfg_data_len : sizeof(s->mfg);
        memcpy(s->mfg, f.mfg_data, n);
        s->mfgLen = (uint8_t)n;
      }
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
      // Wie NimBLE-Arduino 1.4.3 (Legacy, NimBLEClient::handleGapEvent): MTU-Austausch direkt nach dem Verbinden.
      int rc = ble_gattc_exchange_mtu(ev->connect.conn_handle, nullptr, nullptr);
      if (rc != 0) {
        ESP_LOGW(TAG, "BLE: MTU-Austausch nicht gestartet (%d)", rc);
      }
    }
    xSemaphoreGive(evSem);
    break;
  // Diagnose: Wuensche des Bands zu den Verbindungsparametern und deren Ergebnis (laeuft im NimBLE-Host-Task)
  // Wunsch des Bands nach neuen Verbindungsparametern. Wie NimBLE-Arduino 1.4.3 (Legacy): annehmen, beim
  // Link-Layer-Verfahren aber mit den eigenen Standardparametern antworten (30-50 ms, Latenz 0, Timeout 2,56 s)
  // statt die Werte des Bands zu uebernehmen (am Geraet: 22,5-47,5 ms, Timeout 1 s). Laeuft im NimBLE-Host-Task.
  case BLE_GAP_EVENT_CONN_UPDATE_REQ:
  case BLE_GAP_EVENT_L2CAP_UPDATE_REQ: {
    const ble_gap_upd_params *p = ev->conn_update_req.peer_params;
    bool ll = ev->type == BLE_GAP_EVENT_CONN_UPDATE_REQ;
    if (ll && ev->conn_update_req.self_params) {
      ble_gap_upd_params *own = ev->conn_update_req.self_params;
      own->itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN;
      own->itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX;
      own->latency = BLE_GAP_INITIAL_CONN_LATENCY;
      own->supervision_timeout = BLE_GAP_INITIAL_SUPERVISION_TIMEOUT;
      own->min_ce_len = BLE_GAP_INITIAL_CONN_MIN_CE_LEN;
      own->max_ce_len = BLE_GAP_INITIAL_CONN_MAX_CE_LEN;
    }
    if (p) {
      ESP_LOGI(TAG, "BLE: Band moechte neue Verbindungsparameter (%s): Intervall %.2f..%.2f ms, Latenz %u, Timeout %u ms - %s",
               ll ? "LL" : "L2CAP", p->itvl_min * 1.25f, p->itvl_max * 1.25f, p->latency, p->supervision_timeout * 10u,
               ll ? "angenommen mit eigenen Parametern wie Legacy" : "angenommen");
    }
    break; // 0 zurueckgeben = annehmen
  }
  case BLE_GAP_EVENT_CONN_UPDATE: {
    ble_gap_conn_desc d;
    if (ev->conn_update.status == 0 && ble_gap_conn_find(ev->conn_update.conn_handle, &d) == 0) {
      ESP_LOGI(TAG, "BLE: Verbindungsparameter geaendert: Intervall %.2f ms, Latenz %u, Timeout %u ms", d.conn_itvl * 1.25f, d.conn_latency,
               d.supervision_timeout * 10u);
    } else {
      ESP_LOGW(TAG, "BLE: Aenderung der Verbindungsparameter fehlgeschlagen (Status %d)", ev->conn_update.status);
    }
    break;
  }
  // Diagnose: Pairing/Verschluesselung (wuenscht das Band eine Kopplung, erscheint das hier)
  case BLE_GAP_EVENT_ENC_CHANGE:
    if (ev->enc_change.status == 0) {
      ESP_LOGI(TAG, "BLE: Verbindung verschluesselt (Pairing mit dem Band abgeschlossen)");
    } else {
      ESP_LOGW(TAG, "BLE: Pairing/Verschluesselung fehlgeschlagen (Status %d)", ev->enc_change.status);
    }
    break;
  case BLE_GAP_EVENT_PASSKEY_ACTION:
    ESP_LOGW(TAG, "BLE: Band verlangt Passkey-Aktion %u (nicht unterstuetzt)", ev->passkey.params.action);
    break;
  case BLE_GAP_EVENT_MTU:
    ESP_LOGI(TAG, "BLE: MTU %u", ev->mtu.value);
    break;
  case BLE_GAP_EVENT_DISCONNECT:
    ESP_LOGI(TAG, "BLE: Verbindung getrennt (Grund 0x%x)", ev->disconnect.reason);
    connHandle = BLE_HS_CONN_HANDLE_NONE;
    chrHandle = 0;
    writePending = false;
    gwSetBle(false);
    break;
  default:
    break;
  }
  return 0;
}

static int onSvc(uint16_t, const struct ble_gatt_error *err, const struct ble_gatt_svc *svc, void *) {
  if (err->status == 0 && svc) {
    if (svcN < MAX_SVCS) {
      svcs[svcN].start = svc->start_handle;
      svcs[svcN].end = svc->end_handle;
      svcs[svcN].uuid16 = ble_uuid_u16(&svc->uuid.u); // 0 bei 128-Bit-UUID
      svcN++;
    }
  } else {
    xSemaphoreGive(evSem); // BLE_HS_EDONE oder Fehler
  }
  return 0;
}

static int onChr(uint16_t, const struct ble_gatt_error *err, const struct ble_gatt_chr *chr, void *arg) {
  if (err->status == 0 && chr) {
    if (chrN < MAX_CHRS) {
      chrs[chrN].svc = (uint16_t)(uintptr_t)arg;
      chrs[chrN].chr = ble_uuid_u16(&chr->uuid.u);
      chrs[chrN].handle = chr->val_handle;
      chrs[chrN].props = chr->properties;
      chrN++;
    }
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
  xSemaphoreTake(gw.lock, portMAX_DELAY);
  scanHasTarget = gw.band->has();
  scanTarget.type = gw.band->addr().type;
  memcpy(scanTarget.val, gw.band->addr().val, sizeof(scanTarget.val));
  xSemaphoreGive(gw.lock);
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

  // Kandidaten fuer die Auswahl (gemerktes Band oder staerkstes passendes Geraet)
  static BandMemory::Candidate cands[MAX_SEEN];
  for (int i = 0; i < seenN; i++) {
    cands[i].addr.type = seen[i].addr.type;
    memcpy(cands[i].addr.val, seen[i].addr.val, sizeof(cands[i].addr.val));
    cands[i].rssi = seen[i].rssi;
    cands[i].name = seen[i].name;
  }
  xSemaphoreTake(gw.lock, portMAX_DELAY);
  bool remembered = gw.band->has();
  BandMemory::Addr rememberedAddr = gw.band->addr();
  BandMemory::Choice ch = gw.band->choose(cands, seenN);
  xSemaphoreGive(gw.lock);
  char ra[18];
  BandMemory::addrToStr(rememberedAddr, ra, sizeof(ra));

  // Geraeteliste: ohne gemerktes Band auf Info-Stufe (Einrichtung), danach nur auf Debug-Stufe,
  // damit ein ausgeschaltetes gemerktes Band das Log nicht alle paar Sekunden mit der ganzen Liste fuellt.
  esp_log_level_t lvl = remembered ? ESP_LOG_DEBUG : ESP_LOG_INFO;
  ESP_LOG_LEVEL_LOCAL(lvl, TAG, "BLE: Scan fertig, %d Geraete gefunden", seenN);
  for (int i = 0; i < seenN; i++) {
    char a[18];
    BandMemory::addrToStr(cands[i].addr, a, sizeof(a));
    const char *mark = i == ch.index ? (remembered ? "  <- gemerktes Band" : "  <- gewaehlt") : "";
    BandFamily fam = BandFam::fromName(seen[i].name);
    if (seen[i].name[0] && fam != BandFamily::Unknown) {
      ESP_LOG_LEVEL_LOCAL(lvl, TAG, "BLE:   %s  RSSI %d  Name \"%s\" (Familie %s laut Name)%s", a, seen[i].rssi, seen[i].name, BandFam::name(fam), mark);
    } else if (seen[i].name[0]) {
      ESP_LOG_LEVEL_LOCAL(lvl, TAG, "BLE:   %s  RSSI %d  Name \"%s\"%s", a, seen[i].rssi, seen[i].name, mark);
    } else {
      ESP_LOG_LEVEL_LOCAL(lvl, TAG, "BLE:   %s  RSSI %d  (kein Name empfangen)%s", a, seen[i].rssi, mark);
    }
  }

  switch (ch.reason) {
  case BandMemory::Reason::None:
    ESP_LOGI(TAG, "BLE: kein Band gemerkt und kein Geraet einer unterstuetzten Bandfamilie gefunden, naechster Versuch in 4 s");
    return false;
  case BandMemory::Reason::RememberedMissing:
    if (ch.ignored) {
      ESP_LOGI(TAG, "BLE: gemerktes Band %s nicht gefunden (%d andere passende Baender ignoriert), naechster Versuch in 4 s", ra, ch.ignored);
    } else {
      ESP_LOGI(TAG, "BLE: gemerktes Band %s nicht gefunden, naechster Versuch in 4 s", ra);
    }
    return false;
  case BandMemory::Reason::Remembered:
    ESP_LOGI(TAG, "BLE: gemerktes Band %s gefunden (\"%s\", RSSI %d dBm), verbinde erneut", ra, seen[ch.index].name, seen[ch.index].rssi);
    break;
  case BandMemory::Reason::Strongest: {
    char a[18];
    BandMemory::addrToStr(cands[ch.index].addr, a, sizeof(a));
    ESP_LOGI(TAG, "BLE: kein Band gemerkt, staerkstes Geraet \"%s\" %s (RSSI %d dBm) gewaehlt", seen[ch.index].name, a, seen[ch.index].rssi);
    break;
  }
  }
  int best = ch.index;
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
  chrHandle = 0;
  chrProps = 0;
  svcN = 0;
  chrN = 0;
  xSemaphoreTake(evSem, 0);

  if (ble_gattc_disc_all_svcs(connHandle, onSvc, nullptr) == 0) {
    waitEv(5000);
  }
  for (int i = 0; i < svcN && connHandle != BLE_HS_CONN_HANDLE_NONE; i++) {
    xSemaphoreTake(evSem, 0);
    if (ble_gattc_disc_all_chrs(connHandle, svcs[i].start, svcs[i].end, onChr, (void *)(uintptr_t)svcs[i].uuid16) == 0) {
      waitEv(5000);
    }
  }
  for (int i = 0; i < chrN; i++) {
    ESP_LOGD(TAG, "BLE:   Service 0x%04X  Characteristic 0x%04X  Handle %u  Eigenschaften 0x%02X", chrs[i].svc, chrs[i].chr, chrs[i].handle,
             chrs[i].props);
  }
  // Familie: Name ist nur Hinweis, die GATT-Struktur entscheidet
  BandFamily hint = BandFam::fromName(seen[best].name);
  BandFam::Match m;
  bool found = BandFam::matchGatt(hint, chrs, chrN, m);
  BandFam::ZenggeInfo zi;
  if (found && m.family == BandFamily::Zengge && !BandFam::parseZenggeMfg(seen[best].mfg, seen[best].mfgLen, zi)) {
    BandFam::parseZenggeName(seen[best].name, zi);
  }
  BandDriver drv = found ? BandDriver(m.family, zi) : BandDriver();
  char desc[64];
  drv.describe(desc, sizeof(desc));

  if (!found || !drv.supported()) {
    if (!found) {
      ESP_LOGW(TAG, "BLE: kein bekanntes Schreibziel gefunden (%d Services, %d Characteristics; Liste auf Debug-Stufe), trenne", svcN, chrN);
    } else {
      ESP_LOGW(TAG, "BLE: Bandfamilie erkannt (%s), diese Variante wird nicht unterstuetzt, trenne", desc);
    }
    if (connHandle != BLE_HS_CONN_HANDLE_NONE) {
      ble_gap_terminate(connHandle, BLE_ERR_REM_USER_CONN_TERM);
    }
    return false;
  }
  const BandFam::GattChr &c = chrs[m.index];
  if (m.family != hint && hint != BandFamily::Unknown) {
    ESP_LOGI(TAG, "BLE: Name deutet auf %s, GATT-Struktur auf %s; verwendet wird %s", BandFam::name(hint), BandFam::name(m.family),
             BandFam::name(m.family));
  }
  if (m.family != BandFamily::Elk) {
    ESP_LOGW(TAG, "BLE: Bandfamilie %s ist NICHT am Geraet getestet (Pakete aus Fremdquellen, siehe hardware/led-band/)", BandFam::name(m.family));
  }
  if (!(c.props & BandFam::PROP_WRITE)) {
    ESP_LOGW(TAG, "BLE: Band bietet kein Schreiben mit Bestaetigung an, es wird ohne Antwort geschrieben");
  }
  ESP_LOGI(TAG, "BLE: Bandfamilie %s, Service 0x%04X / Characteristic 0x%04X (Handle %u, Eigenschaften 0x%02X: %s%s%s%s)", desc, c.svc, c.chr, c.handle,
           c.props, (c.props & BandFam::PROP_WRITE_NO_RSP) ? "Write-without-response " : "", (c.props & BandFam::PROP_WRITE) ? "Write " : "",
           (c.props & BandFam::PROP_READ) ? "Read " : "", (c.props & BandFam::PROP_NOTIFY) ? "Notify" : "");
  connDriver = drv;
  chrProps = c.props;
  chrHandle = c.handle;
  xSemaphoreTake(gw.lock, portMAX_DELAY);
  snprintf(gw.bandInfo, sizeof(gw.bandInfo), "%s", desc);
  xSemaphoreGive(gw.lock);

  // Erste erfolgreiche Verbindung: Band dauerhaft merken (erst nach gefundenem Service, nicht schon beim Scan)
  if (!remembered) {
    xSemaphoreTake(gw.lock, portMAX_DELAY);
    bool ok = gw.band->remember(cands[best].addr);
    xSemaphoreGive(gw.lock);
    char a[18];
    BandMemory::addrToStr(cands[best].addr, a, sizeof(a));
    if (ok) {
      ESP_LOGI(TAG, "BLE: Band %s (\"%s\") dauerhaft gemerkt, kuenftig wird nur dieses Band verbunden", a, seen[best].name);
    } else {
      ESP_LOGW(TAG, "BLE: Band %s konnte nicht gemerkt werden (NVS-Fehler), naechste Verbindung waehlt erneut", a);
    }
  }
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


// Diagnose: Ergebnis eines Schreibens mit Bestaetigung (laeuft im NimBLE-Host-Task)
static int onWriteRsp(uint16_t, const ble_gatt_error *err, ble_gatt_attr *, void *arg) {
  writePending = false;
  uint32_t sentAt = (uint32_t)(uintptr_t)arg;
  uint32_t ms = gwMillis() - sentAt;
  if (err && err->status == 0) {
    ESP_LOGI(TAG, "BLE: Band hat Schreiben bestaetigt (%lu ms)", (unsigned long)ms);
  } else {
    ESP_LOGW(TAG, "BLE: Band hat Schreiben NICHT bestaetigt (Status %u nach %lu ms)", err ? err->status : 0xFFFF, (unsigned long)ms);
  }
  return 0;
}

// Paket als Hex ohne Leerzeichen (z. B. 7E0004010000000000EF), direkt in LightBlue o. ae. einfuegbar
static void packetHex(const uint8_t *p, size_t n, char *out, size_t cap) {
  size_t o = 0;
  for (size_t i = 0; i < n && o + 2 < cap; i++) {
    o += snprintf(out + o, cap - o, "%02X", p[i]);
  }
  out[o < cap ? o : cap - 1] = 0;
}

static void bleTask(void *) {
  BleScheduler::Config cfg;
  cfg.brightnessMax = CONFIG_LEVILUMA_BRIGHTNESS_MAX == 255 ? 255 : 100;
  cfg.micEqMode = CONFIG_LEVILUMA_MIC_EQ_MODE;
  cfg.fadeSteps = CONFIG_LEVILUMA_FADE_STEPS;
  BleScheduler sched(cfg);
  uint32_t lastAttempt = 0;
  bool ready = false;
  uint32_t colorHintRev = gwSnapshot().colorRev; // Startfarbe nicht als Aenderung melden
  uint32_t lastLinkLog = 0;

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
          sched.setDriver(connDriver);
          sched.onConnected(s);
          gwSetBle(true);
          ready = true;
        } else {
          lastAttempt = gwMillis();
        }
      }
    } else if (ready) {
      // Diagnose alle 10 s (Debug-Stufe): lebt die Verbindung noch (RSSI lesbar) und mit welchen Parametern?
      if (gwMillis() - lastLinkLog >= 10000) {
        lastLinkLog = gwMillis();
        int8_t rssi = 0;
        ble_gap_conn_desc d;
        int rr = ble_gap_conn_rssi(connHandle, &rssi);
        int rf = ble_gap_conn_find(connHandle, &d);
        if (rr == 0 && rf == 0) {
          ESP_LOGD(TAG, "BLE-Verbindung: RSSI %d dBm, Intervall %.2f ms, Latenz %u, Timeout %u ms", rssi, d.conn_itvl * 1.25f, d.conn_latency,
                   d.supervision_timeout * 10u);
        } else {
          ESP_LOGW(TAG, "BLE-Verbindung nicht abfragbar (RSSI rc %d, Verbindung rc %d)", rr, rf);
        }
      }
      LeviCal::Model cal = gwCalibCopy();
      BleScheduler::Step st;
      uint32_t now = gwMillis();

      if (writePending) {
        // auf die Bestaetigung des vorigen Pakets warten (NimBLE begrenzt offene GATT-Vorgaenge)
      } else if (sched.next(s, cal, now, st)) {
        // Standard: mit Bestaetigung, sofern das Band "Write" anbietet; sonst bzw. mit "ble rsp off" ohne Antwort wie Legacy.
        // Bietet das Band nur "Write" an, wird immer mit Bestaetigung geschrieben.
        bool canRsp = chrProps & BLE_GATT_CHR_PROP_WRITE;
        bool withRsp = canRsp && (gw.bleWriteRsp || !(chrProps & BLE_GATT_CHR_PROP_WRITE_NO_RSP));
        if (st.len == 0) {
          // Familie kennt den Befehl nicht (sollte nicht vorkommen: der Scheduler fragt nur Unterstuetztes ab)
          ESP_LOGW(TAG, "BLE: %s fuer Bandfamilie %s nicht verfuegbar, uebersprungen", cmdName(st.cmd), BandFam::name(sched.driver().family()));
          sched.commit(st, true, s, now);
          vTaskDelay(pdMS_TO_TICKS(25));
          continue;
        }
        if (withRsp) {
          writePending = true;
        }
        int rc = withRsp ? ble_gattc_write_flat(connHandle, chrHandle, st.packet, st.len, onWriteRsp, (void *)(uintptr_t)now)
                         : ble_gattc_write_no_rsp_flat(connHandle, chrHandle, st.packet, st.len);
        if (rc != 0) {
          writePending = false;
        }
        bool ok = rc == 0;
        sched.commit(st, ok, s, now);
        char hex[2 * sizeof(st.packet) + 1];
        packetHex(st.packet, st.len, hex, sizeof(hex));

        if (!ok) {
          ESP_LOGW(TAG, "BLE: %s schreiben fehlgeschlagen (%d), Paket %s", cmdName(st.cmd), rc, hex);
        } else if (st.cmd == BleScheduler::Cmd::Color) {
          ESP_LOGI(TAG, "BLE: Farbe Schritt %d/%d -> %u,%u,%u%s, Paket %s", st.fadeStep, st.fadeN, st.r, st.g, st.b, st.raw ? " (raw)" : "", hex);
        } else {
          ESP_LOGI(TAG, "BLE: %s gesendet, Paket %s", cmdName(st.cmd), hex);
        }
        // Nach jedem Versuch Pause: 200 ms wie Legacy, nach einem Fehler 500 ms (sonst alle 25 ms ein neuer Versuch)
        vTaskDelay(pdMS_TO_TICKS(ok ? 200 : 500));
      } else if (sched.fadeDone()) {
        gwMarkSent(s.revision); // nichts mehr offen: alles bis zu dieser Revision ist gesendet
      }
      // Wie Legacy: Farben gehen nur ans Band, wenn das Licht als "an" gilt. Einmal je Farbaenderung erklaeren,
      // damit eine gewaehlte, aber nicht gesendete Farbe im Log nicht wie ein Funkfehler aussieht.
      if (s.power != 1 && s.colorRev != colorHintRev) {
        colorHintRev = s.colorRev;
        ESP_LOGI(TAG, "BLE: Farbe gespeichert, aber nicht gesendet: Licht ist %s (erst einschalten)", s.power == 0 ? "aus" : "im Zustand unbekannt");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(25));
  }
}

void bleStart() {
  evSem = xSemaphoreCreateBinary();
  ESP_ERROR_CHECK(nimble_port_init());
  // Sicherheit wie NimBLE-Arduino 1.4.3 (Legacy, NimBLEDevice::init): ohne Ein-/Ausgabe, kein Bonding, kein MITM,
  // Secure Connections, Schluesselspeicher. Ohne diese Einstellungen kann ein Pairing-Wunsch des Bands haengen.
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_hs_cfg.sm_bonding = 0;
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
  ble_store_config_init();
  ble_hs_cfg.sync_cb = onSync;
  ble_hs_cfg.reset_cb = onReset;
  nimble_port_freertos_init(hostTask);
  xTaskCreatePinnedToCore(bleTask, "levi_ble", 6144, nullptr, 4, nullptr, 0);
}
