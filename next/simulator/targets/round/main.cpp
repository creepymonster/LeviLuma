// LeviLuma Next Desktop-Simulator: Waveshare ESP32-S3-Touch-LCD-1.28 (rund),
// Oberflaeche aus next/controller-round/main/ui.cpp.
#include "board.h"
#include "round_core.h"
#include "sdkconfig.h"
#include "simulator_app.h"
#include "ui.h"

void simRoundPortInit(lv_display_t *display, const SimOptions &o);
void simRoundPortLoop();

int main(int argc, char **argv) {
  SimVariant v;
  v.spec = {"Waveshare Round (ESP32-S3)", LCD_W, LCD_H, true}; // Aufloesung aus next/controller-round/main/board.h
  v.program = "leviluma-sim-round";
  v.ctrlName = CONFIG_LEVILUMA_CTRL_NAME;
  v.requestTimeoutMs = CONFIG_LEVILUMA_REQUEST_TIMEOUT_MS;
  v.requestTries = CONFIG_LEVILUMA_REQUEST_TRIES;
  v.beaconMs = CONFIG_LEVILUMA_BEACON_MS;
  v.hasFirstBoot = false;
  v.portInit = simRoundPortInit;
  v.portLoop = simRoundPortLoop;
  v.buildUi = uiBuild;
  v.mayAutoPair = [](const GatewayRegistry &r) { return Round::mayPair(r); }; // genau ein Gateway
  return simRun(argc, argv, v);
}
