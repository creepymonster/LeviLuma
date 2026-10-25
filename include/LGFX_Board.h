#pragma once
#include <LovyanGFX.hpp>
#include "Config.h"
// Touch-Drehung in LovyanGFX (0..7). Standard 0; per Config.h einstellbar und testen.
#ifndef LEVI_TOUCH_OFFSET_ROTATION
#define LEVI_TOUCH_OFFSET_ROTATION 0
#endif
class LGFX_Board : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Touch_CST816S _touch;
public:
  LGFX_Board() {
    { auto c=_bus.config(); c.spi_host=SPI2_HOST; c.spi_mode=0; c.freq_write=LEVI_DISPLAY_SPI_MHZ*1000000; c.freq_read=16000000; c.spi_3wire=true; c.use_lock=true; c.dma_channel=SPI_DMA_CH_AUTO; c.pin_sclk=10; c.pin_mosi=11; c.pin_miso=-1; c.pin_dc=8; _bus.config(c); _panel.setBus(&_bus); }
    { auto c=_panel.config(); c.pin_cs=9; c.pin_rst=14; c.pin_busy=-1; c.panel_width=240; c.panel_height=240; c.memory_width=240; c.memory_height=240; c.offset_x=0; c.offset_y=0; c.invert=LEVI_DISPLAY_INVERT; c.rgb_order=false; c.readable=false; c.bus_shared=false; _panel.config(c); }
    { auto c=_touch.config(); c.x_min=0; c.x_max=239; c.y_min=0; c.y_max=239; c.pin_int=-1; c.pin_rst=13; c.bus_shared=false;
      c.offset_rotation=LEVI_TOUCH_OFFSET_ROTATION; c.i2c_port=1;   /* I2C-Port 1, Arduino-Wire nutzt Port 0 */ c.i2c_addr=0x15; c.pin_sda=6; c.pin_scl=7; c.freq=400000; _touch.config(c); _panel.setTouch(&_touch); }
    setPanel(&_panel);
  }
};
