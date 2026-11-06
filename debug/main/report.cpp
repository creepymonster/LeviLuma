// Auswertetask: Queue -> Rahmenauswertung -> LeviLuma-Dekodierung -> Filter -> Ausgabe.
// Hier (und nur hier) wird formatiert und serielle Ausgabe fuer Nachrichten erzeugt; die Callbacks bleiben frei davon.
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <string.h>
#include "freertos/task.h"
#include "sdkconfig.h"
#include "app.h"

static void reportTask(void *) {
  static dbg::Capture cap;
  static dbg::FrameInfo fi;
  static dbg::LeviInfo li;
  static char buf[CONFIG_LEVILUMA_DEBUG_OUT_BUF];
  dbg::Dedup dedup(
#if CONFIG_LEVILUMA_DEBUG_ESPNOW_CB
      CONFIG_LEVILUMA_DEBUG_DEDUP_MS
#else
      50
#endif
  );
  uint32_t index = 0;
  dbg::Stats &st = g_app.stats;

  for (;;) {
    if (xQueueReceive(g_app.queue, &cap, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    st.processed++;
    dbg::parse_capture(cap, fi);

    // Derselbe Rahmen ueber zwei Empfangswege: nur einmal ausgeben
    if (dedup.seen(cap.source, fi, cap.t_ms)) {
      st.dup_dropped++;
      continue;
    }
    st.by_channel[cap.channel <= 14 ? cap.channel : 0]++;

    switch (fi.verdict) {
    case dbg::Verdict::Encrypted:
      st.encrypted++;
      break;
    case dbg::Verdict::NotEspNow:
      st.not_espnow++;
      break;
    case dbg::Verdict::Truncated:
      st.truncated++;
      break;
    case dbg::Verdict::Malformed:
    case dbg::Verdict::TooShort:
      st.malformed++;
      break;
    case dbg::Verdict::Ok:
      break;
    }
    // LeviLuma nur auswerten, wenn Klartext-Nutzdaten vorliegen (nie bei verschluesselten Rahmen)
    li = dbg::LeviInfo();
    if (fi.body && (fi.verdict == dbg::Verdict::Ok || fi.verdict == dbg::Verdict::Truncated)) {
      dbg::decode_levi(fi.body, fi.body_len, fi.body_complete, li);

      switch (li.kind) {
      case dbg::LeviInfo::Kind::Decoded:
        st.levi_ok++;
        break;
      case dbg::LeviInfo::Kind::NotLevi:
        st.not_levi++;
        break;
      default:
        if (li.err == LVP_ERR_CRC) {
          st.levi_crc_bad++;
        } else {
          st.levi_other_err++;
        }
        break;
      }
    }
    dbg::Settings s;
    xSemaphoreTake(g_app.lock, portMAX_DELAY);
    s = g_app.settings;
    xSemaphoreGive(g_app.lock);

    // Suchlauf: bei gueltigem LeviLuma-Verkehr auf diesem Kanal bleiben
    if (li.kind == dbg::LeviInfo::Kind::Decoded && s.plan.scanning && s.plan.stay_on_hit) {
      snifferScan(false, 0);
      printf(">>> Suchlauf beendet: gueltiger LeviLuma-Verkehr auf Kanal %u\n", cap.channel);
    }
    if (!s.filter.match(fi, li)) {
      st.filtered++;
      continue;
    }
    dbg::Out out(buf, sizeof(buf));
    dbg::format_record(out, ++index, cap, fi, li, s.fmt);
    fputs(out.c_str(), stdout);

    if (out.truncated()) {
      st.out_truncated++;
      fputs("  [Ausgabe gekuerzt: Ausgabepuffer zu klein, Kconfig LEVILUMA_DEBUG_OUT_BUF]\n", stdout);
    }
    st.printed++;
  }
}

void reportStart() {
  xTaskCreate(reportTask, "levi_report", 8192, nullptr, 4, nullptr);
}
