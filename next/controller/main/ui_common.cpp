// Gemeinsame UI-Bausteine: Theme (wie Legacy themeInit), Bildschirmwechsel, Kopfzeile,
// periodische Aktualisierung aus dem Controller-Zustand, Dialoge.
#include "lv_theme_shadcn.h"
#include "lvgl_port.h"
#include "sdkconfig.h"
#include "ui.h"
#include "board.h"

bool uiHoldScreen = false; // Touch-Kalibrierung und Kalibrierassistent nicht unterbrechen

static UiRefreshFn refreshFn = nullptr;
static lv_obj_t *header = nullptr, *headerTitle = nullptr;
static uint32_t lastVersion = 0, lastFull = 0;
static lv_theme_shadcn_palette_t themePalette{};

uint32_t uiNow() {
  return appMillis();
}

// Farbklima und Typografie: shadcn zinc_dark unveraendert (wie Legacy), Fonts fuer 320x240 angepasst
static void themeInit() {
  static const lv_theme_shadcn_fonts_t fonts = {.small = &lv_font_montserrat_14,
                                                .normal = &lv_font_montserrat_16,
                                                .large = &lv_font_montserrat_20,
                                                .mono = nullptr,
                                                .h1 = &lv_font_montserrat_48,
                                                .h2 = &lv_font_montserrat_32,
                                                .h3 = &lv_font_montserrat_28};
  themePalette = *lv_theme_shadcn_palette_zinc_dark();
  lv_theme_shadcn_init(lv_display_get_default(), &themePalette, &fonts, LV_THEME_SHADCN_FLAG_COMPACT);
}

static void backClicked(lv_event_t *e) {
  UiBackFn fn = (UiBackFn)lv_event_get_user_data(e);
  if (fn) {
    fn();
  }
}

lv_obj_t *uiScreen(const char *title, UiBackFn back, UiRefreshFn refresh) {
  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_scrollable(scr, false);
  lv_obj_set_style_pad_all(scr, 0, 0);

  header = lv_obj_create(scr);
  lv_obj_set_size(header, LV_PCT(100), 40);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_scrollable(header, false);
  lv_obj_set_style_radius(header, 0, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_pad_hor(header, 6, 0);
  lv_obj_set_style_pad_ver(header, 0, 0);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(header, 6, 0);
  lv_theme_shadcn_set_surface(header, LV_THEME_SHADCN_SURFACE_BACKGROUND);

  if (back) {
    lv_obj_t *b = lv_button_create(header);
    lv_theme_shadcn_set_variant(b, LV_THEME_SHADCN_VARIANT_GHOST);
    lv_theme_shadcn_set_size(b, LV_THEME_SHADCN_SIZE_ICON_SM);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, LV_SYMBOL_LEFT);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, backClicked, LV_EVENT_CLICKED, (void *)back);
  }
  headerTitle = lv_label_create(header);
  lv_label_set_text(headerTitle, title);
  lv_label_set_long_mode(headerTitle, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_flex_grow(headerTitle, 1);
  lv_theme_shadcn_set_text_role(headerTitle, LV_THEME_SHADCN_TEXT_LARGE);

  lv_obj_t *sep = lv_obj_create(scr);
  lv_theme_shadcn_set_role(sep, LV_THEME_SHADCN_ROLE_SEPARATOR);
  lv_obj_set_pos(sep, 0, 40);

  lv_obj_t *content = lv_obj_create(scr);
  lv_obj_set_pos(content, 0, 41);
  lv_obj_set_size(content, LV_PCT(100), 240 - 41);
  lv_obj_set_style_radius(content, 0, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(content, 6, 0);
  lv_obj_set_style_pad_row(content, 6, 0);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);

  refreshFn = refresh;
  lastVersion = 0;
  uiHoldScreen = false;
  lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
  return content;
}

void uiSetRefresh(UiRefreshFn fn) {
  refreshFn = fn;
  lastVersion = 0;
}

lv_obj_t *uiHeader() {
  return header;
}

lv_obj_t *uiHeaderTitle() {
  return headerTitle;
}

lv_obj_t *uiCard(lv_obj_t *parent) {
  lv_obj_t *c = lv_obj_create(parent);
  lv_theme_shadcn_set_surface(c, LV_THEME_SHADCN_SURFACE_CARD);
  lv_obj_set_width(c, LV_PCT(100));
  lv_obj_set_height(c, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(c, 8, 0);
  lv_obj_set_style_pad_row(c, 4, 0);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollable(c, false);
  return c;
}

lv_obj_t *uiRow(lv_obj_t *parent) {
  lv_obj_t *r = lv_obj_create(parent);
  lv_obj_set_width(r, LV_PCT(100));
  lv_obj_set_height(r, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(r, 0, 0);
  lv_obj_set_style_pad_all(r, 0, 0);
  lv_obj_set_style_pad_column(r, 8, 0);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scrollable(r, false);
  return r;
}

lv_obj_t *uiLabel(lv_obj_t *parent, const char *text, bool muted) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  if (muted) {
    lv_theme_shadcn_set_text_role(l, LV_THEME_SHADCN_TEXT_MUTED);
  }
  // In Spalten volle Breite mit Zeilenumbruch, in Zeilen natuerliche Breite
  if (lv_obj_get_style_flex_flow(parent, LV_PART_MAIN) == LV_FLEX_FLOW_COLUMN) {
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
  }
  return l;
}

lv_obj_t *uiButton(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user, bool primary) {
  lv_obj_t *b = lv_button_create(parent);
  lv_theme_shadcn_set_variant(b, primary ? LV_THEME_SHADCN_VARIANT_DEFAULT : LV_THEME_SHADCN_VARIANT_OUTLINE);
  lv_theme_shadcn_set_size(b, LV_THEME_SHADCN_SIZE_SM);
  // Mehr Luft: Innenabstand oben/unten und Abstand zu Texten darueber/darunter (Flex-Layout)
  lv_obj_set_height(b, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_ver(b, 10, 0);
  lv_obj_set_style_pad_hor(b, 14, 0);
  lv_obj_set_style_margin_top(b, 6, 0);
  lv_obj_set_style_margin_bottom(b, 4, 0);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  if (cb) {
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
  }
  return b;
}

lv_obj_t *uiDot(lv_obj_t *parent, int size) {
  lv_obj_t *d = lv_obj_create(parent);
  lv_obj_set_size(d, size, size);
  lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(d, 0, 0);
  lv_obj_set_style_pad_all(d, 0, 0);
  lv_obj_set_scrollable(d, false);
  lv_obj_set_clickable(d, false);
  return d;
}

lv_obj_t *uiList(lv_obj_t *parent) {
  lv_obj_t *l = lv_obj_create(parent);
  lv_obj_set_size(l, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(l, 0, 0);
  lv_obj_set_style_pad_all(l, 0, 0);
  lv_obj_set_style_pad_row(l, 4, 0);
  lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollable(l, false);
  return l;
}

lv_obj_t *uiListText(lv_obj_t *list, const char *text) {
  lv_obj_t *t = uiLabel(list, text, true);
  lv_obj_set_style_pad_top(t, 4, 0);
  return t;
}

lv_obj_t *uiListButton(lv_obj_t *list, const char *icon, const char *text, lv_event_cb_t cb, void *user) {
  lv_obj_t *b = lv_button_create(list);
  lv_theme_shadcn_set_variant(b, LV_THEME_SHADCN_VARIANT_OUTLINE);
  lv_obj_set_width(b, LV_PCT(100));
  lv_obj_set_height(b, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_ver(b, 6, 0);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(b, 8, 0);
  if (icon) {
    lv_obj_t *i = lv_label_create(b);
    lv_label_set_text(i, icon);
  }
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_flex_grow(l, 1);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_LEFT, 0);
  if (cb) {
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
  }
  return b;
}

void uiDotColor(lv_obj_t *dot, int state) {
  lv_obj_set_style_bg_color(dot, state > 0 ? lv_color_hex(UI_GREEN) : state == 0 ? lv_color_hex(UI_RED) : lv_color_white(), 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
}

// Deaktiviert: nur LV_STATE_DISABLED, die Darstellung uebernimmt das Theme (wie Legacy)
void uiSetEnabled(lv_obj_t *o, bool en) {
  lv_obj_set_state(o, LV_STATE_DISABLED, !en);
}

void uiToast(const char *text) {
  lv_obj_t *m = lv_msgbox_create(nullptr);
  lv_msgbox_add_text(m, text);
  lv_msgbox_add_close_button(m);
  lv_obj_set_width(m, 280);
}

struct ConfirmCtx {
  void (*onOk)(void *);
  void *user;
  lv_obj_t *box;
};

static void confirmOk(lv_event_t *e) {
  ConfirmCtx *c = (ConfirmCtx *)lv_event_get_user_data(e);
  void (*fn)(void *) = c->onOk;
  void *user = c->user;
  lv_msgbox_close(c->box);
  delete c;
  fn(user);
}

static void confirmCancel(lv_event_t *e) {
  ConfirmCtx *c = (ConfirmCtx *)lv_event_get_user_data(e);
  lv_msgbox_close(c->box);
  delete c;
}

void uiConfirm(const char *title, const char *text, const char *okText, void (*onOk)(void *), void *user) {
  ConfirmCtx *c = new ConfirmCtx{onOk, user, nullptr};
  c->box = lv_msgbox_create(nullptr);
  lv_msgbox_add_title(c->box, title);
  lv_msgbox_add_text(c->box, text);
  lv_obj_set_width(c->box, 290);
  lv_obj_t *cancel = lv_msgbox_add_footer_button(c->box, "Abbrechen");
  lv_theme_shadcn_set_variant(cancel, LV_THEME_SHADCN_VARIANT_OUTLINE);
  lv_obj_add_event_cb(cancel, confirmCancel, LV_EVENT_CLICKED, c);
  lv_obj_t *ok = lv_msgbox_add_footer_button(c->box, okText);
  lv_theme_shadcn_set_variant(ok, LV_THEME_SHADCN_VARIANT_DESTRUCTIVE);
  lv_obj_add_event_cb(ok, confirmOk, LV_EVENT_CLICKED, c);
}

// Aktualisierung: bei neuer Controller-Version sofort, sonst einmal pro Sekunde (Zeitangaben)
static void refreshTimer(lv_timer_t *) {
  static bool homeReturned = false;
  uint32_t v = appVersion();
  uint32_t now = uiNow();

  if (refreshFn && (v != lastVersion || now - lastFull > 1000)) {
    bool full = now - lastFull > 1000;
    lastVersion = v;
    if (full) {
      lastFull = now;
    }
    refreshFn(full);
  }
  // Lange ungenutzt: zurueck zur Uebersicht (wie Legacy LEVI_RECENTER_MS), einmal pro Ruhephase
  bool idle = lvglIdleMs() > CONFIG_LEVILUMA_HOME_MS;
  if (idle && !homeReturned && !uiHoldScreen) {
    uiShowHome();
  }
  homeReturned = idle;
}

void uiBuild() {
  themeInit();
  lv_timer_create(refreshTimer, 100, nullptr);
  TouchCal cal;
#if CONFIG_LEVILUMA_TOUCH_CAL_ON_FIRST_BOOT
  if (!boardTouchCalLoad(cal)) {
    uiShowTouchCal(true);
    return;
  }
#endif
  uiShowHome();
}
