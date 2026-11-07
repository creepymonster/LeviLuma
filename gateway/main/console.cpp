// Serielle Konsole des Gateways (115200 Baud, Zeilen mit Enter abschliessen).
// Befehle: help | status | key <32 hex> | key off | name <text> | pair | unpair all | band | band forget | ble off | ble on | ble rsp on | ble rsp off
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/task.h"
#include "gateway.h"

static const char *TAG = "levi-gw-con";

// Aufruf unter gw.lock
static void printBand() {
  if (gw.band->has()) {
    char a[18];
    BandMemory::addrToStr(gw.band->addr(), a, sizeof(a));
    printf("Gemerktes Band %s (nur dieses wird verbunden)\n", a);
  } else {
    printf("Kein Band gemerkt (das staerkste passende Band wird verbunden und gemerkt)\n");
  }
  if (gw.bandInfo[0]) {
    printf("Bandfamilie (letzte Verbindung): %s\n", gw.bandInfo);
  }
}

static void printStatus() {
  LightSnapshot s = gwSnapshot();
  xSemaphoreTake(gw.lock, portMAX_DELAY);
  const GatewayHandler::Identity &id = gw.handler->identity();
  char idStr[18];
  lvp_id_to_str(id.id, idStr, sizeof(idStr));
  printf("Gateway %s \"%s\", Schluessel %s, %d Controller gekoppelt, Kopplungsfenster %s\n", idStr, id.name, gw.hasKey ? "ja" : "nein",
         gw.pairing->count(), gw.pairing->windowOpen(gwMillis()) ? "offen" : "zu");
  for (int i = 0; i < gw.pairing->count(); i++) {
    lvp_id_to_str(gw.pairing->at(i).mac, idStr, sizeof(idStr));
    printf("  Controller %s \"%s\"\n", idStr, gw.pairing->at(i).name);
  }
  printf("Kalibrierung %s, Rev. %u\n", gw.calib->custom() ? "gespeichert" : "Standard", (unsigned)gw.calib->revision());
  printBand();
  xSemaphoreGive(gw.lock);
  LinkStats st = linkStats();
  printf("ESP-NOW seit Start: %lu Rahmen empfangen (davon %lu wegen voller Empfangs-Queue verworfen), Unicast %lu bestaetigt / %lu ohne MAC-Bestaetigung\n", (unsigned long)st.rx,
         (unsigned long)st.rxDropped, (unsigned long)st.txOk, (unsigned long)st.txFail);
  if (gw.bleOff) {
    printf("BLE abgeschaltet (Diagnose, \"ble on\" schaltet es wieder ein)\n");
  }
  printf("Band %s, Power %d, Helligkeit %u%%, Farbe %u,%u,%u%s%s, Revision %lu, gesendet bis %lu\n", s.ble ? "verbunden" : "getrennt", s.power,
         s.brightness, s.r, s.g, s.b, s.raw ? " (roh)" : "", s.music ? ", Musik" : "", (unsigned long)s.revision, (unsigned long)s.sentRevision);
}

static void handleLine(char *line) {
  char *nl = strpbrk(line, "\r\n");
  if (nl) {
    *nl = 0;
  }
  if (!strcmp(line, "status")) {
    printStatus();
  } else if (!strncmp(line, "key ", 4)) {
    const char *arg = line + 4;

    if (!strcmp(arg, "off")) {
      if (gw.kv->erase("key")) {
        printf("Schluessel im NVS geloescht. Neustart uebernimmt Kconfig-Schluessel oder keinen.\n");
      }
      return;
    }
    uint8_t k[LEVI_KEY_LEN];

    if (!levi_key_parse_hex(arg, k)) {
      printf("Ungueltig: 32 Hex-Zeichen erwartet (nicht nur Nullen).\n");
      return;
    }
    if (!gw.kv->setBlob("key", k, sizeof(k))) {
      printf("NVS-Fehler.\n");
      return;
    }
    printf("Schluessel gespeichert. Gekoppelte Controller muessen denselben Schluessel verwenden. Neustart...\n");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
  } else if (!strncmp(line, "name ", 5)) {
    const char *n = line + 5;
    size_t len = strlen(n);

    if (len == 0 || len > LVP_NAME_MAX) {
      printf("Name: 1 bis %d Zeichen.\n", LVP_NAME_MAX);
      return;
    }
    if (gw.kv->setBlob("name", n, len)) {
      xSemaphoreTake(gw.lock, portMAX_DELAY);
      gw.handler->setName(n);
      xSemaphoreGive(gw.lock);
      printf("Name gespeichert.\n");
    }
  } else if (!strcmp(line, "pair")) {
    gwOpenPairingWindow(300);
  } else if (!strcmp(line, "unpair all")) {
    xSemaphoreTake(gw.lock, portMAX_DELAY);
    bool ok = gw.pairing->clear();
    xSemaphoreGive(gw.lock);
    printf("%s Neustart...\n", ok ? "Alle Kopplungen geloescht." : "NVS-Fehler.");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
  } else if (!strcmp(line, "band")) {
    xSemaphoreTake(gw.lock, portMAX_DELAY);
    printBand();
    xSemaphoreGive(gw.lock);
  } else if (!strcmp(line, "band forget")) {
    xSemaphoreTake(gw.lock, portMAX_DELAY);
    bool ok = gw.band->forget();
    xSemaphoreGive(gw.lock);
    // Neustart trennt eine bestehende Verbindung; danach wird das staerkste passende Band gewaehlt und gemerkt
    printf("%s Neustart...\n", ok ? "Band vergessen." : "NVS-Fehler.");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
  } else if (!strcmp(line, "ble rsp on") || !strcmp(line, "ble rsp off")) {
    // Schreiben mit (Standard) oder ohne Bestaetigung des Bands (wie Legacy); sofort wirksam, bleibt ueber Neustarts
    bool on = !strcmp(line, "ble rsp on");
    uint8_t v = on ? 1 : 0;
    bool ok = gw.kv->setBlob("blewrsp", &v, 1);
    gw.bleWriteRsp = on;
    printf("Schreiben %s Bestaetigung%s\n", on ? "mit" : "ohne", ok ? "" : " (nur bis zum Neustart, NVS-Fehler)");
  } else if (!strcmp(line, "ble off") || !strcmp(line, "ble on")) {
    // Diagnose: BLE ganz abschalten, um die Funk-Koexistenz mit ESP-NOW auszuschliessen (bleibt ueber Neustarts)
    bool off = !strcmp(line, "ble off");
    uint8_t v = 1;
    bool ok = off ? gw.kv->setBlob("bleoff", &v, 1) : gw.kv->erase("bleoff");
    printf("%s Neustart...\n", !ok ? "NVS-Fehler." : off ? "BLE abgeschaltet (Diagnose)." : "BLE eingeschaltet.");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
  } else if (line[0]) {
    printf("Befehle: status | key <32 hex> | key off | name <text> | pair | unpair all | band | band forget | ble off | ble on | ble rsp on | ble rsp off\n");
  }
}

static void consoleTask(void *) {
  setvbuf(stdin, nullptr, _IONBF, 0); // ungepuffert: jedes Zeichen sofort, keine Zeilenpufferung in newlib
  char line[96];
  size_t n = 0;

  for (;;) {
    int c = getchar();

    if (c == EOF) {
      // Ohne Eingabe liefert getchar() EOF und setzt das EOF-Merkmal von stdin; newlib liest danach nicht mehr
      // vom Port, bis es geloescht wird. Ohne clearerr() kaeme nach dem ersten leeren Abruf nie wieder ein Zeichen an.
      clearerr(stdin);
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    if (c == '\n' || c == '\r') {
      line[n] = 0;
      if (n) {
        handleLine(line);
      }
      n = 0;
    } else if (n < sizeof(line) - 1) {
      line[n++] = (char)c;
    }
  }
}

void consoleStart() {
  ESP_LOGI(TAG, "Konsole bereit (\"help\" fuer Befehle)");
  xTaskCreate(consoleTask, "levi_con", 4096, nullptr, 2, nullptr);
}
