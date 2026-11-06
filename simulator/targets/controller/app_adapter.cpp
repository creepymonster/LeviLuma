// Desktop-Adapter fuer die Dienstschicht des RockBase-Controllers (controller/main/app.h).
// Gegenstueck zu controller/main/app.cpp: dieselben Funktionen, derselbe Ablauf (Lock, CtrlLink,
// GatewayRegistry), aber ohne ESP-IDF/FreeRTOS. Funk, Lock, Zeit und Speicher kommen aus SimController.
// Wird app.h erweitert, meldet der Linker fehlende Funktionen hier.
#include "app.h"
#include <string.h>
#include "sim_clock.h"
#include "sim_controller.h"
#include "simulator_app.h"

uint32_t appMillis() {
  return simMillis();
}

void appLock() {
  simController().lock();
}

void appUnlock() {
  simController().unlock();
}

GatewayRegistry &appRegistry() {
  return simController().reg();
}

const uint8_t *appOwnId() {
  return simController().ownId();
}

void appSetListener(CtrlLink::Listener *l) {
  appLock();
  simController().link().setListener(l);
  appUnlock();
}

void appInit() {
}

void appStart() {
}

uint32_t appVersion() {
  appLock();
  uint32_t v = simController().link().version();
  appUnlock();
  return v;
}

int appGatewayCount() {
  return GatewayRegistry::MAX_ENTRIES;
}

bool appView(int idx, GatewayView &v) {
  if (idx < 0 || idx >= GatewayRegistry::MAX_ENTRIES) {
    return false;
  }
  appLock();
  CtrlLink &link = simController().link();
  const GatewayEntry &g = simController().reg().at(idx);
  uint32_t now = appMillis();
  v.used = g.used;
  v.paired = g.paired;
  v.online = link.online(idx, now);
  v.stateValid = g.stateValid;
  v.calibValid = g.calibValid;
  v.pending = g.used && link.commandPending(idx);
  v.calibPending = g.used && link.calibPending(idx);
  v.hasKey = g.hasKey;
  v.lastTimeout = g.lastTimeout;
  v.pairPhase = g.pairPhase;
  v.lastError = g.lastError;
  memcpy(v.id, g.id, LVP_ID_LEN);
  strncpy(v.name, g.displayName(), LVP_NAME_MAX);
  v.name[LVP_NAME_MAX] = 0;
  memcpy(v.announcedName, g.announcedName, sizeof(v.announcedName));
  memcpy(v.alias, g.alias, sizeof(v.alias));
  v.caps = g.caps;
  v.fwVersion = g.fwVersion;
  v.lastSeenAgoMs = g.lastSeenMs ? now - g.lastSeenMs : UINT32_MAX;
  v.state = g.state;
  v.calib = g.calib;
  v.zbEndpoint = g.zbEndpoint;
  appUnlock();
  return v.used;
}

CtrlLink::Stats appStats() {
  appLock();
  CtrlLink::Stats s = simController().link().stats();
  appUnlock();
  return s;
}

void appDiscover() {
  appLock();
  simController().link().discover(appMillis());
  appUnlock();
}

lvp_err_t appPair(int idx, const uint8_t *key) {
  appLock();
  lvp_err_t e = simController().link().pair(idx, key, appMillis());
  appUnlock();
  return e;
}

bool appUnpair(int idx) {
  appLock();
  bool ok = simController().link().unpair(idx, appMillis());
  appUnlock();
  return ok;
}

bool appSetAlias(int idx, const char *alias) {
  appLock();
  bool ok = simController().reg().setAlias(idx, alias);
  appUnlock();
  return ok;
}

lvp_err_t appSetState(int idx, const lvp_set_state_t &cmd) {
  appLock();
  lvp_err_t e = simController().link().setState(idx, cmd, appMillis());
  appUnlock();
  return e;
}

lvp_err_t appSetPower(int idx, bool on) {
  lvp_set_state_t c = {LVP_SET_POWER, (uint8_t)(on ? 1 : 0), 0, 0, 0, 0, 0};
  return appSetState(idx, c);
}

lvp_err_t appSetBrightness(int idx, uint8_t pct) {
  lvp_set_state_t c = {LVP_SET_BRIGHTNESS, 0, pct, 0, 0, 0, 0};
  return appSetState(idx, c);
}

lvp_err_t appSetColor(int idx, uint8_t r, uint8_t g, uint8_t b) {
  lvp_set_state_t c = {LVP_SET_COLOR, 0, 0, r, g, b, 0};
  return appSetState(idx, c);
}

lvp_err_t appSetRaw(int idx, uint8_t r, uint8_t g, uint8_t b) {
  lvp_set_state_t c = {LVP_SET_RAW, 0, 0, r, g, b, 0};
  return appSetState(idx, c);
}

lvp_err_t appSetMusic(int idx, bool on) {
  lvp_set_state_t c = {LVP_SET_MUSIC, 0, 0, 0, 0, 0, (uint8_t)(on ? 1 : 0)};
  return appSetState(idx, c);
}

bool appRequestState(int idx) {
  appLock();
  bool ok = simController().link().requestState(idx, appMillis());
  appUnlock();
  return ok;
}

bool appRequestCalib(int idx) {
  appLock();
  bool ok = simController().link().requestCalib(idx, appMillis());
  appUnlock();
  return ok;
}

bool appSendCalib(int idx, const uint8_t points[LVP_CAL_BYTES]) {
  appLock();
  bool ok = simController().link().sendCalib(idx, points, appMillis());
  appUnlock();
  return ok;
}

bool appResetCalib(int idx) {
  appLock();
  bool ok = simController().link().resetCalib(idx, appMillis());
  appUnlock();
  return ok;
}

// Texte wie in main/app.cpp
const char *appErrorText(uint8_t err, bool timeout) {
  if (timeout && !err) {
    return "Keine Antwort vom Gateway";
  }
  switch (err) {
  case 0:
    return "";
  case LVP_ERR_PAIRING_CLOSED:
    return "Kopplung am Gateway nicht freigegeben (BOOT-Taste druecken)";
  case LVP_ERR_NOT_PAIRED:
    return "Gateway kennt diesen Controller nicht (neu koppeln)";
  case LVP_ERR_FULL:
    return "Keine freien Plaetze";
  case LVP_ERR_STORAGE:
    return "Speicherfehler";
  case LVP_ERR_OUT_OF_RANGE:
    return "Wert ausserhalb des Bereichs";
  case LVP_ERR_VERSION:
    return "Protokollversion passt nicht";
  default:
    return lvp_err_name(err);
  }
}
