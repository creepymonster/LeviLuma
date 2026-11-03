#pragma once
#include <stdint.h>

void lvglPortStart(void (*buildUi)());
uint32_t lvglIdleMs(); // ms seit der letzten Beruehrung
