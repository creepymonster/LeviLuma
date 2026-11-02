// Drei-Punkt-Kalibrierung des resistiven Touch (XPT2046). Tippen auf drei Fadenkreuze,
// daraus eine affine Abbildung Rohwert -> Bildschirm (TouchCal), gespeichert im NVS.
#include <stdio.h>
#include "board.h"
#include "lv_theme_shadcn.h"
#include "lvgl_port.h"
#include "ui.h"

extern bool uiHoldScreen;

static const int32_t TARGETS[3][2] = {{30, 30}, {290, 120}, {60, 210}};
static int32_t raw[3][2];
static int pointIdx = 0;
static lv_obj_t *cross = nullptr, *hint = nullptr;
static lv_timer_t *timer = nullptr;
static bool firstBootMode = false;
static int64_t sumX = 0, sumY = 0;
static int samples = 0;
static bool wasPressed = false;

static void placeCross() {
  lv_obj_set_pos(cross, TARGETS[pointIdx][0] - 15, TARGETS[pointIdx][1] - 15);
  char t[64];
  snprintf(t, sizeof(t), "Punkt %d/3: Fadenkreuz genau antippen", pointIdx + 1);
  lv_label_set_text(hint, t);
}

static void finish() {
  lv_timer_delete(timer);
  timer = nullptr;
  uiHoldScreen = false;
  TouchCal cal;
  if (!cal.solve(raw, TARGETS)) {
    uiShowTouchCal(firstBootMode);
    uiToast("Punkte unbrauchbar, bitte wiederholen.");
    return;
  }
  lvglSetTouchCal(cal);
  bool saved = boardTouchCalSave(cal);
  uiShowHome();
  uiToast(saved ? "Touch kalibriert." : "Touch kalibriert, Speichern fehlgeschlagen.");
}

static void poll(lv_timer_t *) {
  int32_t rx, ry;
  bool pressed = lvglTouchRawPressed(&rx, &ry);

  if (pressed) {
    sumX += rx;
    sumY += ry;
    samples++;
  } else if (wasPressed) {
    if (samples >= 3) { // Beruehrung abgeschlossen: Mittelwert uebernehmen
      raw[pointIdx][0] = (int32_t)(sumX / samples);
      raw[pointIdx][1] = (int32_t)(sumY / samples);
      pointIdx++;
      if (pointIdx >= 3) {
        finish();
        return;
      }
      placeCross();
    }
    sumX = sumY = 0;
    samples = 0;
  }
  wasPressed = pressed;
}

void uiShowTouchCal(bool firstBoot) {
  firstBootMode = firstBoot;
  uiSetRefresh(nullptr);
  uiHoldScreen = true;
  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_scrollable(scr, false);
  hint = lv_label_create(scr);
  lv_obj_align(hint, LV_ALIGN_CENTER, 0, -10);
  lv_obj_t *sub = lv_label_create(scr);
  lv_theme_shadcn_set_text_role(sub, LV_THEME_SHADCN_TEXT_MUTED);
  lv_label_set_text(sub, firstBoot ? "Erster Start: Touch-Kalibrierung" : "Touch-Kalibrierung");
  lv_obj_align(sub, LV_ALIGN_CENTER, 0, 14);
  cross = lv_label_create(scr);
  lv_label_set_text(cross, LV_SYMBOL_PLUS);
  lv_obj_set_style_text_font(cross, &lv_font_montserrat_28, 0);
  lv_obj_set_size(cross, 30, 30);
  lv_obj_set_style_text_align(cross, LV_TEXT_ALIGN_CENTER, 0);
  pointIdx = 0;
  samples = 0;
  sumX = sumY = 0;
  wasPressed = true; // eine noch anliegende Beruehrung erst loslassen
  placeCross();
  lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
  if (timer) {
    lv_timer_delete(timer);
  }
  timer = lv_timer_create(poll, 30, nullptr);
}
