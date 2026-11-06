// LeviLuma Next Desktop-Simulator: RockBase NM-CYD-C5 (ESP32-C5), Oberflaeche aus controller/main.
#include "board.h"
#include "sdkconfig.h"
#include "simulator_app.h"
#include "ui.h"

#if CONFIG_LEVILUMA_ZIGBEE_ENABLED
#error "Der Simulator baut die RockBase-Oberflaeche ohne Zigbee (CONFIG_LEVILUMA_ZIGBEE_ENABLED=n)"
#endif

void simControllerPortInit(lv_display_t *display, const SimOptions &o);
void simControllerPortLoop();

int main(int argc, char **argv) {
  SimVariant v;
  v.spec = {"RockBase NM-CYD-C5", LCD_W, LCD_H, false}; // Aufloesung aus controller/main/board.h
  v.program = "leviluma-sim-controller";
  v.ctrlName = CONFIG_LEVILUMA_CTRL_NAME;
  v.requestTimeoutMs = CONFIG_LEVILUMA_REQUEST_TIMEOUT_MS;
  v.requestTries = CONFIG_LEVILUMA_REQUEST_TRIES;
  v.beaconMs = CONFIG_LEVILUMA_BEACON_MS;
  v.hasFirstBoot = true;
  v.portInit = simControllerPortInit;
  v.portLoop = simControllerPortLoop;
  v.buildUi = uiBuild;
  return simRun(argc, argv, v);
}
