#include "sim_clock.h"
#include <chrono>

static const auto kStart = std::chrono::steady_clock::now();

uint32_t simMillis() {
  // +1: 0 bedeutet in GatewayRegistry "nie gesehen"
  return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - kStart).count() + 1;
}
