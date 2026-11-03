#pragma once
// Controller-Dienst im Simulator: dieselbe Logik wie die Firmware (GatewayRegistry und CtrlLink aus
// next/controller/components/controller_core), ersetzt sind nur die Plattformteile aus main/app.cpp:
//   ESP-NOW-Radio       -> SimRadio (ueber SimAir)
//   FreeRTOS-Mutex/Task -> std::recursive_mutex / std::thread (Link-Thread wie linkTask)
//   NVS                 -> MemKvStore (fluechtig)
//   esp_timer           -> simMillis()
// Die app*-Funktionen der beiden Controller (targets/*/app_adapter.cpp) greifen hierueber zu.
#include <stdint.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include "ctrl_link.h"
#include "gateway_registry.h"
#include "kv_store.h"
#include "sim_air.h"
#include "sim_queue.h"

class SimRadio : public Radio {
public:
  SimRadio(SimAir &air, const uint8_t self[LVP_ID_LEN]);
  bool send(const uint8_t mac[LVP_ID_LEN], const uint8_t *data, size_t len) override;
  bool broadcast(const uint8_t *data, size_t len) override;
  bool setPeer(const uint8_t mac[LVP_ID_LEN], const uint8_t *lmk) override;
  void delPeer(const uint8_t mac[LVP_ID_LEN]) override;

private:
  SimAir &air_;
  uint8_t self_[LVP_ID_LEN];
};

class SimController {
public:
  struct Options {
    uint8_t ownId[LVP_ID_LEN] = {0};
    char ownName[LVP_NAME_MAX + 1] = "LeviLuma";
    uint32_t timeoutMs = 250, tries = 4, beaconMs = 2000;
    // Simulierter Vorzustand: dieses Gateway ueber das normale Protokoll koppeln, sobald es sich meldet
    bool autoPair = false;
    uint8_t autoPairId[LVP_ID_LEN] = {0};
    // Zusatzregel fuer das automatische Koppeln (Round: nur ein Gateway)
    std::function<bool(const GatewayRegistry &)> mayAutoPair;
  };

  SimController(SimAir &air, const Options &o);
  ~SimController();
  void start(); // link->start, Link-Thread
  void stop();

  void lock() {
    m_.lock();
  }
  void unlock() {
    m_.unlock();
  }
  GatewayRegistry &reg() {
    return reg_;
  }
  CtrlLink &link() {
    return *link_;
  }
  const uint8_t *ownId() const {
    return opt_.ownId;
  }

private:
  void run();
  void autoPairStep(uint32_t now);

  SimAir &air_;
  Options opt_;
  std::recursive_mutex m_;
  MemKvStore kv_;
  GatewayRegistry reg_{kv_};
  SimRadio radio_;
  CtrlLink *link_ = nullptr;
  SimQueue<SimFrame> rx_{12};
  std::thread th_;
  std::atomic<bool> running_{false};
  bool autoPairDone_ = false;
};
