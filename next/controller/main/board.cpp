// Hardware des RockBase NM-CYD-C5 (ESP32-C5): ST7789 ueber esp_lcd, XPT2046 ueber spi_master,
// Hintergrundbeleuchtung per LEDC. Display und Touch teilen sich SPI2; der SPI-Treiber
// sorgt mit seiner Bus-Sperre dafuer, dass sich die Uebertragungen nicht ueberschneiden.
#include "board.h"
#include <string.h>
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "kv_store.h"
#include "sdkconfig.h"

static const char *TAG = "levi-board";
static constexpr spi_host_device_t HOST = SPI2_HOST;
static constexpr int LINES_PER_FLUSH = 40;

static esp_lcd_panel_handle_t panel = nullptr;
static spi_device_handle_t touchDev = nullptr;

esp_lcd_panel_handle_t boardPanel() {
  return panel;
}

void boardBacklight(uint8_t percent) {
  uint32_t duty = (uint32_t)percent * 255 / 100;
#if !CONFIG_LEVILUMA_LCD_BL_ACTIVE_HIGH
  duty = 255 - duty;
#endif
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void backlightInit() {
  ledc_timer_config_t t = {};
  t.speed_mode = LEDC_LOW_SPEED_MODE;
  t.duty_resolution = LEDC_TIMER_8_BIT;
  t.timer_num = LEDC_TIMER_0;
  t.freq_hz = 5000;
  t.clk_cfg = LEDC_AUTO_CLK;
  ESP_ERROR_CHECK(ledc_timer_config(&t));
  ledc_channel_config_t c = {};
  c.gpio_num = CONFIG_LEVILUMA_LCD_PIN_BL;
  c.speed_mode = LEDC_LOW_SPEED_MODE;
  c.channel = LEDC_CHANNEL_0;
  c.timer_sel = LEDC_TIMER_0;
#if CONFIG_LEVILUMA_LCD_BL_ACTIVE_HIGH
  c.duty = 0;
#else
  c.duty = 255;
#endif
  ESP_ERROR_CHECK(ledc_channel_config(&c));
}

void boardInit(esp_lcd_panel_io_color_trans_done_cb_t onFlushDone, void *ctx) {
  backlightInit();

  spi_bus_config_t bus = {};
  bus.sclk_io_num = CONFIG_LEVILUMA_LCD_PIN_SCLK;
  bus.mosi_io_num = CONFIG_LEVILUMA_LCD_PIN_MOSI;
  bus.miso_io_num = CONFIG_LEVILUMA_LCD_PIN_MISO;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = LCD_W * LINES_PER_FLUSH * 2 + 8;
  ESP_ERROR_CHECK(spi_bus_initialize(HOST, &bus, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_handle_t io = nullptr;
  esp_lcd_panel_io_spi_config_t ioCfg = {};
  ioCfg.cs_gpio_num = CONFIG_LEVILUMA_LCD_PIN_CS;
  ioCfg.dc_gpio_num = CONFIG_LEVILUMA_LCD_PIN_DC;
  ioCfg.spi_mode = 0;
  ioCfg.pclk_hz = CONFIG_LEVILUMA_LCD_SPI_MHZ * 1000 * 1000;
  ioCfg.trans_queue_depth = 10;
  ioCfg.on_color_trans_done = onFlushDone;
  ioCfg.user_ctx = ctx;
  ioCfg.lcd_cmd_bits = 8;
  ioCfg.lcd_param_bits = 8;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)HOST, &ioCfg, &io));

  esp_lcd_panel_dev_config_t pc = {};
  pc.reset_gpio_num = CONFIG_LEVILUMA_LCD_PIN_RST;
#if CONFIG_LEVILUMA_LCD_BGR
  pc.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
#else
  pc.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
#endif
  pc.bits_per_pixel = 16;
  ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &pc, &panel));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
#if CONFIG_LEVILUMA_LCD_INVERT
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
#else
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, false));
#endif
  // Querformat: Achsen tauschen; Spiegelung je nach Einbaulage (am Geraet pruefen)
  ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, true));
#if CONFIG_LEVILUMA_LCD_ROT_90
  ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, true, false));
#else
  ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, false, true));
#endif
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

  spi_device_interface_config_t td = {};
  td.clock_speed_hz = CONFIG_LEVILUMA_TOUCH_SPI_MHZ_X10 * 100 * 1000;
  td.mode = 0;
  td.spics_io_num = CONFIG_LEVILUMA_TOUCH_PIN_CS;
  td.queue_size = 1;
  ESP_ERROR_CHECK(spi_bus_add_device(HOST, &td, &touchDev));
  ESP_LOGI(TAG, "Display ST7789 %dx%d (SPI %d MHz) und Touch XPT2046 bereit", LCD_W, LCD_H, CONFIG_LEVILUMA_LCD_SPI_MHZ);
}

// Ein 12-Bit-Messwert des XPT2046 (Steuerbyte, dann 2 Byte Antwort)
static int32_t xptRead(uint8_t cmd) {
  spi_transaction_t t = {};
  t.length = 24;
  t.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
  t.tx_data[0] = cmd;

  if (spi_device_polling_transmit(touchDev, &t) != ESP_OK) {
    return -1;
  }
  return ((t.rx_data[1] << 8) | t.rx_data[2]) >> 3;
}

static int32_t median3(int32_t a, int32_t b, int32_t c) {
  if (a > b) {
    int32_t t = a;
    a = b;
    b = t;
  }
  if (b > c) {
    b = c;
  }
  return a > b ? a : b;
}

TouchRaw boardTouchRaw() {
  TouchRaw r = {false, 0, 0, 0};

  if (!touchDev) {
    return r;
  }
  // Druck: z = z1 + 4095 - z2 (wie TFT_eSPI)
  int32_t z1 = xptRead(0xB1), z2 = xptRead(0xC1);
  if (z1 < 0 || z2 < 0) {
    return r;
  }
  r.z = z1 + 4095 - z2;

  if (r.z < CONFIG_LEVILUMA_TOUCH_Z_THRESHOLD) {
    xptRead(0x90); // Wandler aus, Interruptleitung wieder aktiv
    return r;
  }
  int32_t x[3], y[3];
  for (int i = 0; i < 3; i++) {
    x[i] = xptRead(0xD1);
    y[i] = xptRead(0x91);
  }
  xptRead(0x90);
  r.x = median3(x[0], x[1], x[2]);
  r.y = median3(y[0], y[1], y[2]);
  r.pressed = r.x > 0 && r.y > 0 && r.x < 4095 && r.y < 4095;
  return r;
}

static NvsKvStore &touchKv() {
  static NvsKvStore kv("levitouch");
  return kv;
}

bool boardTouchCalLoad(TouchCal &out) {
  int32_t v[6];
  size_t len = sizeof(v);

  if (!touchKv().getBlob("cal", v, &len) || len != sizeof(v)) {
    return false;
  }
  out.ax = v[0];
  out.bx = v[1];
  out.cx = v[2];
  out.ay = v[3];
  out.by = v[4];
  out.cy = v[5];
  out.valid = true;
  return true;
}

bool boardTouchCalSave(const TouchCal &c) {
  int32_t v[6] = {c.ax, c.bx, c.cx, c.ay, c.by, c.cy};
  return touchKv().setBlob("cal", v, sizeof(v));
}

void boardTouchCalDefault(TouchCal &out) {
  out.setLinear(CONFIG_LEVILUMA_TOUCH_RAW_X_MIN, CONFIG_LEVILUMA_TOUCH_RAW_X_MAX, CONFIG_LEVILUMA_TOUCH_RAW_Y_MIN,
                CONFIG_LEVILUMA_TOUCH_RAW_Y_MAX, LCD_W, LCD_H,
#if CONFIG_LEVILUMA_TOUCH_SWAP_XY
                true
#else
                false
#endif
  );
}
