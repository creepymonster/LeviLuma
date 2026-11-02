#pragma once
#include <Arduino.h>
namespace Lotus {
inline void color(uint8_t r,uint8_t g,uint8_t b,uint8_t (&out)[9]) {
  // 0x10 am Ende: wie in der Original-App (dekompiliert)
  const uint8_t c[9]={0x7E,0x07,0x05,0x03,r,g,b,0x10,0xEF};
  memcpy(out,c,9);
}
inline void power(bool on,uint8_t (&out)[9]) {
  const uint8_t state=on?0x01:0x00;
  // An: 7E 04 04 F0 00 01 FF 00 EF (wie die App), Aus: 7E 04 04 00 00 00 FF 00 EF
  const uint8_t c[9]={0x7E,0x04,0x04,(uint8_t)(on?0xF0:0x00),0x00,state,0xFF,0x00,0xEF}; memcpy(out,c,9);
}
// Musikmodus: 7E 04 07 01|00 FF FF FF 00 EF (an/aus), vom Benutzer aus der Original-App uebernommen.
inline void music(bool on,uint8_t (&out)[9]) {
  const uint8_t a[9]={0x7E,0x04,0x07,0x01,0xFF,0xFF,0xFF,0x00,0xEF};
  const uint8_t b[9]={0x7E,0x04,0x07,0x00,0xFF,0xFF,0xFF,0x00,0xEF}; memcpy(out,on?a:b,9);
}
// Helligkeit nativ am Controller: 7E 04 01 BB MM FF FF 00 EF (BB = 0..100, MM = 0xFF), Referenz aus der Python-Vorlage.
// Mikrofon-EQ-Modus: 7E 05 03 (mode|0x80) 04 FF FF 00 EF, nach 'Mikrofon an' senden.
inline void micEq(uint8_t mode,uint8_t (&out)[9]) {
  const uint8_t c[9]={0x7E,0x05,0x03,(uint8_t)((mode&0x7F)|0x80),0x04,0xFF,0xFF,0x00,0xEF}; memcpy(out,c,9);
}
inline void brightness(uint8_t value,uint8_t (&out)[9]) {
  const uint8_t c[9]={0x7E,0x04,0x01,value,0xFF,0xFF,0xFF,0x00,0xEF}; memcpy(out,c,9);
}
}
