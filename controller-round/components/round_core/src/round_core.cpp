#include "round_core.h"

namespace Round {

int activeGateway(const GatewayRegistry &reg) {
  for (int i = 0; i < reg.size(); i++) {
    if (reg.at(i).used && reg.at(i).paired) {
      return i;
    }
  }
  return -1;
}

bool mayPair(const GatewayRegistry &reg) {
  return reg.pairedCount() == 0;
}

bool Throttle::update(uint32_t nowMs) {
  dirty_ = true;
  if (!sentOnce_ || (uint32_t)(nowMs - last_) >= interval_) {
    last_ = nowMs;
    sentOnce_ = true;
    dirty_ = false;
    return true;
  }
  return false;
}

bool Throttle::release(uint32_t nowMs) {
  bool send = dirty_;
  dirty_ = false;
  sentOnce_ = false;
  if (send) {
    last_ = nowMs;
  }
  return send;
}

} // namespace Round
