#pragma once
// Gemeinsamer Zustand des Diagnosegeraets (Firmware-Seite). Die Logik liegt in components/debug_core.
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include "dbg_cmd.h"

// Abgewaehlte Kconfig-Optionen (bool = n) fehlen in sdkconfig.h; hier als 0/1 verfuegbar.
#ifdef CONFIG_LEVILUMA_DEBUG_SCAN_ENABLE
#define LEVI_DBG_SCAN 1
#else
#define LEVI_DBG_SCAN 0
#endif
#ifdef CONFIG_LEVILUMA_DEBUG_SCAN_STAY_ON_HIT
#define LEVI_DBG_STAY_ON_HIT 1
#else
#define LEVI_DBG_STAY_ON_HIT 0
#endif
#ifdef CONFIG_LEVILUMA_DEBUG_ASCII
#define LEVI_DBG_ASCII 1
#else
#define LEVI_DBG_ASCII 0
#endif
#ifdef CONFIG_LEVILUMA_DEBUG_FULL_FRAME
#define LEVI_DBG_FULL_FRAME 1
#else
#define LEVI_DBG_FULL_FRAME 0
#endif
#ifdef CONFIG_LEVILUMA_DEBUG_SHOW_ENCRYPTED
#define LEVI_DBG_SHOW_ENC 1
#else
#define LEVI_DBG_SHOW_ENC 0
#endif

struct App {
  SemaphoreHandle_t lock; // schuetzt settings (Filter, Format, Kanalplan)
  dbg::Settings settings;
  dbg::Stats stats;       // Schreiber je Feld siehe dbg_state.h
  QueueHandle_t queue;    // dbg::Capture
};
extern App g_app;

uint32_t appMillis();

// sniffer.cpp
void snifferStart();                         // WLAN starten, Promiscuous-Empfang aktivieren (sendet nichts)
bool snifferSetChannel(uint8_t ch);          // Kanal wechseln (Plan wird geprueft und aktualisiert)
void snifferScan(bool on, uint32_t dwell_ms);

// report.cpp
void reportStart();

// console.cpp
void consoleStart();
