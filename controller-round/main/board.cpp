// Hardware des Waveshare ESP32-S3-Touch-LCD-1.28, portiert aus Legacy:
//  - LGFX_Board: wortgleiche Konfiguration aus legacy/include/LGFX_Board.h, Config.h-Werte aus Kconfig
//  - Hintergrundbeleuchtung PWM auf GPIO 2 (Legacy: ledcSetup(0, 5000, 8) / ledcAttachPin(2, 0))
//  - Drehung LEVI_DISPLAY_ROTATION / 90 (Legacy: lcd.setRotation)
#include "board.h"
#include <LovyanGFX.hpp>
#include "driver/ledc.h"
#include "esp_log.h"
#include "sdkconfig.h"

static_assert(CONFIG_LEVILUMA_DISPLAY_ROTATION == 0 || CONFIG_LEVILUMA_DISPLAY_ROTATION == 90 || CONFIG_LEVILUMA_DISPLAY_ROTATION == 180 ||
                  CONFIG_LEVILUMA_DISPLAY_ROTATION == 270,
              "LEVILUMA_DISPLAY_ROTATION muss 0, 90, 180 oder 270 sein");

#if CONFIG_LEVILUMA_DISPLAY_INVERT
#define LEVI_DISPLAY_INVERT true
#else
#define LEVI_DISPLAY_INVERT false
#endif

class LGFX_Board : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Touch_CST816S _touch;

public:
  LGFX_Board() {
    {
      auto c = _bus.config();
      c.spi_host = SPI2_HOST;
      c.spi_mode = 0;
      c.freq_write = CONFIG_LEVILUMA_DISPLAY_SPI_MHZ * 1000000;
      c.freq_read = 16000000;
      c.spi_3wire = true;
      c.use_lock = true;
      c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk = 10;
      c.pin_mosi = 11;
      c.pin_miso = -1;
      c.pin_dc = 8;
      _bus.config(c);
      _panel.setBus(&_bus);
    }
    {
      auto c = _panel.config();
      c.pin_cs = 9;
      c.pin_rst = 14;
      c.pin_busy = -1;
      c.panel_width = 240;
      c.panel_height = 240;
      c.memory_width = 240;
      c.memory_height = 240;
      c.offset_x = 0;
      c.offset_y = 0;
      c.invert = LEVI_DISPLAY_INVERT;
      c.rgb_order = false;
      c.readable = false;
      c.bus_shared = false;
      _panel.config(c);
    }
    {
      auto c = _touch.config();
      c.x_min = 0;
      c.x_max = 239;
      c.y_min = 0;
      c.y_max = 239;
      c.pin_int = -1;
      c.pin_rst = 13;
      c.bus_shared = false;
      c.offset_rotation = CONFIG_LEVILUMA_TOUCH_OFFSET_ROTATION;
      c.i2c_port = 1; /* wie Legacy: I2C-Port 1 */
      c.i2c_addr = 0x15;
      c.pin_sda = 6;
      c.pin_scl = 7;
      c.freq = 400000;
      _touch.config(c);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

static const char *TAG = "levi-board";
static LGFX_Board lcd;

void boardBacklight(uint8_t value) {
#if CONFIG_LEVILUMA_BACKLIGHT_ACTIVE_HIGH
  uint32_t duty = value;
#else
  uint32_t duty = 255 - value;
#endif
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void boardInit() {
  ledc_timer_config_t t = {};
  t.speed_mode = LEDC_LOW_SPEED_MODE;
  t.duty_resolution = LEDC_TIMER_8_BIT;
  t.timer_num = LEDC_TIMER_0;
  t.freq_hz = 5000;
  t.clk_cfg = LEDC_AUTO_CLK;
  ESP_ERROR_CHECK(ledc_timer_config(&t));
  ledc_channel_config_t c = {};
  c.gpio_num = 2;
  c.speed_mode = LEDC_LOW_SPEED_MODE;
  c.channel = LEDC_CHANNEL_0;
  c.timer_sel = LEDC_TIMER_0;
  c.duty = 0;
  ESP_ERROR_CHECK(ledc_channel_config(&c));
  boardBacklight(0);

  lcd.init();
  lcd.setRotation(CONFIG_LEVILUMA_DISPLAY_ROTATION / 90); // LovyanGFX: 0=0 Grad, 1=90, 2=180, 3=270
  lcd.fillScreen(TFT_BLACK);
  ESP_LOGI(TAG, "GC9A01 240x240 (SPI %d MHz) und CST816S bereit", CONFIG_LEVILUMA_DISPLAY_SPI_MHZ);
}

void boardPush(int x, int y, int w, int h, const uint16_t *px) {
  lcd.startWrite();
  lcd.setAddrWindow(x, y, w, h);
#if CONFIG_LEVILUMA_RGB565_SWAP
  lcd.pushPixels(px, w * h, true);
#else
  lcd.pushPixels(px, w * h, false);
#endif
  lcd.endWrite();
}

bool boardTouch(int32_t *x, int32_t *y) {
  return lcd.getTouch(x, y);
}
