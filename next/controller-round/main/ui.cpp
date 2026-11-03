// Runde LVGL-Oberflaeche (240x240) im LeviLuma-Next-Skin (shadcn zinc_dark, Montserrat, Statuspunkte
// und Symbole wie Next). Drei Seiten untereinander (senkrecht wischen, Seitenpunkte rechts):
//   0 Hauptansicht: Farbscheibe = Ein/Aus, Helligkeitsbogen, Status (ESP-NOW-Verbindung, Licht an/aus)
//   1 Farbe: Farbrad (Farbton/Saettigung) mit Palettenring (LeviPalette)
//   2 Gateway: Name, Status, Abgleich, Entkoppeln bzw. Suchen und Koppeln
// Der Musikmodus hat bewusst kein Bedienelement, solange er am Band nicht getestet ist
// (die ESP-NOW-Befehle im gemeinsamen Protokoll bleiben bestehen).
// Dazu eigene Bildschirme fuer die Gateway-Suche und die Schluesseleingabe.
//
// Zustand: Angezeigt wird der vom Gateway bestaetigte LightState. Nur waehrend der Finger auf
// Bogen oder Farbrad liegt bzw. ein Befehl unterwegs ist, zeigt die Oberflaeche den Eingabewert.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "BulbIcon.h"
#include "GatewayIcon.h"
#include "Palette.h"
#include "app.h"
#include "levi_key.h"
#include "lv_theme_shadcn.h"
#include "lvgl_port.h"
#include "round_core.h"
#include "sdkconfig.h"
#include "ui.h"

static constexpr uint32_t UI_GREEN = 0x79DA89; // wie Legacy/Next
static constexpr uint32_t UI_RED = 0xE5484D;
static constexpr uint32_t UI_ICON_GREY = 0x9AA3AE;
static constexpr uint32_t UI_OFF = 0x3F3F46; // Farbscheibe bei ausgeschalteter Lampe
static constexpr int WHEEL_D = 150;          // Durchmesser Farbrad

static lv_theme_shadcn_palette_t themePalette{};
static constexpr int PAGES = 3;
static lv_obj_t *mainScr, *tv, *pager[PAGES];
// Hauptansicht
static lv_obj_t *dotGw, *dotPower, *gwName, *arc, *disc, *discSym, *discLbl, *brightLbl, *statusLbl, *setupBox;
// Farbe
static lv_obj_t *wheel, *marker, *palDots[LeviPalette::N];
static uint8_t wheelBuf[WHEEL_D * WHEEL_D * 2];
// Gateway
static lv_obj_t *gwTitle, *gwInfo, *gwPaired, *gwUnpaired;

static bool arcActive = false, wheelActive = false;
static uint8_t inputRgb[3] = {255, 255, 255};
static Round::Throttle arcThrottle(CONFIG_LEVILUMA_INPUT_SEND_MS), wheelThrottle(CONFIG_LEVILUMA_INPUT_SEND_MS);
static uint32_t lastVersion = 0, lastFull = 0;
static GatewayView view;
static bool haveView = false;
enum class Overlay : uint8_t { None, Search, Key };
static Overlay overlay = Overlay::None; // eigener Bildschirm ueber der Hauptansicht

// ---------------- Hilfen ----------------

static lv_obj_t *dot(lv_obj_t *parent, int size) {
  lv_obj_t *d = lv_obj_create(parent);
  lv_obj_set_size(d, size, size);
  lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(d, 0, 0);
  lv_obj_set_style_pad_all(d, 0, 0);
  lv_obj_set_scrollable(d, false);
  lv_obj_set_clickable(d, false);
  return d;
}

static void dotState(lv_obj_t *d, int s) { // 1 gruen, 0 rot, -1 weiss (unbekannt)
  lv_obj_set_style_bg_color(d, s > 0 ? lv_color_hex(UI_GREEN) : s == 0 ? lv_color_hex(UI_RED) : lv_color_white(), 0);
  lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
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

static lv_obj_t *label(lv_obj_t *parent, const char *t, bool muted = false) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, t);
  if (muted) {
    lv_theme_shadcn_set_text_role(l, LV_THEME_SHADCN_TEXT_MUTED);
  }
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  return l;
}

static lv_obj_t *button(lv_obj_t *parent, const char *t, lv_event_cb_t cb, bool primary = true) {
  lv_obj_t *b = lv_button_create(parent);
  lv_theme_shadcn_set_variant(b, primary ? LV_THEME_SHADCN_VARIANT_DEFAULT : LV_THEME_SHADCN_VARIANT_OUTLINE);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, t);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}

static lv_obj_t *plainBox(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_set_scrollable(o, false);
  return o;
}

static void setEnabled(lv_obj_t *o, bool en) {
  lv_obj_set_state(o, LV_STATE_DISABLED, !en);
}

static bool usable() { // Gateway online und mit dem Band verbunden
  return haveView && view.online && view.stateValid && (view.state.flags & LVP_STATE_BLE);
}

static bool lampOn() {
  return haveView && view.stateValid && view.state.power == 1;
}

static void toast(const char *t) {
  lv_obj_t *m = lv_msgbox_create(nullptr);
  lv_msgbox_add_text(m, t);
  lv_msgbox_add_close_button(m);
  lv_obj_set_width(m, 180);
}

static void reportError(lvp_err_t e) {
  if (e != LVP_OK) {
    toast(appErrorText(e, false));
  }
}

// ---------------- Hauptansicht ----------------

static void discClicked(lv_event_t *) {
  if (!usable()) {
    return;
  }
  reportError(appSetPower(!lampOn()));
}

static void arcEvent(lv_event_t *e) {
  lv_event_code_t c = lv_event_get_code(e);
  int v = lv_arc_get_value(arc);
  char t[12];
  snprintf(t, sizeof(t), "%d %%", v);

  if (c == LV_EVENT_PRESSED) {
    arcActive = true;
    arcThrottle.reset();
  } else if (c == LV_EVENT_VALUE_CHANGED && arcActive) {
    lv_label_set_text(brightLbl, t); // vorlaeufiger Eingabewert
    if (arcThrottle.update(appMillis())) {
      appSetBrightness((uint8_t)v);
    }
  } else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) {
    if (arcThrottle.release(appMillis())) {
      appSetBrightness((uint8_t)v);
    }
    arcActive = false;
  }
}

static void setupClicked(lv_event_t *) {
  lv_tileview_set_tile_by_index(tv, 0, 2, LV_ANIM_ON);
}

static void buildMain(lv_obj_t *tile) {
  static lv_image_dsc_t gwDsc, bulbDsc;
  lv_obj_t *row = plainBox(tile);
  lv_obj_set_size(row, LV_SIZE_CONTENT, 16);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 4, 0);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 36); // innerhalb des Helligkeitsbogens
  icon(row, gwDsc, kGatewayA8, sizeof(kGatewayA8));
  dotGw = dot(row, 8);
  icon(row, bulbDsc, kBulbA8, sizeof(kBulbA8));
  dotPower = dot(row, 8);

  // Helligkeitsbogen: Wertebereich wie Protokoll (LVP_BRIGHTNESS_MIN..MAX = 1..100)
  arc = lv_arc_create(tile);
  lv_obj_set_size(arc, 206, 206);
  lv_obj_center(arc);
  lv_arc_set_rotation(arc, 135);
  lv_arc_set_bg_angles(arc, 0, 270);
  lv_arc_set_range(arc, LVP_BRIGHTNESS_MIN, LVP_BRIGHTNESS_MAX);
  lv_obj_set_style_arc_width(arc, 12, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 12, LV_PART_INDICATOR);
  lv_obj_add_event_cb(arc, arcEvent, LV_EVENT_ALL, nullptr);

  gwName = label(tile, "", true);
  lv_label_set_long_mode(gwName, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_width(gwName, 120);
  lv_obj_align(gwName, LV_ALIGN_TOP_MID, 0, 50);

  // Farbscheibe: zeigt die bestaetigte Farbe (grau, wenn aus) und schaltet Ein/Aus
  disc = lv_obj_create(tile);
  lv_obj_set_size(disc, 104, 104);
  lv_obj_center(disc);
  lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(disc, 0, 0);
  lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(disc, false);
  lv_obj_set_clickable(disc, true);
  lv_obj_add_event_cb(disc, discClicked, LV_EVENT_CLICKED, nullptr);
  discSym = lv_label_create(disc);
  lv_label_set_text(discSym, LV_SYMBOL_POWER);
  lv_obj_set_style_text_font(discSym, &lv_font_montserrat_28, 0);
  lv_obj_align(discSym, LV_ALIGN_CENTER, 0, -10);
  discLbl = lv_label_create(disc);
  lv_obj_align(discLbl, LV_ALIGN_CENTER, 0, 18);

  brightLbl = label(tile, "");
  lv_obj_set_style_text_font(brightLbl, &lv_font_montserrat_20, 0);
  lv_obj_align(brightLbl, LV_ALIGN_CENTER, 0, 72);
  statusLbl = label(tile, "", true);
  lv_label_set_long_mode(statusLbl, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_width(statusLbl, 130);
  lv_obj_align(statusLbl, LV_ALIGN_CENTER, 0, 96);

  // ohne gekoppeltes Gateway: Hinweis statt Bedienung
  setupBox = plainBox(tile);
  lv_obj_set_size(setupBox, 170, LV_SIZE_CONTENT);
  lv_obj_center(setupBox);
  lv_obj_set_flex_flow(setupBox, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(setupBox, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(setupBox, 10, 0);
  label(setupBox, "Kein Gateway");
  button(setupBox, "Einrichten", setupClicked);
}

static uint32_t textColorFor(uint8_t r, uint8_t g, uint8_t b) {
  return (r * 299 + g * 587 + b * 114) / 1000 > 150 ? 0x000000 : 0xFFFFFF;
}

static void refreshMain() {
  bool paired = haveView && view.paired;
  lv_obj_set_hidden(setupBox, paired);
  lv_obj_set_hidden(arc, !paired);
  lv_obj_set_hidden(disc, !paired);
  lv_obj_set_hidden(brightLbl, !paired);
  lv_obj_set_hidden(statusLbl, !paired);
  lv_obj_set_hidden(gwName, !paired);
  dotState(dotGw, paired ? (view.online ? 1 : 0) : -1);
  dotState(dotPower, paired && view.stateValid && view.state.power >= 0 ? (view.state.power == 1 ? 1 : 0) : -1);
  if (!paired) {
    return;
  }
  lv_label_set_text(gwName, view.name);
  bool on = lampOn();
  bool use = usable();
  setEnabled(disc, use);
  setEnabled(arc, use && on);
  uint8_t r = view.state.r, g = view.state.g, b = view.state.b;
  lv_color_t col = on ? lv_color_make(r, g, b) : lv_color_hex(UI_OFF);
  lv_obj_set_style_bg_color(disc, col, 0);
  uint32_t fg = on ? textColorFor(r, g, b) : 0xFFFFFF;
  lv_obj_set_style_text_color(discSym, lv_color_hex(fg), 0);
  lv_obj_set_style_text_color(discLbl, lv_color_hex(fg), 0);
  lv_obj_set_style_opa(disc, use ? LV_OPA_COVER : LV_OPA_50, 0);
  lv_label_set_text(discLbl, !view.stateValid || view.state.power < 0 ? "?" : on ? "An" : "Aus");

  // Helligkeit: bestaetigter Wert, ausser waehrend der Eingabe oder solange ein Befehl unterwegs ist
  if (view.stateValid && !arcActive && !view.pending) {
    lv_arc_set_value(arc, view.state.brightness);
    char t[12];
    snprintf(t, sizeof(t), "%u %%", view.state.brightness);
    lv_label_set_text(brightLbl, t);
  }
  const char *st;
  if (!view.online) {
    st = "Gateway offline";
  } else if (view.pending) {
    st = "Wird gesendet ...";
  } else if (view.lastTimeout || view.lastError) {
    st = appErrorText(view.lastError, view.lastTimeout);
  } else if (!view.stateValid) {
    st = "Abgleich ...";
  } else if (!(view.state.flags & LVP_STATE_BLE)) {
    st = "Band getrennt";
  } else {
    st = "Bestaetigt";
  }
  lv_label_set_text(statusLbl, st);
}

// ---------------- Farbe ----------------

static void placeMarker(const uint8_t rgb[3]) {
  float dx, dy;
  Round::rgbToWheel(rgb, WHEEL_D / 2.f - 4, &dx, &dy);
  lv_obj_align(marker, LV_ALIGN_CENTER, (int)lroundf(dx), (int)lroundf(dy));
  lv_obj_set_style_bg_color(marker, lv_color_make(rgb[0], rgb[1], rgb[2]), 0);
}

static void wheelEvent(lv_event_t *e) {
  lv_event_code_t c = lv_event_get_code(e);
  if (!(usable() && lampOn())) {
    return;
  }
  if (c == LV_EVENT_PRESSED || c == LV_EVENT_PRESSING) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_coords(wheel, &a);
    float dx = p.x - (a.x1 + a.x2) / 2.f, dy = p.y - (a.y1 + a.y2) / 2.f;
    if (c == LV_EVENT_PRESSED) {
      wheelActive = true;
      wheelThrottle.reset();
    }
    Round::wheelToRgb(dx, dy, WHEEL_D / 2.f - 4, inputRgb);
    placeMarker(inputRgb); // vorlaeufiger Eingabewert
    if (wheelThrottle.update(appMillis())) {
      appSetColor(inputRgb[0], inputRgb[1], inputRgb[2]);
    }
  } else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) {
    if (wheelThrottle.release(appMillis())) {
      appSetColor(inputRgb[0], inputRgb[1], inputRgb[2]);
    }
    wheelActive = false;
  }
}

static void paletteClicked(lv_event_t *e) {
  if (!(usable() && lampOn())) {
    return;
  }
  uint8_t r, g, b;
  LeviPalette::get((int)(intptr_t)lv_event_get_user_data(e), r, g, b);
  reportError(appSetColor(r, g, b));
}

static void buildColor(lv_obj_t *tile) {
  // Farbrad einmal berechnen (Farbton = Winkel, Saettigung = Abstand), rund beschnitten
  wheel = lv_canvas_create(tile);
  lv_canvas_set_buffer(wheel, wheelBuf, WHEEL_D, WHEEL_D, LV_COLOR_FORMAT_RGB565);
  lv_canvas_fill_bg(wheel, lv_color_black(), LV_OPA_COVER);
  float r = WHEEL_D / 2.f;
  for (int y = 0; y < WHEEL_D; y++) {
    for (int x = 0; x < WHEEL_D; x++) {
      float dx = x + 0.5f - r, dy = y + 0.5f - r;
      if (dx * dx + dy * dy > r * r) {
        continue;
      }
      uint8_t c[3];
      Round::wheelToRgb(dx, dy, r - 4, c);
      lv_canvas_set_px(wheel, x, y, lv_color_make(c[0], c[1], c[2]), LV_OPA_COVER);
    }
  }
  lv_obj_set_style_radius(wheel, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_clip_corner(wheel, true, 0);
  lv_obj_center(wheel);
  lv_obj_set_clickable(wheel, true);
  lv_obj_set_scroll_chain(wheel, false); // Ziehen im Rad blaettert nicht die Seite
  lv_obj_add_event_cb(wheel, wheelEvent, LV_EVENT_ALL, nullptr);

  marker = dot(tile, 18);
  lv_obj_set_style_border_width(marker, 3, 0);
  lv_obj_set_style_border_color(marker, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(marker, 6, 0);
  lv_obj_set_style_shadow_opa(marker, LV_OPA_50, 0);

  // Palettenring aus LeviPalette (wie Next/Legacy), oben beginnend im Uhrzeigersinn
  const float ringR = 100.f;
  for (int k = 0; k < LeviPalette::N; k++) {
    palDots[k] = lv_obj_create(tile);
    lv_obj_set_size(palDots[k], 16, 16);
    lv_obj_set_scrollable(palDots[k], false);
    lv_obj_set_style_radius(palDots[k], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(palDots[k], 0, 0);
    lv_obj_set_style_border_color(palDots[k], lv_color_white(), 0);
    uint8_t pr, pg, pb;
    LeviPalette::get(k, pr, pg, pb);
    lv_obj_set_style_bg_color(palDots[k], lv_color_make(pr, pg, pb), 0);
    lv_obj_set_style_bg_opa(palDots[k], LV_OPA_COVER, 0);
    lv_obj_set_ext_click_area(palDots[k], 5);
    float a = -1.5707963f + 6.2831853f * k / LeviPalette::N;
    lv_obj_align(palDots[k], LV_ALIGN_CENTER, (int)lroundf(cosf(a) * ringR), (int)lroundf(sinf(a) * ringR));
    lv_obj_add_event_cb(palDots[k], paletteClicked, LV_EVENT_CLICKED, (void *)(intptr_t)k);
  }
}

static void refreshColor() {
  bool active = usable() && lampOn();
  lv_obj_set_style_opa(wheel, active ? LV_OPA_COVER : LV_OPA_40, 0);
  for (int k = 0; k < LeviPalette::N; k++) {
    uint8_t r, g, b;
    LeviPalette::get(k, r, g, b);
    bool sel = haveView && view.stateValid && r == view.state.r && g == view.state.g && b == view.state.b;
    lv_obj_set_style_border_width(palDots[k], sel ? 2 : 0, 0);
    lv_obj_set_style_opa(palDots[k], active ? LV_OPA_COVER : LV_OPA_40, 0);
  }
  lv_obj_set_hidden(marker, !(haveView && view.stateValid));
  if (haveView && view.stateValid && !wheelActive && !view.pending) {
    uint8_t c[3] = {view.state.r, view.state.g, view.state.b};
    placeMarker(c);
  }
}

// ---------------- Gateway ----------------

static void showSearch();

static void searchClicked(lv_event_t *) {
  showSearch();
}

static void resyncClicked(lv_event_t *) {
  appResync();
  toast("Abgleich gestartet");
}

static void unpairOk(lv_event_t *e) {
  lv_msgbox_close((lv_obj_t *)lv_event_get_user_data(e));
  appUnpair();
}

static void unpairCancel(lv_event_t *e) {
  lv_msgbox_close((lv_obj_t *)lv_event_get_user_data(e));
}

static void unpairClicked(lv_event_t *) {
  lv_obj_t *m = lv_msgbox_create(nullptr);
  lv_msgbox_add_text(m, "Gateway entkoppeln?");
  lv_obj_set_width(m, 190);
  lv_obj_t *c = lv_msgbox_add_footer_button(m, "Nein");
  lv_theme_shadcn_set_variant(c, LV_THEME_SHADCN_VARIANT_OUTLINE);
  lv_obj_add_event_cb(c, unpairCancel, LV_EVENT_CLICKED, m);
  lv_obj_t *ok = lv_msgbox_add_footer_button(m, "Ja");
  lv_theme_shadcn_set_variant(ok, LV_THEME_SHADCN_VARIANT_DESTRUCTIVE);
  lv_obj_add_event_cb(ok, unpairOk, LV_EVENT_CLICKED, m);
}

static void buildGateway(lv_obj_t *tile) {
  gwPaired = plainBox(tile);
  lv_obj_set_size(gwPaired, 176, 190);
  lv_obj_center(gwPaired);
  lv_obj_set_flex_flow(gwPaired, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(gwPaired, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(gwPaired, 6, 0);
  gwTitle = label(gwPaired, "");
  lv_obj_set_style_text_font(gwTitle, &lv_font_montserrat_20, 0);
  lv_label_set_long_mode(gwTitle, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_width(gwTitle, 170);
  gwInfo = label(gwPaired, "", true);
  lv_obj_t *row = plainBox(gwPaired);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, 8, 0);
  lv_obj_t *s = button(row, LV_SYMBOL_REFRESH, resyncClicked, false);
  lv_obj_set_size(s, 48, 40);
  lv_obj_t *u = button(row, LV_SYMBOL_TRASH, unpairClicked, false);
  lv_obj_set_size(u, 48, 40);
  lv_theme_shadcn_set_variant(u, LV_THEME_SHADCN_VARIANT_DESTRUCTIVE);

  gwUnpaired = plainBox(tile);
  lv_obj_set_size(gwUnpaired, 176, LV_SIZE_CONTENT);
  lv_obj_center(gwUnpaired);
  lv_obj_set_flex_flow(gwUnpaired, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(gwUnpaired, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(gwUnpaired, 10, 0);
  label(gwUnpaired, "Kein Gateway\ngekoppelt");
  button(gwUnpaired, LV_SYMBOL_REFRESH " Suchen", searchClicked);
}

static void refreshGateway() {
  bool paired = haveView && view.paired;
  lv_obj_set_hidden(gwPaired, !paired);
  lv_obj_set_hidden(gwUnpaired, paired);
  if (!paired) {
    return;
  }
  lv_label_set_text(gwTitle, view.name);
  char seen[24], t[160];
  if (view.lastSeenAgoMs == UINT32_MAX) {
    snprintf(seen, sizeof(seen), "nie gesehen");
  } else {
    snprintf(seen, sizeof(seen), "vor %lu s", (unsigned long)(view.lastSeenAgoMs / 1000));
  }
  snprintf(t, sizeof(t), "%s, %s\nBand %s\nID ..%02X:%02X:%02X\n%s", view.online ? "online" : "offline", seen,
           view.stateValid && (view.state.flags & LVP_STATE_BLE) ? "verbunden" : "getrennt", view.id[3], view.id[4], view.id[5],
           view.hasKey ? "verschluesselt" : "ohne Schluessel");
  lv_label_set_text(gwInfo, t);
}

// ---------------- Suche und Kopplung ----------------

static lv_obj_t *searchList, *searchStatus;
static int pairingIdx = -1;
static bool pairUsedKey = false;
static uint32_t searchSig = 0, lastDiscover = 0;
static void showKey(int idx);

static void closeOverlay() {
  overlay = Overlay::None;
  searchList = nullptr;
  pairingIdx = -1;
  lv_screen_load_anim(mainScr, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true); // loescht den Overlay-Bildschirm
  lastVersion = 0;
}

static void backClicked(lv_event_t *) {
  closeOverlay();
}

static void startPair(int idx, const uint8_t *key) {
  lvp_err_t e = appPair(idx, key);
  if (e != LVP_OK) {
    lv_label_set_text(searchStatus, appErrorText(e, false));
    return;
  }
  pairingIdx = idx;
  pairUsedKey = key != nullptr;
  lv_label_set_text(searchStatus, "Kopple ...");
}

static void gatewayPicked(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  GatewayView v;
  if (!appView(idx, v)) {
    return;
  }
  if (v.caps & LVP_GW_KEY_REQUIRED) {
    showKey(idx);
  } else {
    startPair(idx, nullptr);
  }
}

static lv_obj_t *overlayScreen(const char *title) {
  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_scrollable(scr, false);
  lv_obj_t *t = label(scr, title);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 18);
  lv_obj_t *b = button(scr, LV_SYMBOL_LEFT, backClicked, false);
  lv_obj_set_size(b, 64, 34);
  lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, -12);
  return scr;
}

static void buildSearchList() {
  lv_obj_clean(searchList);
  GatewayView v;
  int n = 0;
  for (int i = 0; i < appGatewayCount(); i++) {
    if (!appView(i, v) || v.paired || !v.online) {
      continue;
    }
    char line[48];
    snprintf(line, sizeof(line), "%s%s", (v.caps & LVP_GW_KEY_REQUIRED) ? LV_SYMBOL_EYE_CLOSE " " : "", v.name);
    lv_obj_t *b = lv_button_create(searchList);
    lv_theme_shadcn_set_variant(b, LV_THEME_SHADCN_VARIANT_OUTLINE);
    lv_obj_set_width(b, LV_PCT(100));
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, line);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(l, LV_PCT(100), lv_font_get_line_height(lv_obj_get_style_text_font(l, LV_PART_MAIN))); // einzeilig
    lv_obj_add_event_cb(b, gatewayPicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    n++;
  }
  if (!n) {
    label(searchList, "Suche ...", true);
  }
}

static void refreshSearch() {
  uint32_t now = appMillis();
  if (now - lastDiscover > 3000) { // waehrend der Suche haeufiger als die 15-s-Grundsuche
    lastDiscover = now;
    appDiscover();
  }
  uint32_t sig = 2166136261u;
  GatewayView v;
  for (int i = 0; i < appGatewayCount(); i++) {
    if (appView(i, v) && !v.paired && v.online) {
      sig = (sig ^ (uint32_t)(i + 1)) * 16777619u;
      sig = (sig ^ v.caps) * 16777619u;
      for (const char *s = v.name; *s; s++) {
        sig = (sig ^ (uint8_t)*s) * 16777619u;
      }
    }
  }
  if (sig != searchSig) {
    searchSig = sig;
    buildSearchList();
  }
  if (pairingIdx >= 0 && appView(pairingIdx, v)) {
    if (v.paired) {
      closeOverlay();
      lv_tileview_set_tile_by_index(tv, 0, 0, LV_ANIM_OFF);
      toast("Gekoppelt");
      return;
    }
    if (v.pairPhase == PairPhase::Failed) {
      pairingIdx = -1;
      lv_label_set_text(searchStatus, v.lastTimeout && !v.lastError ? (pairUsedKey ? "Schluessel falsch?" : "Keine Antwort")
                                                                    : appErrorText(v.lastError, false));
    }
  }
}

static void showSearch() {
  if (!appMayPair()) {
    return;
  }
  lv_obj_t *scr = overlayScreen("Gateway waehlen");
  searchList = lv_obj_create(scr);
  lv_obj_set_size(searchList, 170, 120);
  lv_obj_align(searchList, LV_ALIGN_CENTER, 0, -4);
  lv_obj_set_style_bg_opa(searchList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(searchList, 0, 0);
  lv_obj_set_style_pad_all(searchList, 2, 0);
  lv_obj_set_style_pad_row(searchList, 6, 0);
  lv_obj_set_flex_flow(searchList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(searchList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  searchStatus = label(scr, "", true);
  lv_label_set_long_mode(searchStatus, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_width(searchStatus, 160);
  lv_obj_align(searchStatus, LV_ALIGN_BOTTOM_MID, 0, -52);
  searchSig = 0;
  lastDiscover = 0;
  pairingIdx = -1;
  lv_obj_t *old = lv_screen_active();
  lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, false); // Hauptbildschirm bleibt erhalten
  if (old != mainScr) {
    lv_obj_delete(old); // vorheriges Overlay (Schluesseleingabe) ersetzen
  }
  overlay = Overlay::Search;
  refreshSearch();
}

// Schluesseleingabe: 32 Hex-Zeichen ueber eine 4x4-Tastatur im inneren Quadrat des Kreises
static lv_obj_t *keyLbl;
static char keyText[LEVI_KEY_HEX_LEN + 1];
static int keyIdx = -1;

static void keyShow() {
  size_t n = strlen(keyText);
  char t[64];
  snprintf(t, sizeof(t), "%s%s  %u/32", n > 8 ? ".." : "", keyText + (n > 8 ? n - 8 : 0), (unsigned)n);
  lv_label_set_text(keyLbl, t);
}

static void keyPressed(lv_event_t *e) {
  lv_obj_t *bm = (lv_obj_t *)lv_event_get_target(e);
  const char *k = lv_buttonmatrix_get_button_text(bm, lv_buttonmatrix_get_selected_button(bm));
  size_t n = strlen(keyText);
  if (!k) {
    return;
  }
  if (!strcmp(k, LV_SYMBOL_BACKSPACE)) {
    if (n) {
      keyText[n - 1] = 0;
    }
  } else if (!strcmp(k, LV_SYMBOL_OK)) {
    uint8_t key[LEVI_KEY_LEN];
    if (!levi_key_parse_hex(keyText, key)) {
      lv_label_set_text(keyLbl, "32 Zeichen noetig");
      return;
    }
    int idx = keyIdx;
    showSearch();
    startPair(idx, key);
    return;
  } else if (n < LEVI_KEY_HEX_LEN) {
    keyText[n] = k[0];
    keyText[n + 1] = 0;
  }
  keyShow();
}

static void showKey(int idx) {
  keyIdx = idx;
  keyText[0] = 0;
  lv_obj_t *old = lv_screen_active();
  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_scrollable(scr, false);
  keyLbl = label(scr, "");
  lv_obj_align(keyLbl, LV_ALIGN_TOP_MID, 0, 28);
  static const char *map[] = {"1", "2", "3", "4", "\n", "5", "6", "7", "8", "\n", "9", "0", "A", "B", "\n",
                              "C", "D", "E", "F", "\n", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, ""};
  lv_obj_t *bm = lv_buttonmatrix_create(scr);
  lv_buttonmatrix_set_map(bm, map);
  lv_obj_set_size(bm, 168, 150);
  lv_obj_align(bm, LV_ALIGN_CENTER, 0, 16);
  lv_obj_add_event_cb(bm, keyPressed, LV_EVENT_VALUE_CHANGED, nullptr);
  keyShow();
  lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, false);
  if (old != mainScr) {
    searchList = nullptr;
    lv_obj_delete(old); // Suchbildschirm durch die Tastatur ersetzen
  }
  overlay = Overlay::Key;
}

// ---------------- Rahmen ----------------

static void tileChanged(lv_event_t *) {
  lv_obj_t *t = lv_tileview_get_tile_active(tv);
  int idx = (int)lv_obj_get_y(t) / LCD_H_PX;
  for (int i = 0; i < PAGES; i++) {
    lv_obj_set_style_bg_opa(pager[i], i == idx ? LV_OPA_COVER : LV_OPA_40, 0);
  }
}

static void refreshTimer(lv_timer_t *) {
  static bool homeReturned = false;
  uint32_t now = appMillis();
  uint32_t v = appVersion();

  if (overlay == Overlay::Search) {
    refreshSearch();
  } else if (overlay == Overlay::None && (v != lastVersion || now - lastFull > 1000)) {
    lastVersion = v;
    if (now - lastFull > 1000) {
      lastFull = now;
    }
    int a = appActive();
    haveView = a >= 0 && appView(a, view);
    if (!haveView) {
      memset(&view, 0, sizeof(view));
    }
    refreshMain();
    refreshColor();
    refreshGateway();
  }
  // Lange ungenutzt: zurueck zur Hauptansicht (wie Legacy LEVI_RECENTER_MS)
  bool idle = lvglIdleMs() > CONFIG_LEVILUMA_HOME_MS;
  if (idle && !homeReturned) {
    if (overlay != Overlay::None) {
      closeOverlay();
    }
    lv_tileview_set_tile_by_index(tv, 0, 0, LV_ANIM_OFF);
  }
  homeReturned = idle;
}

void uiBuild() {
  static const lv_theme_shadcn_fonts_t fonts = {.small = &lv_font_montserrat_14,
                                                .normal = &lv_font_montserrat_16,
                                                .large = &lv_font_montserrat_20,
                                                .mono = nullptr,
                                                .h1 = &lv_font_montserrat_48,
                                                .h2 = &lv_font_montserrat_32,
                                                .h3 = &lv_font_montserrat_28};
  themePalette = *lv_theme_shadcn_palette_zinc_dark();
  lv_theme_shadcn_init(lv_display_get_default(), &themePalette, &fonts, LV_THEME_SHADCN_FLAG_COMPACT);

  overlay = Overlay::None;
  searchList = nullptr;
  mainScr = lv_screen_active();
  lv_obj_set_scrollable(mainScr, false);
  tv = lv_tileview_create(mainScr);
  lv_obj_set_size(tv, LCD_W_PX, LCD_H_PX);
  lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
  lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
  lv_obj_t *t0 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_BOTTOM);
  lv_obj_t *t1 = lv_tileview_add_tile(tv, 0, 1, (lv_dir_t)(LV_DIR_TOP | LV_DIR_BOTTOM));
  lv_obj_t *t2 = lv_tileview_add_tile(tv, 0, 2, LV_DIR_TOP);
  lv_obj_t *tiles[PAGES] = {t0, t1, t2};
  for (lv_obj_t *t : tiles) {
    lv_obj_set_style_pad_all(t, 0, 0);
    lv_obj_set_scrollbar_mode(t, LV_SCROLLBAR_MODE_OFF);
  }
  buildMain(t0);
  buildColor(t1);
  buildGateway(t2);
  lv_obj_add_event_cb(tv, tileChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  // Seitenpunkte rechts (innerhalb des Kreises), zeigen die aktuelle Seite
  for (int i = 0; i < PAGES; i++) {
    pager[i] = dot(mainScr, 6);
    lv_obj_set_style_bg_color(pager[i], lv_color_white(), 0);
    lv_obj_set_style_bg_opa(pager[i], i == 0 ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_align(pager[i], LV_ALIGN_RIGHT_MID, -8, -10 + i * 10);
  }
  lv_timer_create(refreshTimer, 100, nullptr);
  refreshTimer(nullptr);
}

// Fuer die Host-Vorschau: Seite direkt waehlen
void uiShowTile(int idx) {
  lv_tileview_set_tile_by_index(tv, 0, idx, LV_ANIM_OFF);
  tileChanged(nullptr);
}

void uiShowSearchForPreview() {
  showSearch();
}

void uiShowKeyForPreview(int idx) {
  showKey(idx);
}
