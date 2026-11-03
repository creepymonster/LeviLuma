// Uebersicht: alle gekoppelten Gateways als Karten (Farbe, Name, Status, Ein/Aus-Schalter).
// Antippen der Karte oeffnet die Detailansicht. Angezeigt wird nur der bestaetigte Zustand;
// waehrend ein Befehl unterwegs ist, steht "Wird gesendet ...".
#include <stdio.h>
#include "lv_theme_shadcn.h"
#include "ui.h"
#if CONFIG_LEVILUMA_ZIGBEE_ENABLED
#include "zigbee_bridge.h"
#endif

static constexpr int N = GatewayRegistry::MAX_ENTRIES;
static lv_obj_t *list = nullptr, *emptyBox = nullptr, *headerInfo = nullptr;
static lv_obj_t *card[N], *swatch[N], *nameLbl[N], *statusLbl[N], *sw[N];
static bool shown[N];

static void cardClicked(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  uiShowDetail(idx);
}

static void switchChanged(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  lv_obj_t *s = (lv_obj_t *)lv_event_get_target(e);
  bool on = lv_obj_has_state(s, LV_STATE_CHECKED);
  lvp_err_t err = appSetPower(idx, on);
  if (err != LVP_OK) {
    uiToast(appErrorText(err, false));
  }
}

static void settingsClicked(lv_event_t *) {
  uiShowSettings();
}

static void setupClicked(lv_event_t *) {
  uiShowGateways();
}

// Statuszeile einer Karte aus dem bestaetigten Zustand
void uiGatewayStatusText(const GatewayView &v, char *out, size_t cap) {
  if (!v.online) {
    snprintf(out, cap, "Offline");
  } else if (v.pending) {
    snprintf(out, cap, "Wird gesendet ...");
  } else if (v.lastTimeout) {
    snprintf(out, cap, "Keine Antwort");
  } else if (!v.stateValid) {
    snprintf(out, cap, "Abgleich ...");
  } else if (!(v.state.flags & LVP_STATE_BLE)) {
    snprintf(out, cap, "Band nicht verbunden");
  } else if (v.state.power == 1) {
    snprintf(out, cap, "An - %u %%", v.state.brightness);
  } else if (v.state.power == 0) {
    snprintf(out, cap, "Aus");
  } else {
    snprintf(out, cap, "Zustand unbekannt");
  }
}

static void buildCard(int i) {
  card[i] = uiCard(list);
  lv_obj_set_flex_flow(card[i], LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(card[i], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(card[i], 10, 0);
  lv_obj_set_clickable(card[i], true);
  lv_obj_add_event_cb(card[i], cardClicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);

  swatch[i] = uiDot(card[i], 26);
  lv_obj_t *col = lv_obj_create(card[i]);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_all(col, 0, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_grow(col, 1);
  lv_obj_set_height(col, LV_SIZE_CONTENT);
  lv_obj_set_scrollable(col, false);
  lv_obj_set_clickable(col, false);
  lv_obj_set_event_bubble(col, true);
  nameLbl[i] = uiLabel(col, "");
  lv_label_set_long_mode(nameLbl[i], LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_width(nameLbl[i], LV_PCT(100));
  statusLbl[i] = uiLabel(col, "", true);
  sw[i] = lv_switch_create(card[i]);
  lv_obj_add_event_cb(sw[i], switchChanged, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
}

static void refresh(bool) {
  GatewayView v;
  int online = 0, paired = 0;
  bool layoutChanged = false;

  for (int i = 0; i < N; i++) {
    bool want = appView(i, v) && v.paired;
    layoutChanged |= want != shown[i];
  }
  if (layoutChanged) {
    lv_obj_clean(list);
    for (int i = 0; i < N; i++) {
      shown[i] = appView(i, v) && v.paired;
      if (shown[i]) {
        buildCard(i);
      }
    }
  }
  for (int i = 0; i < N; i++) {
    if (!shown[i] || !appView(i, v)) {
      continue;
    }
    paired++;
    online += v.online ? 1 : 0;
    lv_label_set_text(nameLbl[i], v.name);
    char st[48];
    uiGatewayStatusText(v, st, sizeof(st));
    lv_label_set_text(statusLbl[i], st);
    bool on = v.stateValid && v.state.power == 1;
    lv_obj_set_style_bg_color(swatch[i], on ? lv_color_make(v.state.r, v.state.g, v.state.b) : lv_color_hex(0x3F3F46), 0);
    lv_obj_set_style_bg_opa(swatch[i], LV_OPA_COVER, 0);
    bool usable = v.online && v.stateValid && (v.state.flags & LVP_STATE_BLE);
    uiSetEnabled(sw[i], usable);
    if (!v.pending) { // waehrend ein Befehl laeuft, bleibt die Eingabe sichtbar
      lv_obj_set_state(sw[i], LV_STATE_CHECKED, on);
    }
  }
  lv_obj_set_hidden(emptyBox, paired > 0);
  lv_obj_set_hidden(list, paired == 0);
  char info[32];
  snprintf(info, sizeof(info), "%d/%d online", online, paired);
#if CONFIG_LEVILUMA_ZIGBEE_ENABLED
  ZbStatus zs = zbStatus();
  snprintf(info, sizeof(info), "%d/%d  ZB %s", online, paired, zs.joined ? "ok" : "-");
#endif
  lv_label_set_text(headerInfo, info);
}

void uiShowHome() {
  lv_obj_t *content = uiScreen("LeviLuma", nullptr, refresh);
  headerInfo = uiLabel(uiHeader(), "", true);
  lv_obj_t *gear = lv_button_create(uiHeader());
  lv_theme_shadcn_set_variant(gear, LV_THEME_SHADCN_VARIANT_GHOST);
  lv_theme_shadcn_set_size(gear, LV_THEME_SHADCN_SIZE_ICON_SM);
  lv_obj_t *gl = lv_label_create(gear);
  lv_label_set_text(gl, LV_SYMBOL_SETTINGS);
  lv_obj_center(gl);
  lv_obj_add_event_cb(gear, settingsClicked, LV_EVENT_CLICKED, nullptr);

  list = lv_obj_create(content);
  lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_row(list, 6, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollable(list, false);
  for (int i = 0; i < N; i++) {
    shown[i] = false;
  }

  emptyBox = uiCard(content);
  uiLabel(emptyBox, "Noch kein Gateway gekoppelt.");
  uiLabel(emptyBox, "Gateways werden automatisch gefunden.", true);
  uiButton(emptyBox, "Gateways einrichten", setupClicked);
  refresh(true);
}
