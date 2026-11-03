// Zigbee-Einstellungen (nur mit CONFIG_LEVILUMA_ZIGBEE_ENABLED): Netzwerkstatus, FRITZ!Box-Kopplung,
// Netzwerk verlassen, Endpoint-Verwaltung (Zuordnung Gateway <-> Endpoint) und Diagnose.
#include <stdio.h>
#include "esp_system.h"
#include "lv_theme_shadcn.h"
#include "ui.h"
#include "zb_mapping.h"
#include "zigbee_bridge.h"

static lv_obj_t *netLbl, *epList, *diagLbl, *restartBtn;
static uint32_t epSig = 0;

static void back() {
  uiShowSettings();
}

static void steerClicked(lv_event_t *) {
  ZbStatus s = zbStatus();
  if (!s.started) {
    uiToast(s.noEndpoints ? "Erst ein Gateway koppeln, dann neu starten." : "Zigbee-Stack laeuft nicht.");
    return;
  }
  zbStartSteering();
  uiToast("Kopplung gestartet. In der FRITZ!Box muss \"Smart Home > Geraet anmelden\" aktiv sein.");
}

static void doLeave(void *) {
  zbLeave();
}

static void leaveClicked(lv_event_t *) {
  uiConfirm("Netzwerk verlassen", "Zigbee-Netzwerk verlassen? Die Lichter verschwinden aus der FRITZ!Box.", "Verlassen", doLeave, nullptr);
}

static void doRestart(void *) {
  esp_restart();
}

static void restartClicked(lv_event_t *) {
  uiConfirm("Neustart", "Controller neu starten, um die Zigbee-Endpoints neu anzulegen?", "Neu starten", doRestart, nullptr);
}

static void epToggle(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  GatewayView v;
  if (!appView(idx, v)) {
    return;
  }
  bool has = v.zbEndpoint != 0 && v.zbEndpoint != ZbMap::OPT_OUT;
  if (!zbAssign(idx, !has)) {
    uiToast("Zuordnung nicht moeglich");
  }
  epSig = 0;
}

static void buildEpList() {
  lv_obj_clean(epList);
  GatewayView v;
  int n = 0;
  for (int i = 0; i < appGatewayCount(); i++) {
    if (!appView(i, v) || !v.paired) {
      continue;
    }
    bool has = v.zbEndpoint != 0 && v.zbEndpoint != ZbMap::OPT_OUT;
    char line[80];
    if (has) {
      snprintf(line, sizeof(line), "EP %u  <->  %s\nTippen: Zuordnung entfernen", v.zbEndpoint, v.name);
    } else {
      snprintf(line, sizeof(line), "kein Endpoint  <->  %s\nTippen: zuordnen", v.name);
    }
    uiListButton(epList, has ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE, line, epToggle, (void *)(intptr_t)i);
    n++;
  }
  if (!n) {
    uiLabel(epList, "Keine gekoppelten Gateways.", true);
  }
}

static void refresh(bool) {
  ZbStatus s = zbStatus();
  char t[200];
  if (!s.started) {
    snprintf(t, sizeof(t), "Stack: %s", s.noEndpoints ? "nicht gestartet (kein Gateway gekoppelt)" : "startet ...");
  } else {
    snprintf(t, sizeof(t), "Netzwerk: %s%s\nPAN 0x%04X, Kanal %u, Adresse 0x%04X\nGeraetetyp: %s", s.joined ? "verbunden" : "nicht verbunden",
             s.steering ? " (Kopplung laeuft)" : "", s.panId, s.channel, s.shortAddr, s.deviceType);
  }
  lv_label_set_text(netLbl, t);

  uint32_t sig = 0;
  GatewayView v;
  for (int i = 0; i < appGatewayCount(); i++) {
    if (appView(i, v) && v.paired) {
      sig = sig * 31 + (uint32_t)i * 7 + v.zbEndpoint + (uint32_t)v.name[0];
    }
  }
  if (sig != epSig) {
    epSig = sig;
    buildEpList();
  }
  snprintf(t, sizeof(t), "Letztes Ereignis: %s\nBefehle von Zigbee %lu, Attribut-Updates %lu, Fehler %lu%s", s.lastSignal,
           (unsigned long)s.commandsRx, (unsigned long)s.attrUpdates, (unsigned long)s.errors,
           s.restartRequired ? "\nEndpoint-Zuordnung geaendert: Neustart erforderlich" : "");
  lv_label_set_text(diagLbl, t);
  lv_obj_set_hidden(restartBtn, !(s.restartRequired || s.noEndpoints));
}

void uiShowZigbee() {
  lv_obj_t *c = uiScreen("Zigbee (FRITZ!Box)", back, refresh);
  lv_obj_t *card = uiCard(c);
  netLbl = uiLabel(card, "");
  lv_label_set_long_mode(netLbl, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width(netLbl, LV_PCT(100));
  lv_obj_t *row = uiRow(c);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(row, 6, 0);
  uiButton(row, "Mit FRITZ!Box koppeln", steerClicked);
  lv_obj_t *leave = uiButton(row, "Verlassen", leaveClicked, nullptr, false);
  lv_theme_shadcn_set_variant(leave, LV_THEME_SHADCN_VARIANT_DESTRUCTIVE);
  restartBtn = uiButton(c, LV_SYMBOL_REFRESH " Neu starten", restartClicked, nullptr, false);

  uiListText(c, "Endpoints (je Gateway ein Zigbee-Licht)");
  epList = uiList(c);
  epSig = 0;
  uiListText(c, "Diagnose");
  lv_obj_t *dcard = uiCard(c);
  diagLbl = uiLabel(dcard, "", true);
  lv_label_set_long_mode(diagLbl, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width(diagLbl, LV_PCT(100));
  refresh(true);
}
