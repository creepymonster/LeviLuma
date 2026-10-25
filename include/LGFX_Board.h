#pragma once
#include <LovyanGFX.hpp>
#include "Config.h"
class LGFX_Board : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel;
  lgfx::Bus_SPI _bus;
public:
  LGFX_Board() {
    { auto c=_bus.config(); c.spi_host=SPI2_HOST; c.spi_mode=0; c.freq_write=LEVI_DISPLAY_SPI_MHZ*1000000; c.freq_read=16000000; c.spi_3wire=true; c.use_lock=true; c.dma_channel=SPI_DMA_CH_AUTO; c.pin_sclk=10; c.pin_mosi=11; c.pin_miso=-1; c.pin_dc=8; _bus.config(c); _panel.setBus(&_bus); }
    { auto c=_panel.config(); c.pin_cs=9; c.pin_rst=14; c.pin_busy=-1; c.panel_width=240; c.panel_height=240; c.memory_width=240; c.memory_height=240; c.offset_x=0; c.offset_y=0; c.invert=LEVI_DISPLAY_INVERT; c.rgb_order=false; c.readable=false; c.bus_shared=false; _panel.config(c); }
    setPanel(&_panel);
  }
};
