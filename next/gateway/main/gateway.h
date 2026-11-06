#pragma once
// Gemeinsame Objekte der Gateway-Firmware.
// Nebenlaeufigkeit:
//  - gwLock schuetzt LightModel, CalibStore, PairingTable, GatewayHandler und BandMemory.
//  - Der BLE-Task liest nur Kopien (gwSnapshot, gwCalibCopy) und meldet ueber gwSetBle/gwMarkSent.
//  - Der Link-Task (ESP-NOW) ist der einzige, der Rahmen verarbeitet und sendet.
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "CalibCore.h"
#include "band_memory.h"
#include "calib_store.h"
#include "gateway_handler.h"
#include "kv_store.h"
#include "levi_key.h"
#include "light_model.h"
#include "pairing_table.h"

#define GW_LOG_TAG "levi-gw"

// Ereignisse an den Link-Task
#define GW_EVT_STATE_CHANGED BIT0 // Zustand oder BLE-Status geaendert: gekoppelte Controller benachrichtigen
#define GW_EVT_PAIRING_OPENED BIT1
#define GW_EVT_KEY_CHANGED BIT2 // Schluessel geaendert: Peers neu eintragen

struct GatewayContext {
  SemaphoreHandle_t lock;
  EventGroupHandle_t events;
  NvsKvStore *kv;
  LightModel *light;
  CalibStore *calib;
  PairingTable *pairing;
  BandMemory *band; // gemerktes Band (BLE-Adresse)
  GatewayHandler *handler;
  uint8_t key[LEVI_KEY_LEN];
  bool hasKey;
  bool bleOff; // Diagnose: BLE per Konsole abgeschaltet ("ble off", NVS), Band wird nicht verbunden
};

// Funkstatistik seit dem Start (Diagnose, Konsole "status" und periodisches Log)
struct LinkStats {
  uint32_t rx;        // vom ESP-NOW-Empfangs-Callback gelieferte Rahmen (alle Absender)
  uint32_t rxDropped; // davon verworfen, weil die Empfangs-Queue voll war
  uint32_t txOk;      // Unicast-Rahmen mit MAC-Bestaetigung
  uint32_t txFail;    // Unicast-Rahmen ohne MAC-Bestaetigung
};
LinkStats linkStats();

extern GatewayContext gw;

uint32_t gwMillis();
LightSnapshot gwSnapshot();
LeviCal::Model gwCalibCopy();
void gwSetBle(bool connected);
void gwMarkSent(uint32_t rev);
void gwOpenPairingWindow(uint32_t seconds);

// Module
void linkStart();    // ESP-NOW (WLAN im STA-Modus ohne Verbindung)
void bleStart();     // NimBLE-Central zum Band
void consoleStart(); // serielle Konsole (Schluessel, Name, Status)
void statusIoStart(); // Kopplungstaste und Status-LED
