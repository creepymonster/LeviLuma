#pragma once
// Runde LVGL-Oberflaeche (240x240). Alle Funktionen laufen im LVGL-Task.
static constexpr int LCD_W_PX = 240;
static constexpr int LCD_H_PX = 240;

void uiBuild();
// Nur fuer die Host-Vorschau (tools/preview)
void uiShowTile(int idx);
void uiShowSearchForPreview();
void uiShowKeyForPreview(int idx);
