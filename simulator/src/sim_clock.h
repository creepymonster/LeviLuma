#pragma once
// Desktop-Zeitquelle (Ersatz fuer esp_timer_get_time): Millisekunden seit Programmstart, monoton.
#include <stdint.h>

uint32_t simMillis();
