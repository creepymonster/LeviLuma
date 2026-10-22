#pragma once
#include <Arduino.h>
namespace Lotus {
inline void color(uint8_t r,uint8_t g,uint8_t b,uint8_t (&out)[9]) {
  // 0x0A am Ende: am Band getestet (Rot kommt an)
  const uint8_t c[9]={0x7E,0x07,0x05,0x03,r,g,b,0x0A,0xEF};
  memcpy(out,c,9);
}
inline void power(bool on,uint8_t (&out)[9]) {
  const uint8_t state=on?0x01:0x00;
  const uint8_t c[9]={0x7E,0x04,0x04,state,0x00,state,0xFF,0x00,0xEF}; memcpy(out,c,9);
}
// Musikmodus: 7E 04 07 01|00 FF FF FF 00 EF (an/aus), vom Benutzer aus der Original-App uebernommen.
inline void music(bool on,uint8_t (&out)[9]) {
  const uint8_t a[9]={0x7E,0x04,0x07,0x01,0xFF,0xFF,0xFF,0x00,0xEF};
  const uint8_t b[9]={0x7E,0x04,0x07,0x00,0xFF,0xFF,0xFF,0x00,0xEF}; memcpy(out,on?a:b,9);
}
// Helligkeit nativ am Controller: 7E 04 01 BB MM FF FF 00 EF (BB = 0..100, MM = 0xFF), Referenz aus der Python-Vorlage.
inline void brightness(uint8_t pct,uint8_t (&out)[9]) {
  uint8_t c[9]={0x7E,0x04,0x01,(uint8_t)constrain(pct,0,100),0xFF,0xFF,0xFF,0x00,0xEF}; memcpy(out,c,9);
}
}
