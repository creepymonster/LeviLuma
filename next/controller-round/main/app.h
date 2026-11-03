#pragma once
// Dienst des runden Controllers: ESP-NOW (gemeinsames Protokoll ueber CtrlLink aus controller_core),
// Gateway-Register (NVS) und threadsichere Aktionen fuer die Oberflaeche. Genau ein Gateway.
// Nebenlaeufigkeit wie beim RockBase-Controller: appLock schuetzt Register und Link, der
// ESP-NOW-Callback kopiert nur in eine Queue, LVGL liest den Zustand im eigenen Task ueber appView().
#include <stdint.h>
#include "ctrl_link.h"
#include "gateway_registry.h"

struct GatewayView {
  bool used, paired, online, stateValid, pending, hasKey, lastTimeout;
  PairPhase pairPhase;
  uint8_t lastError;
  uint8_t id[LVP_ID_LEN];
  char name[LVP_NAME_MAX + 1];
  uint8_t caps;
  uint16_t fwVersion;
  uint32_t lastSeenAgoMs;
  lvp_state_t state;
};

void appInit();
void appStart();
uint32_t appMillis();
uint32_t appVersion();
int appGatewayCount();
bool appView(int idx, GatewayView &out);
int appActive();     // Index des gekoppelten Gateways oder -1
bool appMayPair();   // noch kein Gateway gekoppelt

void appDiscover();
lvp_err_t appPair(int idx, const uint8_t *key); // nur wenn appMayPair()
bool appUnpair();                                // entkoppelt das aktive Gateway
bool appResync();                                // Zustand neu abfragen und Erkennung senden
lvp_err_t appSetPower(bool on);
lvp_err_t appSetBrightness(uint8_t pct);
lvp_err_t appSetColor(uint8_t r, uint8_t g, uint8_t b);

const char *appErrorText(uint8_t err, bool timeout);
