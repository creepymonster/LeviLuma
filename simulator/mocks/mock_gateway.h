#pragma once
// Simuliertes Gateway (Ersatz fuer XIAO-Gateway, BLE-Band und Gateway-NVS im Simulator).
//
// Die Protokolllogik ist NICHT nachgebaut: Es laeuft die echte Gateway-Logik aus
// gateway/components/light_core (GatewayHandler, LightModel, CalibStore, PairingTable), also
// derselbe Code, der auf dem XIAO den autoritativen LightState haelt. Ersetzt sind nur:
//  - ESP-NOW -> SimAir (simuliertes Funkmedium)
//  - NVS     -> MemKvStore (fluechtig, endet mit dem Programm)
//  - BLE-Band -> simuliert: "verbunden" ist ein Schalter, gesendete Revisionen werden nach kurzer
//               Zeit als an das Band gesendet markiert (sentRevision), wie der BLE-Task.
// Der Ablauf des Link-Tasks (Antworten, Beacons, Zustandsmeldungen hoechstens alle 100 ms) folgt
// gateway/main/espnow_link.cpp.
//
// Bedienung aus dem Simulator (Steuerleiste, Testskripte) nur ueber post(): die Befehle laufen ueber
// eine Queue in den Gateway-Thread. Kein Zugriff auf LVGL oder auf die Controller-Oberflaeche.
#include <stdint.h>
#include <atomic>
#include <mutex>
#include <thread>
#include "calib_store.h"
#include "gateway_handler.h"
#include "kv_store.h"
#include "levi_key.h"
#include "light_model.h"
#include "pairing_table.h"
#include "sim_air.h"
#include "sim_queue.h"

struct MockGatewayConfig {
  uint8_t id[LVP_ID_LEN] = {0};
  char name[LVP_NAME_MAX + 1] = "Simuliert";
  bool hasKey = false;
  uint8_t key[LEVI_KEY_LEN] = {0}; // nur Simulationsschluessel, nie echte Schluessel
  uint32_t pairingWindowMs = 0;    // Kopplungsfenster beim Start (0 = geschlossen)
  uint32_t beaconMs = 2000;        // wie CONFIG_LEVILUMA_BEACON_MS
  uint32_t bleConnectDelayMs = 1200; // simulierte Verbindungszeit zum Band nach dem Start
  bool bleAtStart = true;
  // Plausibler Startzustand (simuliert): wird nach dem Verbinden des Bands gesetzt
  bool initialState = true;
  uint8_t initPower = 1, initBrightness = 80, initR = 255, initG = 107, initB = 107;
};

struct MockGatewaySnapshot {
  uint8_t id[LVP_ID_LEN];
  char name[LVP_NAME_MAX + 1];
  bool online, ble, pairingOpen, hasKey;
  char keyHex[LEVI_KEY_HEX_LEN + 1];
  int pairedCount;
  LightSnapshot light;
  uint32_t rxFrames, txFrames, rejected;
};

class MockGateway {
public:
  enum class Cmd : uint8_t { SetOnline, SetBle, SetMusic, OpenPairing, SetPower, SetBrightness };

  MockGateway(SimAir &air, const MockGatewayConfig &cfg);
  ~MockGateway();
  void start();
  void stop();

  // Threadsicher: Befehl an den Gateway-Thread (Event-Queue)
  void post(Cmd c, int value = 0);
  MockGatewaySnapshot snapshot();
  const MockGatewayConfig &config() const {
    return cfg_;
  }

private:
  struct Event {
    bool isFrame;
    SimFrame frame;
    Cmd cmd;
    int value;
  };
  void run();
  void handleFrame(const SimFrame &f, uint32_t now);
  void handleCmd(Cmd c, int value, uint32_t now);
  void ensurePeer(const uint8_t mac[LVP_ID_LEN]);
  void sendTo(const uint8_t mac[LVP_ID_LEN], const uint8_t *data, size_t len);
  void broadcast(const uint8_t *data, size_t len);
  void notifyPaired();
  void applyLocal(const lvp_set_state_t &c); // wie ein anderer Controller/Taster am Gateway

  SimAir &air_;
  MockGatewayConfig cfg_;
  SimQueue<Event> q_{32};
  std::thread th_;
  std::atomic<bool> running_{false};

  std::mutex lock_; // wie gw.lock: Modell, Kalibrierung, Kopplungen, Handler
  MemKvStore kv_;
  LightModel light_;
  CalibStore calib_;
  PairingTable pairing_;
  GatewayHandler *handler_ = nullptr;
  std::atomic<bool> online_{true}; // nur der Gateway-Thread schreibt, snapshot() liest
  bool bleWanted_ = true;           // nur Gateway-Thread
  std::atomic<uint32_t> rx_{0}, tx_{0}, rejected_{0};
};
