// Detailansicht eines Gateways im Stil der Legacy-LCD-UI, auf 320x240 nebeneinander:
// links das Farbrad (Farbset aus LeviPalette, Name und Hex in der Mitte, Tippen = naechste Farbe),
// rechts die Helligkeit (5..100 % in 5er-Schritten wie Legacy). Keine Statustexte: Verbindung und
// Licht zeigen die Statuspunkte, gesperrte Bedienelemente zeigen, dass nichts steuerbar ist.
// Ein/Aus liegt auf der Uebersicht (Schalter je Gateway). Der Musikmodus hat bewusst kein
// Bedienelement, solange er am Band nicht getestet ist (die ESP-NOW-Befehle bleiben bestehen).
// Alle Anzeigen folgen dem vom Gateway bestaetigten Zustand.
#include <math.h>
#include <stdio.h>
#include "BulbIcon.h"
#include "GatewayIcon.h"
#include "Palette.h"
#include "lv_theme_shadcn.h"
#include "ui.h"

static int gw = -1;
static lv_obj_t *ring, *dots[LeviPalette::N], *pickName, *pickHex;
static lv_obj_t *bar, *barLbl;
static lv_obj_t *dotOnline, *dotPower;
static lv_image_dsc_t bulbDsc, gatewayDsc;
static int selected = -1; // Palettenindex der bestaetigten Farbe, -1 = eigene Farbe
static bool lastActive = false;

static void back() {
  uiShowHome();
}

static void selectColor(int i) {
  uint8_t r, g, b;
  LeviPalette::get(i, r, g, b);
  lvp_err_t e = appSetColor(gw, r, g, b);
  if (e != LVP_OK) {
    uiToast(appErrorText(e, false));
  }
}

static void dotClicked(lv_event_t *e) {
  if (!lastActive) {
    return;
  }
  selectColor((int)(intptr_t)lv_event_get_user_data(e));
}

// Tipp ins Farbrad (nicht auf einen Punkt): rechte Haelfte naechste, linke vorige Farbe (wie Legacy)
static void ringClicked(lv_event_t *e) {
  if (!lastActive || lv_event_get_target(e) != ring) {
    return;
  }
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  lv_area_t a;
  lv_obj_get_coords(ring, &a);
  bool right = p.x >= (a.x1 + a.x2) / 2;
  int n = (selected < 0) ? (right ? 0 : LeviPalette::N - 1) : (selected + (right ? 1 : LeviPalette::N - 1)) % LeviPalette::N;
  selectColor(n);
}

static void barChanged(lv_event_t *) {
  if (!lv_indev_active()) {
    return; // nur Benutzereingaben
  }
  int value = lv_slider_get_value(bar);
  int snapped = (value + 2) / 5 * 5;
  if (value != snapped) {
    lv_slider_set_value(bar, snapped, LV_ANIM_OFF);
  }
  char t[16];
  snprintf(t, sizeof(t), "%d %%", snapped);
  lv_label_set_text(barLbl, t);
  appSetBrightness(gw, (uint8_t)snapped);
}

static void icon(lv_obj_t *parent, lv_image_dsc_t &dsc, const uint8_t *data, uint32_t size) {
  dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  dsc.header.cf = LV_COLOR_FORMAT_A8;
  dsc.header.w = 12;
  dsc.header.h = 12;
  dsc.header.stride = 12;
  dsc.data_size = size;
  dsc.data = data;
  lv_obj_t *img = lv_image_create(parent);
  lv_image_set_src(img, &dsc);
  lv_obj_set_style_image_recolor(img, lv_color_hex(UI_ICON_GREY), 0);
  lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
}

static void refresh(bool) {
  GatewayView v;

  if (!appView(gw, v) || !v.paired) {
    uiShowHome();
    return;
  }
  lv_label_set_text(uiHeaderTitle(), v.name);
  bool ble = v.stateValid && (v.state.flags & LVP_STATE_BLE);
  bool on = v.stateValid && v.state.power == 1;
  uiDotColor(dotOnline, v.online ? 1 : 0);
  uiDotColor(dotPower, !v.stateValid || v.state.power < 0 ? -1 : on ? 1 : 0);

  bool active = v.online && ble && on; // Farbe und Helligkeit nur eingeschaltet und mit Band verbunden (wie Legacy)
  lastActive = active;
  uiSetEnabled(bar, active);

  // Helligkeit: Eingabe waehrend Druck/Versand nicht ueberschreiben
  if (v.stateValid && !v.pending && !lv_obj_has_state(bar, LV_STATE_PRESSED)) {
    lv_slider_set_value(bar, v.state.brightness < 5 ? 5 : v.state.brightness, LV_ANIM_OFF);
    char t[16];
    snprintf(t, sizeof(t), "%u %%", v.state.brightness);
    lv_label_set_text(barLbl, t);
  }

  // Farbe: Palettenindex nur bei exakter Uebereinstimmung (wie Legacy setRGB)
  selected = -1;
  for (int k = 0; v.stateValid && k < LeviPalette::N; k++) {
    uint8_t r, g, b;
    LeviPalette::get(k, r, g, b);
    if (r == v.state.r && g == v.state.g && b == v.state.b && !(v.state.flags & LVP_STATE_RAW)) {
      selected = k;
    }
  }
  for (int k = 0; k < LeviPalette::N; k++) {
    lv_obj_set_style_border_width(dots[k], k == selected ? 2 : 0, 0);
    uiSetEnabled(dots[k], active);
  }
  char name[40], hex[12] = "";
  snprintf(name, sizeof(name), "%s", selected >= 0 ? LeviPalette::name(selected) : "Eigene Farbe");
  if (v.stateValid) {
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", v.state.r, v.state.g, v.state.b);
  }
  lv_label_set_text(pickName, name);
  lv_label_set_text(pickHex, hex);

}

void uiShowDetail(int idx) {
  gw = idx;
  lv_obj_t *content = uiScreen("", back, refresh);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
  lv_obj_set_scrollable(content, false);
  lv_obj_set_style_pad_all(content, 4, 0);

  // Statuspunkte in der Kopfzeile: Gateway per ESP-NOW erreichbar, Licht an/aus
  lv_obj_t *h = uiHeader();
  icon(h, gatewayDsc, kGatewayA8, sizeof(kGatewayA8));
  dotOnline = uiDot(h, 10);
  icon(h, bulbDsc, kBulbA8, sizeof(kBulbA8));
  dotPower = uiDot(h, 10);

  // Farbrad
  ring = lv_obj_create(content);
  lv_obj_set_size(ring, 176, 188);
  lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ring, 0, 0);
  lv_obj_set_style_pad_all(ring, 0, 0);
  lv_obj_set_scrollable(ring, false);
  lv_obj_set_clickable(ring, true);
  lv_obj_add_event_cb(ring, ringClicked, LV_EVENT_CLICKED, nullptr);
  const float ringR = 74.0f;
  for (int k = 0; k < LeviPalette::N; k++) {
    dots[k] = lv_obj_create(ring);
    lv_obj_set_size(dots[k], 20, 20);
    lv_obj_set_scrollable(dots[k], false);
    lv_obj_set_style_radius(dots[k], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(dots[k], 0, 0);
    lv_obj_set_style_border_color(dots[k], lv_color_white(), 0);
    lv_obj_set_style_shadow_width(dots[k], 0, 0);
    uint8_t r, g, b;
    LeviPalette::get(k, r, g, b);
    lv_obj_set_style_bg_color(dots[k], lv_color_make(r, g, b), 0);
    lv_obj_set_style_bg_opa(dots[k], LV_OPA_COVER, 0);
    lv_obj_set_ext_click_area(dots[k], 4);
    float a = -1.5707963f + 6.2831853f * k / LeviPalette::N;
    lv_obj_align(dots[k], LV_ALIGN_CENTER, (int)lroundf(cosf(a) * ringR), (int)lroundf(sinf(a) * ringR));
    lv_obj_add_event_cb(dots[k], dotClicked, LV_EVENT_CLICKED, (void *)(intptr_t)k);
  }
  pickName = lv_label_create(ring);
  lv_obj_align(pickName, LV_ALIGN_CENTER, 0, -10);
  pickHex = lv_label_create(ring);
  lv_theme_shadcn_set_text_role(pickHex, LV_THEME_SHADCN_TEXT_MUTED);
  lv_obj_align(pickHex, LV_ALIGN_CENTER, 0, 12);

  // rechte Spalte: Helligkeit
  lv_obj_t *col = lv_obj_create(content);
  lv_obj_set_flex_grow(col, 1);
  lv_obj_set_height(col, LV_PCT(100));
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_all(col, 2, 0);
  lv_obj_set_style_pad_row(col, 14, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scrollable(col, false);

  barLbl = lv_label_create(col);
  lv_obj_set_style_text_font(barLbl, &lv_font_montserrat_32, 0);
  lv_label_set_text(barLbl, "-");
  bar = lv_slider_create(col);
  lv_slider_set_range(bar, 5, 100);
  lv_obj_set_size(bar, LV_PCT(90), 16);
  lv_obj_add_event_cb(bar, barChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  refresh(true);
}
