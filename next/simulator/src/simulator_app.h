#pragma once
// Gemeinsamer Ablauf beider Simulator-Programme: Optionen, simuliertes Funkmedium, simulierte
// Gateways, Controller-Dienst, SDL-Fenster und die LVGL-Hauptschleife (wie der LVGL-Task der Firmware).
#include <stdint.h>
#include <functional>
#include <string>
#include <vector>
#include "gateway_registry.h"
#include "lvgl.h"
#include "sdl_backend.h"

class SimController;
class MockGateway;
class SimAir;

struct SimOptions {
  int zoom = 2;
  bool dim = true;        // Hintergrundbeleuchtung nach CONFIG_LEVILUMA_DIM_MS aus (wie Firmware)
  bool unpaired = false;  // ohne simulierten Vorzustand (kein Gateway gekoppelt)
  bool firstBoot = false; // RockBase: ohne gespeicherte Touch-Kalibrierung starten
  int gateways = 2;       // simulierte Gateways (1..3)
  int loss = 0;           // Paketverlust in %
  std::string script;     // Testskript (siehe tests/README im Simulator-README)
  int timeoutMs = 0;      // Skript: Abbruch nach dieser Zeit (0 = 120 s)
};

struct SimVariant {
  SimDeviceSpec spec;
  const char *program;
  // Werte aus der sdkconfig.h der Variante (Kconfig der Firmware)
  const char *ctrlName;
  uint32_t requestTimeoutMs, requestTries, beaconMs;
  bool hasFirstBoot;
  // Desktop-Port der Variante (Gegenstueck zu main/lvgl_port.cpp): Eingabegeraet anlegen ...
  void (*portInit)(lv_display_t *device, const SimOptions &o);
  // ... und je Schleifendurchlauf (Abschaltung der Beleuchtung)
  void (*portLoop)();
  void (*buildUi)(); // uiBuild() der Firmware
  std::function<bool(const GatewayRegistry &)> mayAutoPair;
};

int simRun(int argc, char **argv, const SimVariant &v);

// Fuer die app*-Adapter der Varianten
SimController &simController();
// Fuer Skripte
std::vector<MockGateway *> &simGateways();
SimAir &simAir();
const SimOptions &simOptions();
