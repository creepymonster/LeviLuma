// Runde LVGL-Oberflaeche (240x240) im LeviLuma-Next-Skin (shadcn zinc_dark, Montserrat, Statuspunkte
// und Symbole wie Next). Vier Seiten untereinander (senkrecht wischen, Seitenpunkte rechts):
//   0 Ein/Aus: Schalter wie auf der RockBase-Uebersicht, Statuspunkte (ESP-NOW, Licht an/aus)
//   1 Helligkeit: waagerechter Regler (1..100 % wie Protokoll) mit grossem Prozentwert (wie Legacy)
//   2 Farbe: Farbpunkte des Farbsets (LeviPalette) auf einem Kreis, Name und Hex in der Mitte,
//     Tipp rechts = naechste, links = vorige Farbe (wie die Legacy-LCD-UI)
//   3 Gateway: Name, Status, Abgleich, Entkoppeln bzw. Suchen und Koppeln
//     Ist ein Gateway gekoppelt, ist diese Seite versteckt (Kinder sollen nicht versehentlich entkoppeln).
//     Geheimer Weg: 5x schnell (innerhalb von 3 s) auf den Gateway-Namen der Seite Ein/Aus tippen.
//     Beim Verlassen der Seite und nach langer Pause ist sie wieder versteckt.
// Der Musikmodus hat bewusst kein Bedienelement, solange er am Band nicht getestet ist
// (die ESP-NOW-Befehle im gemeinsamen Protokoll bleiben bestehen).
// Dazu eigene Bildschirme fuer die Gateway-Suche und die Schluesseleingabe.
//
// Keine Statustexte: Verbindung und Licht zeigen die Statuspunkte, gesperrte Bedienelemente zeigen,
// dass gerade nichts gesteuert werden kann (Gateway offline, Band getrennt, Lampe aus).
// Zustand: Angezeigt wird der vom Gateway bestaetigte LightState. Nur waehrend der Finger auf
// dem Helligkeitsregler liegt bzw. ein Befehl unterwegs ist, zeigt die Oberflaeche den Eingabewert.
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

static lv_theme_shadcn_palette_t themePalette{};
static constexpr int PAGES = 4;
static constexpr int PAGE_POWER = 0, PAGE_BRIGHT = 1, PAGE_COLOR = 2, PAGE_GATEWAY = 3;
static lv_obj_t *mainScr, *tv, *pager[PAGES];
// Ein/Aus
static lv_obj_t *dotGw, *dotPower, *gwName, *powerSw, *setupBox;
// Helligkeit
static lv_obj_t *bright, *brightTitle, *brightLbl;
// Farbe
static lv_obj_t *colorTile, *palDots[LeviPalette::N], *colorName, *colorHex;
static int colorSel = -1; // Index der bestaetigten Farbe im Farbset, -1 = eigene Farbe
// Gateway
static lv_obj_t *gwTile, *gwTitle, *gwInfo, *gwPaired, *gwUnpaired;
// Geheimer Zugang zur Gateway-Seite, solange ein Gateway gekoppelt ist
static constexpr int SECRET_TAPS = 5;
static constexpr uint32_t SECRET_WINDOW_MS = 3000;
static bool gwUnlocked = false;
static int secretTaps = 0;
static uint32_t secretFirstMs = 0;
static void applyGatewayLock();
static void tileChanged(lv_event_t *);

static bool brightActive = false;
static Round::Throttle brightThrottle(CONFIG_LEVILUMA_INPUT_SEND_MS);
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
  // Mehr Luft wie beim RockBase-Controller (uiButton): Innenabstand und Abstand zum Text
  lv_obj_set_height(b, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_ver(b, 10, 0);
  lv_obj_set_style_pad_hor(b, 16, 0);
  lv_obj_set_style_margin_top(b, 6, 0);
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
  lv_obj_t *text = lv_msgbox_add_text(m, t);
  lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
  lv_msgbox_add_close_button(m);
  lv_obj_set_width(m, 180);
  lv_obj_set_height(m, LV_SIZE_CONTENT); // sonst waechst der Inhalt bis zum unteren Rand der runden Anzeige
}

static void reportError(lvp_err_t e) {
  if (e != LVP_OK) {
    toast(appErrorText(e, false));
  }
}

// ---------------- Seite Ein/Aus ----------------

// Schalter wie auf der RockBase-Uebersicht (ui_home.cpp): Eingabe sendet, die Anzeige folgt der Bestaetigung
static void powerChanged(lv_event_t *) {
  if (!usable()) {
    return;
  }
  reportError(appSetPower(lv_obj_has_state(powerSw, LV_STATE_CHECKED)));
}

// 5x schnell auf den Gateway-Namen tippen: Gateway-Seite freigeben und anzeigen
static void gwNameClicked(lv_event_t *) {
  uint32_t now = appMillis();
  if (secretTaps == 0 || now - secretFirstMs > SECRET_WINDOW_MS) {
    secretTaps = 0;
    secretFirstMs = now;
  }
  if (++secretTaps < SECRET_TAPS) {
    return;
  }
  secretTaps = 0;
  gwUnlocked = true;
  applyGatewayLock();
  // Ohne Animation: Die Seite ist sofort bedienbar (ein Tipp waehrend des Gleitens wuerde als Wischen gelten)
  lv_tileview_set_tile_by_index(tv, 0, PAGE_GATEWAY, LV_ANIM_OFF);
  tileChanged(nullptr);
}

static void setupClicked(lv_event_t *) {
  lv_tileview_set_tile_by_index(tv, 0, PAGE_GATEWAY, LV_ANIM_ON);
}

static void buildPower(lv_obj_t *tile) {
  static lv_image_dsc_t gwDsc, bulbDsc;
  lv_obj_t *row = plainBox(tile);
  lv_obj_set_size(row, LV_SIZE_CONTENT, 16);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 4, 0);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 24);
  icon(row, gwDsc, kGatewayA8, sizeof(kGatewayA8));
  dotGw = dot(row, 8);
  icon(row, bulbDsc, kBulbA8, sizeof(kBulbA8));
  dotPower = dot(row, 8);

  gwName = label(tile, "", true);
  lv_label_set_long_mode(gwName, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_width(gwName, 150);
  lv_obj_align(gwName, LV_ALIGN_TOP_MID, 0, 42);
  lv_obj_set_clickable(gwName, true); // geheimer Weg zur Gateway-Seite (gwNameClicked)
  lv_obj_set_ext_click_area(gwName, 8);
  lv_obj_add_event_cb(gwName, gwNameClicked, LV_EVENT_CLICKED, nullptr);

  // Ein/Aus-Schalter (Theme-Schalter, gross fuer den Finger)
  powerSw = lv_switch_create(tile);
  lv_obj_set_size(powerSw, 96, 50);
  lv_obj_align(powerSw, LV_ALIGN_CENTER, 0, 8);
  lv_obj_set_ext_click_area(powerSw, 8);
  lv_obj_add_event_cb(powerSw, powerChanged, LV_EVENT_VALUE_CHANGED, nullptr);


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

static void refreshPower() {
  bool paired = haveView && view.paired;
  lv_obj_set_hidden(setupBox, paired);
  lv_obj_set_hidden(powerSw, !paired);
  lv_obj_set_hidden(gwName, !paired);
  dotState(dotGw, paired ? (view.online ? 1 : 0) : -1);
  dotState(dotPower, paired && view.stateValid && view.state.power >= 0 ? (view.state.power == 1 ? 1 : 0) : -1);
  if (!paired) {
    return;
  }
  lv_label_set_text(gwName, view.name);
  bool on = lampOn();
  setEnabled(powerSw, usable());
  if (!view.pending) { // waehrend ein Befehl laeuft, bleibt die Eingabe sichtbar (wie RockBase)
    lv_obj_set_state(powerSw, LV_STATE_CHECKED, on);
  }
}

// ---------------- Seite Helligkeit ----------------

static void brightEvent(lv_event_t *e) {
  lv_event_code_t c = lv_event_get_code(e);
  int v = lv_slider_get_value(bright);
  char t[12];
  snprintf(t, sizeof(t), "%d %%", v);

  if (c == LV_EVENT_PRESSED) {
    brightActive = true;
    brightThrottle.reset();
  } else if (c == LV_EVENT_VALUE_CHANGED && brightActive) {
    lv_label_set_text(brightLbl, t); // vorlaeufiger Eingabewert
    if (brightThrottle.update(appMillis())) {
      appSetBrightness((uint8_t)v);
    }
  } else if (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST) {
    if (brightThrottle.release(appMillis())) {
      appSetBrightness((uint8_t)v);
    }
    brightActive = false;
  }
}

static void buildBrightness(lv_obj_t *tile) {
  // Waagerechter Regler wie Legacy (Helligkeitsseite), Wertebereich wie Protokoll (LVP_BRIGHTNESS_MIN..MAX = 1..100).
  // Ziehen im Regler blaettert nicht die Seite (die Seiten wechseln senkrecht).
  bright = lv_slider_create(tile);
  lv_slider_set_range(bright, LVP_BRIGHTNESS_MIN, LVP_BRIGHTNESS_MAX);
  lv_obj_set_size(bright, 180, 18);
  lv_obj_align(bright, LV_ALIGN_CENTER, 0, 40);
  lv_obj_set_ext_click_area(bright, 14);
  lv_obj_set_scroll_chain(bright, false);
  lv_obj_add_event_cb(bright, brightEvent, LV_EVENT_ALL, nullptr);

  brightTitle = label(tile, "Helligkeit", true);
  lv_obj_align(brightTitle, LV_ALIGN_CENTER, 0, -46);
  brightLbl = label(tile, "-");
  lv_obj_set_style_text_font(brightLbl, &lv_font_montserrat_48, 0);
  lv_obj_align(brightLbl, LV_ALIGN_CENTER, 0, -10);
}

static void refreshBrightness() {
  bool paired = haveView && view.paired;
  lv_obj_set_hidden(bright, !paired);
  lv_obj_set_hidden(brightLbl, !paired);
  lv_label_set_text(brightTitle, paired ? "Helligkeit" : "Kein Gateway");
  if (!paired) {
    return;
  }
  bool on = lampOn();
  setEnabled(bright, usable() && on);
  // bestaetigter Wert, ausser waehrend der Eingabe oder solange ein Befehl unterwegs ist
  if (view.stateValid && !brightActive && !view.pending) {
    lv_slider_set_value(bright, view.state.brightness, LV_ANIM_OFF);
    char t[12];
    snprintf(t, sizeof(t), "%u %%", view.state.brightness);
    lv_label_set_text(brightLbl, t);
  }
}

// ---------------- Farbe (wie Legacy) ----------------

// Tipp auf die Seite: rechte Haelfte naechste, linke vorige Farbe; am Ende wieder vorn (Legacy nextColorClicked)
static void colorClicked(lv_event_t *) {
  if (!(usable() && lampOn())) {
    return;
  }
  lv_indev_t *in = lv_indev_active();
  if (!in) {
    return;
  }
  lv_point_t p;
  lv_indev_get_point(in, &p);
  lv_area_t a;
  lv_obj_get_coords(colorTile, &a);
  bool right = p.x >= (a.x1 + a.x2) / 2;
  int n = (colorSel < 0) ? (right ? 0 : LeviPalette::N - 1) : (colorSel + (right ? 1 : LeviPalette::N - 1)) % LeviPalette::N;
  uint8_t r, g, b;
  LeviPalette::get(n, r, g, b);
  reportError(appSetColor(r, g, b)); // kalibriert: die Kalibrierung wendet das Gateway an
}

static void buildColor(lv_obj_t *tile) {
  colorTile = tile;
  // Farbpunkte auf einem Kreis, oben beginnend im Uhrzeigersinn (wie Legacy layoutRound240)
  const float ringR = 90.f;
  for (int k = 0; k < LeviPalette::N; k++) {
    palDots[k] = dot(tile, 16);
    lv_obj_set_style_border_color(palDots[k], lv_color_white(), 0);
    uint8_t pr, pg, pb;
    LeviPalette::get(k, pr, pg, pb);
    lv_obj_set_style_bg_color(palDots[k], lv_color_make(pr, pg, pb), 0);
    lv_obj_set_style_bg_opa(palDots[k], LV_OPA_COVER, 0);
    float a = -1.5707963f + 6.2831853f * k / LeviPalette::N;
    lv_obj_align(palDots[k], LV_ALIGN_CENTER, (int)lroundf(cosf(a) * ringR), (int)lroundf(sinf(a) * ringR));
  }
  colorName = label(tile, "");
  lv_obj_set_style_text_font(colorName, &lv_font_montserrat_20, 0);
  lv_obj_align(colorName, LV_ALIGN_CENTER, 0, -10);
  colorHex = label(tile, "", true);
  lv_obj_align(colorHex, LV_ALIGN_CENTER, 0, 14);
  lv_obj_set_clickable(tile, true);
  lv_obj_add_event_cb(tile, colorClicked, LV_EVENT_CLICKED, nullptr);
}

static void refreshColor() {
  bool active = usable() && lampOn();
  bool valid = haveView && view.stateValid;
  colorSel = -1;
  for (int k = 0; valid && k < LeviPalette::N; k++) {
    uint8_t r, g, b;
    LeviPalette::get(k, r, g, b);
    if (r == view.state.r && g == view.state.g && b == view.state.b && !(view.state.flags & LVP_STATE_RAW)) {
      colorSel = k;
    }
  }
  for (int k = 0; k < LeviPalette::N; k++) {
    lv_obj_set_style_border_width(palDots[k], k == colorSel ? 2 : 0, 0);
    lv_obj_set_style_opa(palDots[k], active ? LV_OPA_COVER : LV_OPA_40, 0);
  }
  char hex[12] = "";
  if (valid) {
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", view.state.r, view.state.g, view.state.b);
  }
  lv_label_set_text(colorName, !(haveView && view.paired) ? "Kein Gateway" : colorSel >= 0 ? LeviPalette::name(colorSel) : "Eigene Farbe");
  lv_label_set_text(colorHex, hex);
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

// Gateway-Seite verstecken, solange ein Gateway gekoppelt und die Seite nicht freigegeben ist. Eine versteckte
// Kachel zaehlt in LVGL nicht zur Scroll-Ausdehnung, die Farbseite ist dann die letzte erreichbare Seite.
static void applyGatewayLock() {
  bool locked = haveView && view.paired && !gwUnlocked;
  if (lv_obj_is_hidden(gwTile) == locked) {
    return;
  }
  if (locked && lv_tileview_get_tile_active(tv) == gwTile) {
    lv_tileview_set_tile_by_index(tv, 0, PAGE_COLOR, LV_ANIM_OFF);
  }
  lv_obj_set_hidden(gwTile, locked);
  lv_obj_set_hidden(pager[PAGE_GATEWAY], locked);
  int n = locked ? PAGES - 1 : PAGES; // Seitenpunkte senkrecht zentriert halten
  for (int i = 0; i < n; i++) {
    lv_obj_align(pager[i], LV_ALIGN_RIGHT_MID, -8, -(n - 1) * 5 + i * 10);
  }
}

static void refreshGateway() {
  applyGatewayLock();
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

// Eintraege der Suchliste waehrend einer laufenden Kopplung sperren (Darstellung durch das Theme)
static void searchListSetEnabled(bool on) {
  if (!searchList) {
    return;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(searchList); i++) {
    lv_obj_t *c = lv_obj_get_child(searchList, (int32_t)i);
    if (on) {
      lv_obj_remove_state(c, LV_STATE_DISABLED);
    } else {
      lv_obj_add_state(c, LV_STATE_DISABLED);
    }
  }
}

static void startPair(int idx, const uint8_t *key) {
  if (pairingIdx >= 0) {
    return; // Kopplung laeuft bereits
  }
  lvp_err_t e = appPair(idx, key);
  if (e != LVP_OK) {
    lv_label_set_text(searchStatus, appErrorText(e, false));
    return;
  }
  pairingIdx = idx;
  pairUsedKey = key != nullptr;
  searchListSetEnabled(false);
  GatewayView v;
  char t[48];
  snprintf(t, sizeof(t), "Kopple mit %s ...", appView(idx, v) ? v.name : "Gateway");
  lv_label_set_text(searchStatus, t);
}

static void gatewayPicked(lv_event_t *e) {
  if (pairingIdx >= 0) {
    return; // Kopplung laeuft bereits
  }
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
    if (pairingIdx >= 0) {
      lv_obj_add_state(b, LV_STATE_DISABLED); // Liste neu aufgebaut, waehrend gekoppelt wird
    }
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
      lv_tileview_set_tile_by_index(tv, 0, PAGE_POWER, LV_ANIM_OFF);
      toast("Gekoppelt");
      return;
    }
    if (v.pairPhase == PairPhase::Failed) {
      pairingIdx = -1;
      searchListSetEnabled(true);
      lv_label_set_text(searchStatus, v.lastTimeout && !v.lastError ? (pairUsedKey ? "Schluessel falsch?" : "Keine Antwort")
                                                                    : appErrorText(v.lastError, false));
    }
  }
}

static void showSearch() {
  if (!appMayPair()) {
    return;
  }
  lv_obj_t *scr = overlayScreen("Gateways");
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
  if (gwUnlocked && idx != PAGE_GATEWAY) { // Gateway-Seite verlassen: wieder verstecken
    gwUnlocked = false;
    applyGatewayLock();
  }
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
    refreshPower();
    refreshBrightness();
    refreshColor();
    refreshGateway();
  }
  // Lange ungenutzt: zurueck zur Hauptansicht (wie Legacy LEVI_RECENTER_MS)
  bool idle = lvglIdleMs() > CONFIG_LEVILUMA_HOME_MS;
  if (idle && !homeReturned) {
    if (overlay != Overlay::None) {
      closeOverlay();
    }
    lv_tileview_set_tile_by_index(tv, 0, PAGE_POWER, LV_ANIM_OFF);
    gwUnlocked = false;
    applyGatewayLock();
    tileChanged(nullptr);
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
  lv_obj_t *t0 = lv_tileview_add_tile(tv, 0, PAGE_POWER, LV_DIR_BOTTOM);
  lv_obj_t *t1 = lv_tileview_add_tile(tv, 0, PAGE_BRIGHT, (lv_dir_t)(LV_DIR_TOP | LV_DIR_BOTTOM));
  lv_obj_t *t2 = lv_tileview_add_tile(tv, 0, PAGE_COLOR, (lv_dir_t)(LV_DIR_TOP | LV_DIR_BOTTOM));
  lv_obj_t *t3 = gwTile = lv_tileview_add_tile(tv, 0, PAGE_GATEWAY, LV_DIR_TOP);
  lv_obj_t *tiles[PAGES] = {t0, t1, t2, t3};
  for (lv_obj_t *t : tiles) {
    lv_obj_set_style_pad_all(t, 0, 0);
    lv_obj_set_scrollbar_mode(t, LV_SCROLLBAR_MODE_OFF);
  }
  buildPower(t0);
  buildBrightness(t1);
  buildColor(t2);
  buildGateway(t3);
  lv_obj_add_event_cb(tv, tileChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  // Seitenpunkte rechts (innerhalb des Kreises), zeigen die aktuelle Seite
  for (int i = 0; i < PAGES; i++) {
    pager[i] = dot(mainScr, 6);
    lv_obj_set_style_bg_color(pager[i], lv_color_white(), 0);
    lv_obj_set_style_bg_opa(pager[i], i == 0 ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_align(pager[i], LV_ALIGN_RIGHT_MID, -8, -15 + i * 10);
  }
  lv_timer_create(refreshTimer, 100, nullptr);
  refreshTimer(nullptr);
}

// Fuer die Host-Vorschau: Seite direkt waehlen
void uiShowTile(int idx) {
  gwUnlocked = idx == PAGE_GATEWAY; // Vorschau zeigt die Gateway-Seite auch bei Kopplung
  applyGatewayLock();
  lv_tileview_set_tile_by_index(tv, 0, idx, LV_ANIM_OFF);
  tileChanged(nullptr);
}

void uiShowSearchForPreview() {
  showSearch();
}

void uiShowKeyForPreview(int idx) {
  showKey(idx);
}
