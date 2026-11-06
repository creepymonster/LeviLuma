#include "light_model.h"

lvp_err_t LightModel::apply(const lvp_set_state_t &c) {
  lvp_err_t e = lvp_validate_set_state(&c);

  if (e != LVP_OK) {
    return e;
  }
  if (c.mask & LVP_SET_POWER) {
    s_.power = c.power ? 1 : 0;
  }
  if (c.mask & LVP_SET_BRIGHTNESS) {
    s_.brightness = c.brightness; // 1..100 bereits geprueft (Legacy: constrain 1..100)
  }
  if (c.mask & LVP_SET_RAW) { // wie Legacy setRaw: ohne Kalibrierung, Musik bleibt
    s_.raw = true;
    s_.r = c.r;
    s_.g = c.g;
    s_.b = c.b;
    ++s_.colorRev;
  }
  if (c.mask & LVP_SET_COLOR) { // wie Legacy setRGB: kalibriert, beendet den Musikmodus
    s_.raw = false;
    s_.music = false;
    s_.r = c.r;
    s_.g = c.g;
    s_.b = c.b;
    ++s_.colorRev;
  }
  if (c.mask & LVP_SET_MUSIC) {
    s_.music = c.music != 0;
  }
  ++s_.revision;
  return LVP_OK;
}

void LightModel::setBle(bool connected) {
  s_.ble = connected;
}

void LightModel::markSent(uint32_t rev) {
  if (s_.sentRevision < rev) {
    s_.sentRevision = rev;
  }
}

void LightModel::setCalibRevision(uint16_t rev) {
  s_.calibRevision = rev;
}

lvp_state_t LightModel::toProto() const {
  lvp_state_t p{};
  p.revision = s_.revision;
  p.sent_revision = s_.sentRevision;
  p.power = s_.power;
  p.brightness = s_.brightness;
  p.r = s_.r;
  p.g = s_.g;
  p.b = s_.b;
  p.flags = (uint8_t)((s_.music ? LVP_STATE_MUSIC : 0) | (s_.raw ? LVP_STATE_RAW : 0) | (s_.ble ? LVP_STATE_BLE : 0));
  p.calib_revision = s_.calibRevision;
  return p;
}
