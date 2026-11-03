// Serielle Eingabe (115200 Baud, Zeilen mit Enter abschliessen): Befehle siehe "help".
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "app.h"

static void handleLine(const char *line) {
  static char outbuf[2048];
  dbg::Out out(outbuf, sizeof(outbuf));
  dbg::CmdEffects fx;
  dbg::Stats snap;
  xSemaphoreTake(g_app.lock, portMAX_DELAY);
  snap = g_app.stats;
  dbg::run_command(line, g_app.settings, snap, appMillis(), out, fx);
  xSemaphoreGive(g_app.lock);
  fputs(out.c_str(), stdout);

  // Wirkungen auf das Funkteil ausserhalb der Sperre
  if (fx.scan_stop) {
    snifferScan(false, 0);
  }
  if (fx.set_channel) {
    if (snifferSetChannel(fx.channel)) {
      printf("Empfang jetzt auf Kanal %u.\n", fx.channel);
    } else {
      printf("Kanalwechsel fehlgeschlagen.\n");
    }
  }
  if (fx.scan_start) {
    snifferScan(true, fx.dwell_ms);
  }
  if (fx.reset_stats) {
    g_app.stats = dbg::Stats(); // Zaehler gleichzeitig laufender Tasks koennen dabei einzelne Zaehlungen verlieren
  }
}

static void consoleTask(void *) {
  char line[96];
  size_t n = 0;
  bool overflow = false;

  for (;;) {
    int c = getchar();

    if (c == EOF) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    if (c == '\n' || c == '\r') {
      line[n] = 0;

      if (overflow) {
        printf("Zeile zu lang, verworfen.\n");
      } else if (n) {
        handleLine(line);
      }
      n = 0;
      overflow = false;
    } else if (n < sizeof(line) - 1) {
      line[n++] = (char)c;
    } else {
      overflow = true;
    }
  }
}

void consoleStart() {
  xTaskCreate(consoleTask, "levi_con", 4096, nullptr, 2, nullptr);
}
