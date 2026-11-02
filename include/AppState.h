#pragma once
// Gemeinsamer Zustand fuer LVGL-UI, BLE und Web-API.
// Schreiben nur ueber die Setter aus state.cpp (unter stateLock), Lesen ueber snapshot() als Kopie.
#include <stdint.h>
#include <stdio.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "CalibCore.h"
#include "Palette.h"

// Logging ueber stdout (UART0-Konsole), also in demselben Monitor wie die NimBLE-Meldungen.
#define LEVI_LOG(fmt,...) printf("[%8lu] levi: " fmt "\n",(unsigned long)millis(),##__VA_ARGS__)

// sentRevision: letzte Revision, die vollstaendig an BLE gesendet wurde.
// Das ist eine Sendebestaetigung, keine Ausfuehrungsbestaetigung.
struct State { bool ble=false; bool music=false; bool raw=false; int8_t power=-1; uint8_t r=139,g=92,b=246,brightness=100; uint32_t revision=0; uint32_t colorRev=0; uint32_t sentRevision=0; };

extern SemaphoreHandle_t stateLock;
void stateInit();   // Mutexe anlegen und Kalibrierung laden (einmal in setup)

// Index in der Farbliste des Farbwaehlers (0..N-1 aus LeviPalette, MUSIC_IDX), -1 = freie Farbe.
// Besitz: nur der Loop-Task (LVGL-UI, Web-Handler setRGB/setMusic). Der BLE-Task liest 'selected' nicht,
// er arbeitet nur mit der Snapshot-Kopie von State. Deshalb ist kein Mutex noetig. Wenn BLE je darauf zugreift, unter stateLock verschieben.
extern int8_t selected;

// Farbliste des Farbwaehlers (LeviPalette, Config LEVI_PICKER_LIST), danach der Musikmodus.
static constexpr int MUSIC_IDX=LeviPalette::N;
static constexpr int LIST_COUNT=LeviPalette::N+1;
State snapshot();
void stateSetBle(bool on);
void stateMarkSent(uint32_t rev);

// Kalibrierung: vollstaendige Kopie des Modells (BLE-Task und Web-JSON).
LeviCal::Model calSnapshot(bool *custom=nullptr);

// Setter (aendern den Zustand unter stateLock, nur aus dem Loop-Task aufrufen).
void setPower(bool on);
void setBrightness(int b);
void setRaw(uint8_t r,uint8_t g,uint8_t b);
void setRGB(uint8_t r,uint8_t g,uint8_t b);
void setMusic(bool on);

// Kalibrierung speichern, loeschen und als JSON ausgeben (NVS).
bool calibSave(const uint8_t *pts);
bool calibReset();
String calibJson();
