#pragma once
// LVGL 9.6 auf dem NM-CYD-C5: Anzeige, Touch-Eingabe, Hintergrundbeleuchtung (Abschaltung nach
// Inaktivitaet wie Legacy) und der LVGL-Task. LVGL wird ausschliesslich aus diesem Task benutzt.
#include <stdint.h>
#include "lvgl.h"
#include "touch_cal.h"

void lvglPortStart(void (*buildUi)());
uint32_t lvglIdleMs();          // ms seit der letzten Beruehrung
void lvglSetTouchCal(const TouchCal &cal);
// Rohwerte fuer den Kalibrierbildschirm: true, solange gedrueckt
bool lvglTouchRawPressed(int32_t *rx, int32_t *ry);
