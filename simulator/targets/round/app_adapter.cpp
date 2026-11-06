// Desktop-Adapter fuer die Dienstschicht des runden Controllers (controller-round/main/app.h).
// Gegenstueck zu controller-round/main/app.cpp: dieselben Funktionen und Regeln (genau ein Gateway
// ueber round_core), ohne ESP-IDF/FreeRTOS. Funk, Lock, Zeit und Speicher kommen aus SimController.
#include "app.h"
#include <string.h>
#include "round_core.h"
#include "sim_clock.h"
#include "sim_controller.h"
#include "simulator_app.h"

namespace {
void lock() {
  simController().lock();
}
void unlock() {
  simController().unlock();
}
} // namespace

uint32_t appMillis() {
  return simMillis();
}

void appInit() {
}

void appStart() {
}

uint32_t appVersion() {
  lock();
  uint32_t v = simController().link().version();
  unlock();
  return v;
}

int appGatewayCount() {
  return GatewayRegistry::MAX_ENTRIES;
}

bool appView(int idx, GatewayView &v) {
  if (idx < 0 || idx >= GatewayRegistry::MAX_ENTRIES) {
    return false;
  }
  lock();
  CtrlLink &link = simController().link();
  const GatewayEntry &g = simController().reg().at(idx);
  uint32_t now = appMillis();
  v.used = g.used;
  v.paired = g.paired;
  v.online = link.online(idx, now);
  v.stateValid = g.stateValid;
  v.pending = g.used && link.commandPending(idx);
  v.hasKey = g.hasKey;
  v.lastTimeout = g.lastTimeout;
  v.pairPhase = g.pairPhase;
  v.lastError = g.lastError;
  memcpy(v.id, g.id, LVP_ID_LEN);
  strncpy(v.name, g.displayName(), LVP_NAME_MAX);
  v.name[LVP_NAME_MAX] = 0;
  v.caps = g.caps;
  v.fwVersion = g.fwVersion;
  v.lastSeenAgoMs = g.lastSeenMs ? now - g.lastSeenMs : UINT32_MAX;
  v.state = g.state;
  unlock();
  return v.used;
}

int appActive() {
  lock();
  int i = Round::activeGateway(simController().reg());
  unlock();
  return i;
}

bool appMayPair() {
  lock();
  bool ok = Round::mayPair(simController().reg());
  unlock();
  return ok;
}

void appDiscover() {
  lock();
  simController().link().discover(appMillis());
  unlock();
}

lvp_err_t appPair(int idx, const uint8_t *key) {
  lock();
  lvp_err_t e = Round::mayPair(simController().reg()) ? simController().link().pair(idx, key, appMillis()) : LVP_ERR_FULL;
  unlock();
  return e;
}

bool appUnpair() {
  lock();
  int i = Round::activeGateway(simController().reg());
  bool ok = i >= 0 && simController().link().unpair(i, appMillis());
  unlock();
  return ok;
}

bool appResync() {
  lock();
  int i = Round::activeGateway(simController().reg());
  simController().link().discover(appMillis());
  bool ok = i >= 0 && simController().link().requestState(i, appMillis());
  unlock();
  return ok;
}

static lvp_err_t setState(const lvp_set_state_t &c) {
  lock();
  int i = Round::activeGateway(simController().reg());
  lvp_err_t e = i >= 0 ? simController().link().setState(i, c, appMillis()) : LVP_ERR_NOT_PAIRED;
  unlock();
  return e;
}

lvp_err_t appSetPower(bool on) {
  return setState({LVP_SET_POWER, (uint8_t)(on ? 1 : 0), 0, 0, 0, 0, 0});
}

lvp_err_t appSetBrightness(uint8_t pct) {
  return setState({LVP_SET_BRIGHTNESS, 0, pct, 0, 0, 0, 0});
}

lvp_err_t appSetColor(uint8_t r, uint8_t g, uint8_t b) {
  return setState({LVP_SET_COLOR, 0, 0, r, g, b, 0});
}

// Texte wie in main/app.cpp
const char *appErrorText(uint8_t err, bool timeout) {
  if (timeout && !err) {
    return "Keine Antwort";
  }
  switch (err) {
  case 0:
    return "";
  case LVP_ERR_PAIRING_CLOSED:
    return "Am Gateway BOOT druecken";
  case LVP_ERR_NOT_PAIRED:
    return "Gateway kennt uns nicht";
  case LVP_ERR_FULL:
    return "Kein Platz";
  case LVP_ERR_STORAGE:
    return "Speicherfehler";
  default:
    return lvp_err_name(err);
  }
}
