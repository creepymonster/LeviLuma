#pragma once
// Gemeinsamer Zustand fuer LVGL-UI, BLE und Web-API.
// Schreiben nur ueber die Funktionen in main.cpp (unter stateLock), Lesen ueber snapshot() als Kopie.
#include <stdint.h>
#include <stdio.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "CalibCore.h"

// Logging ueber stdout (UART0-Konsole), also in demselben Monitor wie die NimBLE-Meldungen.
#define LEVI_LOG(fmt,...) printf("[%8lu] levi: " fmt "\n",(unsigned long)millis(),##__VA_ARGS__)

// sentRevision: letzte Revision, die vollstaendig an BLE gesendet wurde.
// Das ist eine Sendebestaetigung, keine Ausfuehrungsbestaetigung.
struct State { bool ble=false; bool music=false; bool raw=false; int8_t power=-1; uint8_t r=139,g=92,b=246,brightness=75; uint32_t revision=0; uint32_t colorRev=0; uint32_t sentRevision=0; };

extern SemaphoreHandle_t stateLock;
State snapshot();
void stateSetBle(bool on);
void stateMarkSent(uint32_t rev);

// Kalibrierung: vollstaendige Kopie des Modells (BLE-Task und Web-JSON).
LeviCal::Model calSnapshot(bool *custom=nullptr);
