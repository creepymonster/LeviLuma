#pragma once
#include <Arduino.h>
namespace Lotus {
inline void color(uint8_t r,uint8_t g,uint8_t b,uint8_t (&out)[9]) {
  const uint8_t c[9]={0x7E,0x07,0x05,0x03,r,g,b,0x0A,0xEF}; memcpy(out,c,9);
}
inline void power(bool on,uint8_t (&out)[9]) {
  const uint8_t a[9]={0x7E,0x07,0x04,0xFF,0,1,2,1,0xEF};
  const uint8_t b[9]={0x7E,0x07,0x04,0,0,0,2,1,0xEF}; memcpy(out,on?a:b,9);
}
// Helligkeitsbefehl ist noch NICHT am Zielgeraet bestaetigt.
inline void brightness(uint8_t pct,uint8_t (&out)[9]) {
  uint8_t c[9]={0x7E,0x04,0x01,(uint8_t)constrain(pct,0,100),0x01,0xFF,0x02,0x01,0xEF}; memcpy(out,c,9);
}
}
