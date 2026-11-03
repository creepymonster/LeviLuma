#pragma once
// LVGL-Oberflaeche des Controllers im LeviLuma-Skin (shadcn-Theme zinc_dark wie Legacy, Montserrat).
// Alle Funktionen laufen im LVGL-Task.
#include <stdint.h>
#include "app.h"
#include "lvgl.h"

void uiBuild(); // Einstieg (vom LVGL-Port aufgerufen)

// ---- Navigation und Bausteine (ui_common.cpp) ----
typedef void (*UiRefreshFn)(bool full);
typedef void (*UiBackFn)();

// Neuer Bildschirm mit Kopfzeile. back = nullptr: kein Zurueck-Knopf. Rueckgabe: Inhaltsbereich.
lv_obj_t *uiScreen(const char *title, UiBackFn back, UiRefreshFn refresh);
void uiSetRefresh(UiRefreshFn fn); // fuer Bildschirme ohne uiScreen()
lv_obj_t *uiHeader();       // Kopfzeile des aktuellen Bildschirms (fuer Zusatzknoepfe rechts)
lv_obj_t *uiHeaderTitle();  // Titel-Label
lv_obj_t *uiCard(lv_obj_t *parent);
lv_obj_t *uiRow(lv_obj_t *parent);
lv_obj_t *uiLabel(lv_obj_t *parent, const char *text, bool muted = false);
lv_obj_t *uiButton(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user = nullptr, bool primary = true);
lv_obj_t *uiDot(lv_obj_t *parent, int size = 10);
// Liste als Flex-Spalte (lv_list ist in LVGL 9.6 abgekuendigt)
lv_obj_t *uiList(lv_obj_t *parent);
lv_obj_t *uiListText(lv_obj_t *list, const char *text); // Abschnittsueberschrift (zentriert)
lv_obj_t *uiListButton(lv_obj_t *list, const char *icon, const char *text, lv_event_cb_t cb, void *user = nullptr);
void uiDotColor(lv_obj_t *dot, int state); // 1 gruen, 0 rot, -1 weiss (unbekannt)
void uiToast(const char *text);
void uiConfirm(const char *title, const char *text, const char *okText, void (*onOk)(void *), void *user);
void uiSetEnabled(lv_obj_t *o, bool en);
uint32_t uiNow();

// Farben ausserhalb des Themes (wie Legacy, siehe AGENTS.md)
static constexpr uint32_t UI_GREEN = 0x79DA89;
static constexpr uint32_t UI_RED = 0xE5484D;
static constexpr uint32_t UI_ICON_GREY = 0x9AA3AE;

// ---- Bildschirme ----
void uiShowHome();
void uiShowDetail(int idx);
void uiShowSettings();
void uiShowGateways();
void uiShowGatewaySettings(int idx);
void uiShowPair(int idx);
void uiShowCalibration(int idx);
void uiShowTouchCal(bool firstBoot);
void uiShowInfo();
#if CONFIG_LEVILUMA_ZIGBEE_ENABLED
void uiShowZigbee();
#endif
