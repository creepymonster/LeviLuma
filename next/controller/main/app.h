#pragma once
// Controller-Dienst: Gateway-Register, ESP-NOW-Link und die Schnittstelle fuer Oberflaeche und Zigbee.
// Nebenlaeufigkeit:
//  - appLock() schuetzt Register und Link. Oberflaeche (LVGL-Task) und Zigbee-Task rufen die
//    Funktionen unten auf; sie nehmen den Lock selbst.
//  - Der Link-Task verarbeitet empfangene ESP-NOW-Rahmen und Zeitablaeufe.
//  - LVGL wird nur aus dem LVGL-Task angesprochen; der liest den Zustand ueber appSnapshot().
#include <stdint.h>
#include "ctrl_link.h"
#include "gateway_registry.h"
#include "kv_store.h"

struct GatewayView {
  bool used, paired, online, stateValid, calibValid, pending, calibPending, hasKey, lastTimeout;
  PairPhase pairPhase;
  uint8_t lastError;
  uint8_t id[LVP_ID_LEN];
  char name[LVP_NAME_MAX + 1];
  char announcedName[LVP_NAME_MAX + 1];
  char alias[LVP_NAME_MAX + 1];
  uint8_t caps;
  uint16_t fwVersion;
  uint32_t lastSeenAgoMs;
  lvp_state_t state;
  lvp_calib_t calib;
  uint8_t zbEndpoint;
};

void appInit();
void appStart(); // ESP-NOW starten, Link-Task
uint32_t appMillis();
uint32_t appVersion(); // steigt bei jeder sichtbaren Aenderung
int appGatewayCount(); // Groesse der Tabelle (GatewayRegistry::MAX_ENTRIES)
bool appView(int idx, GatewayView &out);
const uint8_t *appOwnId();
CtrlLink::Stats appStats();

// Aktionen (alle threadsicher)
void appDiscover();
lvp_err_t appPair(int idx, const uint8_t *key);
bool appUnpair(int idx);
bool appSetAlias(int idx, const char *alias);
lvp_err_t appSetPower(int idx, bool on);
lvp_err_t appSetBrightness(int idx, uint8_t pct);
lvp_err_t appSetColor(int idx, uint8_t r, uint8_t g, uint8_t b);
lvp_err_t appSetRaw(int idx, uint8_t r, uint8_t g, uint8_t b);
lvp_err_t appSetMusic(int idx, bool on);
lvp_err_t appSetState(int idx, const lvp_set_state_t &cmd);
bool appRequestState(int idx);
bool appRequestCalib(int idx);
bool appSendCalib(int idx, const uint8_t points[LVP_CAL_BYTES]);
bool appResetCalib(int idx);

// Fuer Zigbee: Zugriff auf Register unter Lock
void appLock();
void appUnlock();
GatewayRegistry &appRegistry();
void appSetListener(CtrlLink::Listener *l);

// Text zu Fehlern fuer die Oberflaeche
const char *appErrorText(uint8_t err, bool timeout);
