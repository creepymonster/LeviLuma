#pragma once
// RockBase NM-CYD-C5: Display ST7789 (240x320, hier quer 320x240), Touch XPT2046 (resistiv),
// beide am gemeinsamen SPI-Bus (SCK 6, MOSI 7, MISO 2). Pins aus Kconfig, Standardwerte nach
// RockBase-README, TFT_eSPI-Setup und Schaltplan SCH_NM-CYD-C5-v1.0 (siehe README.md).
#include <stdbool.h>
#include <stdint.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "touch_cal.h"

static constexpr int LCD_W = 320;
static constexpr int LCD_H = 240;

struct TouchRaw {
  bool pressed;
  int32_t x, y, z; // Rohwerte 0..4095, Druck
};

// Display, Hintergrundbeleuchtung und Touch initialisieren. Callback wird nach jeder
// abgeschlossenen Farbuebertragung aufgerufen (ISR-Kontext).
void boardInit(esp_lcd_panel_io_color_trans_done_cb_t onFlushDone, void *ctx);
esp_lcd_panel_handle_t boardPanel();
void boardBacklight(uint8_t percent);
TouchRaw boardTouchRaw();

// Touch-Kalibrierung (NVS); ohne gespeicherte Werte gilt die lineare Abbildung aus Kconfig.
bool boardTouchCalLoad(TouchCal &out);
bool boardTouchCalSave(const TouchCal &cal);
void boardTouchCalDefault(TouchCal &out);
