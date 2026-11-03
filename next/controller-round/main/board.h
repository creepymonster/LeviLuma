#pragma once
// Waveshare ESP32-S3-Touch-LCD-1.28: GC9A01 240x240 rund (SPI), Touch CST816S (I2C).
// Treiber und Parameter unveraendert aus Legacy (legacy/include/LGFX_Board.h, LovyanGFX),
// hier mit LovyanGFX als nativer ESP-IDF-Komponente (ohne Arduino).
#include <stdint.h>

static constexpr int LCD_W = 240;
static constexpr int LCD_H = 240;

void boardInit();
void boardBacklight(uint8_t value); // 0..255 wie Legacy backlight()
// Synchroner Flush wie Legacy flush(): Bereich schreiben, danach ist der Puffer frei
void boardPush(int x, int y, int w, int h, const uint16_t *px);
// Touch: true bei Beruehrung (Koordinaten bereits von LovyanGFX gedreht)
bool boardTouch(int32_t *x, int32_t *y);
