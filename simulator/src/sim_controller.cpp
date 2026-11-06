#include "sim_controller.h"
#include <stdio.h>
#include <string.h>
#include "sim_clock.h"

static const uint8_t BCAST[LVP_ID_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

SimRadio::SimRadio(SimAir &air, const uint8_t self[LVP_ID_LEN]) : air_(air) {
  memcpy(self_, self, LVP_ID_LEN);
}

bool SimRadio::send(const uint8_t mac[LVP_ID_LEN], const uint8_t *data, size_t len) {
  return air_.send(self_, mac, data, len);
}

bool SimRadio::broadcast(const uint8_t *data, size_t len) {
  return air_.broadcast(self_, data, len);
}

bool SimRadio::setPeer(const uint8_t mac[LVP_ID_LEN], const uint8_t *lmk) {
  air_.setPeer(self_, mac, lmk);
  return true;
}

void SimRadio::delPeer(const uint8_t mac[LVP_ID_LEN]) {
  air_.delPeer(self_, mac);
}

SimController::SimController(SimAir &air, const Options &o) : air_(air), opt_(o), radio_(air, o.ownId) {
  reg_.load(); // MemKvStore: beim Start leer wie ein frisch geloeschter NVS
  CtrlLink::Config c;
  memcpy(c.ownId, opt_.ownId, LVP_ID_LEN);
  strncpy(c.ownName, opt_.ownName, LVP_NAME_MAX);
  c.session = 0xC0DE0001u;
  c.timeoutMs = opt_.timeoutMs;
  c.tries = (uint8_t)opt_.tries;
  c.beaconMs = opt_.beaconMs;
  link_ = new CtrlLink(reg_, radio_, c);
}

SimController::~SimController() {
  stop();
  delete link_;
}

void SimController::start() {
  air_.attach(opt_.ownId, [this](const SimFrame &f) {
    rx_.push(f); // wie onRecv: nur kopieren, bei voller Queue verwerfen
  });
  air_.setPeer(opt_.ownId, BCAST, nullptr);
  lock();
  link_->start(simMillis());
  unlock();
  running_ = true;
  th_ = std::thread([this] { run(); });
}

void SimController::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  rx_.wake();
  if (th_.joinable()) {
    th_.join();
  }
  air_.detach(opt_.ownId);
}

// Simulierter Vorzustand "bereits gekoppelt": nutzt den normalen Kopplungsablauf (PAIR/PAIR_RSP)
void SimController::autoPairStep(uint32_t now) {
  if (!opt_.autoPair || autoPairDone_) {
    return;
  }
  int idx = reg_.find(opt_.autoPairId);
  if (idx < 0) {
    return;
  }
  autoPairDone_ = true;
  if (reg_.at(idx).paired || (opt_.mayAutoPair && !opt_.mayAutoPair(reg_))) {
    return;
  }
  lvp_err_t e = link_->pair(idx, nullptr, now);
  printf("[sim] Simulierter Vorzustand: koppele mit \"%s\" (%s)\n", reg_.at(idx).displayName(), e == LVP_OK ? "gestartet" : lvp_err_name(e));
}

// wie linkTask in main/app.cpp
void SimController::run() {
  while (running_) {
    SimFrame f;
    bool got = rx_.pop(f, 20);
    lock();
    uint32_t now = simMillis();
    if (got) {
      link_->onFrame(f.src, f.broadcast, f.data, f.len, now);
    }
    link_->tick(now);
    autoPairStep(now);
    unlock();
  }
}
