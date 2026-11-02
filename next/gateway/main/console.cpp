// Serielle Konsole des Gateways (115200 Baud, Zeilen mit Enter abschliessen).
// Befehle: help | status | key <32 hex> | key off | name <text> | pair | unpair all
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/task.h"
#include "gateway.h"

static const char *TAG = "levi-gw-con";

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
  xSemaphoreGive(gw.lock);
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
  } else if (line[0]) {
    printf("Befehle: status | key <32 hex> | key off | name <text> | pair | unpair all\n");
  }
}

static void consoleTask(void *) {
  char line[96];
  size_t n = 0;

  for (;;) {
    int c = getchar();

    if (c == EOF) {
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
