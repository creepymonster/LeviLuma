// Kalibrierung eines Gateways, portiert aus dem Assistenten der Legacy-Web-UI (WebUi.h):
// 10 Schritte, je Zielfarbe die LED per R/G/B-Regler auf die Vorschau einstellen (rohe Farbe, ohne
// Kalibrierung, hoechstens alle 400 ms ans Band), "Uebernehmen" -> naechster Schritt, am Ende werden
// die 30 Werte gespeichert. Abbrechen stellt die vorherige Farbe wieder her. Gespeichert wird auf dem Gateway.
#include <stdio.h>
#include <string.h>
#include "calib_targets.h"
#include "lv_theme_shadcn.h"
#include "ui.h"

extern bool uiHoldScreen;
static int gw = -1;
static lv_obj_t *statusLbl, *pointsLbl, *stepLbl, *preview, *sliders[3], *values[3];
static int step = -1; // -1 = Uebersicht
static uint8_t cur[3], done[LVP_CAL_BYTES];
static uint8_t restore[3];
static bool restoreValid = false, saving = false;

static void back() {
  uiShowGatewaySettings(gw);
}

static void restoreColor() {
  if (restoreValid) {
    appSetColor(gw, restore[0], restore[1], restore[2]);
  }
}

static void overviewRefresh(bool) {
  GatewayView v;
  if (!appView(gw, v) || !v.paired) {
    uiShowGateways();
    return;
  }
  if (saving && !v.calibPending) {
    saving = false;
    uiToast(v.lastError ? appErrorText(v.lastError, false) : "Gespeichert.");
  }
  if (!v.calibValid) {
    lv_label_set_text(statusLbl, v.calibPending ? "Lade Kalibrierung ..." : "Kalibrierung unbekannt (Gateway offline?)");
    lv_label_set_text(pointsLbl, "");
    return;
  }
  char t[80];
  snprintf(t, sizeof(t), "%s, Rev. %u", v.calib.custom ? "Eigene Kalibrierung aktiv (10 Punkte)" : "Standard (FastLED TypicalSMD5050)",
           v.calib.calib_revision);
  lv_label_set_text(statusLbl, t);
  char p[200];
  int n = 0;
  for (int i = 0; i < LVP_CAL_BYTES && n < (int)sizeof(p) - 5; i++) {
    n += snprintf(p + n, sizeof(p) - n, "%u%s", v.calib.points[i], i < LVP_CAL_BYTES - 1 ? (i % 3 == 2 ? ",  " : ",") : "");
  }
  lv_label_set_text(pointsLbl, p);
}

static void showOverview();
static void showStep();

static void sendCurrent() {
  appSetRaw(gw, cur[0], cur[1], cur[2]); // Vorschau ohne Kalibrierung, wird beim Gateway gedrosselt
}

static void sliderChanged(lv_event_t *e) {
  int c = (int)(intptr_t)lv_event_get_user_data(e);
  cur[c] = (uint8_t)lv_slider_get_value(sliders[c]);
  char t[8];
  snprintf(t, sizeof(t), "%u", cur[c]);
  lv_label_set_text(values[c], t);
  sendCurrent();
}

static void nextClicked(lv_event_t *) {
  memcpy(&done[step * 3], cur, 3);
  step++;
  if (step < CalTargets::N) {
    showStep();
    return;
  }
  // fertig: speichern, dann vorherige Farbe wiederherstellen (wie calFinish)
  if (!appSendCalib(gw, done)) {
    uiToast("Senden nicht moeglich");
  }
  saving = true;
  restoreColor();
  showOverview();
}

static void cancelClicked(lv_event_t *) {
  restoreColor();
  showOverview();
}

static void stepRefresh(bool) {
  GatewayView v;
  if (!appView(gw, v) || !v.paired) {
    uiShowGateways();
  }
}

static void showStep() {
  const CalTargets::Target &t = CalTargets::list[step];
  char title[40];
  snprintf(title, sizeof(title), "Schritt %d/%d: %s", step + 1, CalTargets::N, t.name);
  lv_obj_t *c = uiScreen(title, nullptr, stepRefresh);
  lv_obj_set_style_pad_row(c, 3, 0);
  lv_obj_t *row = uiRow(c);
  preview = uiDot(row, 34);
  lv_obj_set_style_radius(preview, 6, 0);
  lv_obj_set_style_bg_color(preview, lv_color_make(t.r, t.g, t.b), 0);
  lv_obj_set_style_bg_opa(preview, LV_OPA_COVER, 0);
  stepLbl = uiLabel(row, "LED auf die Vorschau einstellen", true);
  lv_label_set_long_mode(stepLbl, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_flex_grow(stepLbl, 1);

  cur[0] = t.r;
  cur[1] = t.g;
  cur[2] = t.b;
  static const char *names[3] = {"R", "G", "B"};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *r = uiRow(c);
    uiLabel(r, names[i]);
    sliders[i] = lv_slider_create(r);
    lv_slider_set_range(sliders[i], 0, 255);
    lv_slider_set_value(sliders[i], cur[i], LV_ANIM_OFF);
    lv_obj_set_flex_grow(sliders[i], 1);
    lv_obj_set_height(sliders[i], 14);
    lv_obj_add_event_cb(sliders[i], sliderChanged, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
    char v[8];
    snprintf(v, sizeof(v), "%u", cur[i]);
    values[i] = uiLabel(r, v);
    lv_obj_set_width(values[i], 34);
  }
  lv_obj_t *b = uiRow(c);
  uiButton(b, "Abbrechen", cancelClicked, nullptr, false);
  uiButton(b, step + 1 < CalTargets::N ? "Uebernehmen" : "Speichern", nextClicked);
  uiHoldScreen = true; // Assistent nicht durch die Rueckkehr zur Uebersicht unterbrechen
  sendCurrent();
}

static void startClicked(lv_event_t *) {
  GatewayView v;
  if (!appView(gw, v) || !v.online || !v.stateValid || v.state.power != 1 || !(v.state.flags & LVP_STATE_BLE)) {
    uiToast("Gateway muss online, mit dem Band verbunden und eingeschaltet sein.");
    return;
  }
  restore[0] = v.state.r;
  restore[1] = v.state.g;
  restore[2] = v.state.b;
  restoreValid = !(v.state.flags & LVP_STATE_RAW);
  step = 0;
  showStep();
}

static void doReset(void *) {
  if (!appResetCalib(gw)) {
    uiToast("Senden nicht moeglich");
  }
  saving = true;
}

static void resetClicked(lv_event_t *) {
  uiConfirm("Zuruecksetzen", "Kalibrierung auf den Standard zuruecksetzen?", "Zuruecksetzen", doReset, nullptr);
}

static void showOverview() {
  step = -1;
  lv_obj_t *c = uiScreen("Kalibrierung", back, overviewRefresh);
  lv_obj_t *card = uiCard(c);
  statusLbl = uiLabel(card, "");
  lv_label_set_long_mode(statusLbl, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width(statusLbl, LV_PCT(100));
  pointsLbl = uiLabel(card, "", true);
  lv_label_set_long_mode(pointsLbl, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width(pointsLbl, LV_PCT(100));
  lv_obj_t *r = uiRow(c);
  uiButton(r, "Kalibrierung starten", startClicked);
  uiButton(r, "Zuruecksetzen", resetClicked, nullptr, false);
  uiLabel(c, "Gespeichert wird auf dem Gateway (NVS).", true);
  appRequestCalib(gw);
  overviewRefresh(true);
}

void uiShowCalibration(int idx) {
  gw = idx;
  saving = false;
  showOverview();
}
