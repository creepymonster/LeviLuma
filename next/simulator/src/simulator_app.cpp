#include "simulator_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory>
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "control_panel.h"
#include "levi_key.h"
#include "mock_gateway.h"
#include "sim_air.h"
#include "sim_clock.h"
#include "sim_controller.h"
#include "sim_script.h"

namespace {

SimOptions gOpt;
SimAir gAir;
std::unique_ptr<SimController> gCtl;
std::vector<std::unique_ptr<MockGateway>> gGwOwned;
std::vector<MockGateway *> gGw;

// Simulierte Geraete-IDs (lokal verwaltete MAC-Adressen, Bit 0x02 im ersten Byte)
const uint8_t CTRL_ID[LVP_ID_LEN] = {0x02, 0x51, 0x4D, 0x00, 0x00, 0x01};

void usage(const char *prog, const SimVariant &v) {
  printf("LeviLuma Next Desktop-Simulator: %s (%dx%d%s)\n"
         "Aufruf: %s [Optionen]\n"
         "  --zoom N        Vergroesserung des Displays (1..4, Standard 2)\n"
         "  --no-dim        Hintergrundbeleuchtung nicht abschalten (Firmware: nach %s)\n"
         "  --unpaired      ohne simulierten Vorzustand starten (kein Gateway gekoppelt)\n"
         "  --gateways N    Anzahl simulierter Gateways (1..3, Standard 2)\n"
         "  --loss P        Paketverlust im simulierten Funk in %% (0..50)\n"
         "%s"
         "  --script DATEI  Testskript ausfuehren und danach beenden (Rueckgabe 0 = bestanden)\n"
         "  --timeout MS    Hoechstdauer eines Skripts (Standard 120000)\n"
         "  --help          diese Hilfe\n",
         v.spec.name, v.spec.width, v.spec.height, v.spec.round ? ", rund" : "", prog, "CONFIG_LEVILUMA_DIM_MS",
         v.hasFirstBoot ? "  --first-boot    ohne gespeicherte Touch-Kalibrierung starten (Kalibrierung beim ersten Start)\n" : "");
}

bool parse(int argc, char **argv, const SimVariant &v, SimOptions &o, int &exitCode) {
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&](const char *what) -> const char * {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s: Wert fehlt\n", what);
        return nullptr;
      }
      return argv[++i];
    };
    const char *val = nullptr;
    if (a == "--help" || a == "-h") {
      usage(argv[0], v);
      exitCode = 0;
      return false;
    } else if (a == "--zoom" && (val = next("--zoom"))) {
      o.zoom = atoi(val);
    } else if (a == "--no-dim") {
      o.dim = false;
    } else if (a == "--unpaired") {
      o.unpaired = true;
    } else if (a == "--first-boot" && v.hasFirstBoot) {
      o.firstBoot = true;
    } else if (a == "--gateways" && (val = next("--gateways"))) {
      o.gateways = atoi(val);
    } else if (a == "--loss" && (val = next("--loss"))) {
      o.loss = atoi(val);
    } else if (a == "--script" && (val = next("--script"))) {
      o.script = val;
    } else if (a == "--timeout" && (val = next("--timeout"))) {
      o.timeoutMs = atoi(val);
    } else {
      fprintf(stderr, "Unbekannte Option: %s (--help zeigt alle)\n", a.c_str());
      exitCode = 2;
      return false;
    }
  }
  o.gateways = o.gateways < 1 ? 1 : o.gateways > 3 ? 3 : o.gateways;
  return true;
}

// Simulierte Gateways: ausdruecklich als simuliert benannt
void makeGateways(const SimVariant &v) {
  static const char *names[3] = {"SIM Wohnzimmer", "SIM Kueche", "SIM Flur"};
  for (int i = 0; i < gOpt.gateways; i++) {
    MockGatewayConfig c;
    uint8_t id[LVP_ID_LEN] = {0x02, 0x51, 0x47, 0x00, 0x00, (uint8_t)(0x10 + i)};
    memcpy(c.id, id, LVP_ID_LEN);
    strncpy(c.name, names[i], LVP_NAME_MAX);
    c.beaconMs = v.beaconMs;
    c.bleConnectDelayMs = 800 + 400 * i;
    if (i == 0) {
      c.pairingWindowMs = 120000; // fuer den simulierten Vorzustand bzw. die erste Kopplung
    } else if (i == 1) {
      // Gateway mit individuellem Schluessel (nur Simulation; der Schluessel steht in der Steuerleiste)
      c.hasKey = true;
      levi_key_parse_hex("00112233445566778899AABBCCDDEEFF", c.key);
      c.initBrightness = 40;
      c.initR = 90;
      c.initG = 170;
      c.initB = 255;
    } else {
      c.pairingWindowMs = 0;
      c.initPower = 0;
    }
    gGwOwned.push_back(std::make_unique<MockGateway>(gAir, c));
    gGw.push_back(gGwOwned.back().get());
  }
}

} // namespace

SimController &simController() {
  return *gCtl;
}

std::vector<MockGateway *> &simGateways() {
  return gGw;
}

SimAir &simAir() {
  return gAir;
}

const SimOptions &simOptions() {
  return gOpt;
}

int simRun(int argc, char **argv, const SimVariant &v) {
  int exitCode = 0;
  if (!parse(argc, argv, v, gOpt, exitCode)) {
    return exitCode;
  }
  printf("LeviLuma Desktop-Simulator: %s %dx%d - SIMULATION, keine Hardware angeschlossen\n", v.spec.name, v.spec.width, v.spec.height);
  printf("LVGL %d.%d.%d\n", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
  gAir.setLossPercent(gOpt.loss);

  // Reihenfolge wie app_main: Dienst (appInit/appStart), dann LVGL
  makeGateways(v);
  SimController::Options co;
  memcpy(co.ownId, CTRL_ID, LVP_ID_LEN);
  strncpy(co.ownName, v.ctrlName, LVP_NAME_MAX);
  co.timeoutMs = v.requestTimeoutMs;
  co.tries = v.requestTries;
  co.beaconMs = v.beaconMs;
  co.autoPair = !gOpt.unpaired;
  memcpy(co.autoPairId, gGw[0]->config().id, LVP_ID_LEN);
  co.mayAutoPair = v.mayAutoPair;
  gCtl = std::make_unique<SimController>(gAir, co);
  for (MockGateway *g : gGw) {
    g->start();
  }
  gCtl->start();

  lv_init();
  lv_tick_set_cb(simMillis);
  if (!SimBackend::init(v.spec, gOpt.zoom, 340)) {
    return 1;
  }
  v.portInit(SimBackend::deviceDisplay(), gOpt);
  v.buildUi();
  lv_timer_handler();
  controlPanelBuild(SimBackend::panelDisplay(), v.spec.name, gGw, gAir);

  SimScript script;
  bool scripted = !gOpt.script.empty();
  if (scripted && !script.load(gOpt.script)) {
    fprintf(stderr, "%s\n", script.error().c_str());
    return 2;
  }
  uint32_t started = simMillis(), limit = gOpt.timeoutMs > 0 ? (uint32_t)gOpt.timeoutMs : 120000;

  bool running = true;
  while (running) {
    running = SimBackend::pump();
    if (scripted) {
      SimScript::Result r = script.step();
      if (r == SimScript::Result::Done) {
        printf("Skript bestanden: %s\n", gOpt.script.c_str());
        break;
      }
      if (r == SimScript::Result::Failed) {
        fprintf(stderr, "Skript fehlgeschlagen: %s\n  %s\n", gOpt.script.c_str(), script.error().c_str());
        exitCode = 1;
        break;
      }
      if (simMillis() - started > limit) {
        fprintf(stderr, "Skript abgebrochen: Zeit ueberschritten (%u ms)\n", (unsigned)limit);
        exitCode = 1;
        break;
      }
    }
    uint32_t wait = lv_timer_handler();
    v.portLoop();
    SimBackend::setTitleSuffix(SimBackend::backlight() == 0 ? " - Beleuchtung aus (Klick weckt)" : "");
    SimBackend::present();
    SDL_Delay(wait > 10 ? 10 : (wait ? wait : 1));
  }

  gCtl->stop();
  for (MockGateway *g : gGw) {
    g->stop();
  }
  SimBackend::shutdown();
  return exitCode;
}
