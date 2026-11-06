#include "mock_gateway.h"
#include <stdio.h>
#include <string.h>
#include "sim_clock.h"

static const uint8_t BCAST[LVP_ID_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

MockGateway::MockGateway(SimAir &air, const MockGatewayConfig &cfg)
    : air_(air), cfg_(cfg), calib_(kv_, CalibStore::Defaults{}), pairing_(kv_) {
  calib_.load();
  pairing_.load();
  light_.setCalibRevision(calib_.revision());
  GatewayHandler::Identity id;
  memcpy(id.id, cfg_.id, LVP_ID_LEN);
  strncpy(id.name, cfg_.name, LVP_NAME_MAX);
  id.fwVersion = 0x0100;
  id.keyConfigured = cfg_.hasKey;
  id.session = 0x51A00000u | cfg_.id[5];
  handler_ = new GatewayHandler(light_, calib_, pairing_, id);
  bleWanted_ = cfg_.bleAtStart;
}

MockGateway::~MockGateway() {
  stop();
  delete handler_;
}

void MockGateway::start() {
  if (running_) {
    return;
  }
  air_.attach(cfg_.id, [this](const SimFrame &f) {
    Event e{};
    e.isFrame = true;
    e.frame = f;
    q_.push(e); // wie onRecv: nur kopieren, bei voller Queue verwerfen
  });
  air_.setPeer(cfg_.id, BCAST, nullptr);
  if (cfg_.pairingWindowMs) {
    pairing_.openWindow(simMillis(), cfg_.pairingWindowMs);
  }
  running_ = true;
  th_ = std::thread([this] { run(); });
}

void MockGateway::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  q_.wake();
  if (th_.joinable()) {
    th_.join();
  }
  air_.detach(cfg_.id);
}

void MockGateway::post(Cmd c, int value) {
  Event e{};
  e.isFrame = false;
  e.cmd = c;
  e.value = value;
  q_.push(e);
}

// Peer eintragen wie ensurePeer() in espnow_link.cpp: mit LMK, wenn das Gateway einen Schluessel hat
void MockGateway::ensurePeer(const uint8_t mac[LVP_ID_LEN]) {
  air_.setPeer(cfg_.id, mac, cfg_.hasKey ? cfg_.key : nullptr);
}

void MockGateway::sendTo(const uint8_t mac[LVP_ID_LEN], const uint8_t *data, size_t len) {
  if (!len || !online_) {
    return;
  }
  if (air_.send(cfg_.id, mac, data, len)) {
    ++tx_;
  }
}

void MockGateway::broadcast(const uint8_t *data, size_t len) {
  sendTo(BCAST, data, len);
}

void MockGateway::notifyPaired() {
  uint8_t buf[LVP_MAX_FRAME];
  uint8_t macs[PairingTable::MAX][LVP_ID_LEN];
  int n;
  size_t len;
  {
    std::lock_guard<std::mutex> g(lock_);
    len = handler_->buildStateNotify(buf, sizeof(buf));
    n = pairing_.count();
    for (int i = 0; i < n; i++) {
      memcpy(macs[i], pairing_.at(i).mac, LVP_ID_LEN);
    }
  }
  for (int i = 0; i < n; i++) {
    sendTo(macs[i], buf, len);
  }
}

void MockGateway::handleFrame(const SimFrame &f, uint32_t now) {
  if (!online_) {
    return; // Gateway "aus": nichts empfangen
  }
  ++rx_;
  GatewayHandler::Output out;
  {
    std::lock_guard<std::mutex> g(lock_);
    handler_->handle(f.src, f.broadcast, f.data, f.len, now, out);
  }
  if (out.ensurePeer) {
    ensurePeer(f.src);
  }
  if (out.replyLen) {
    sendTo(out.replyBroadcast ? BCAST : f.src, out.reply, out.replyLen);
  }
  if (out.removePeer) {
    std::lock_guard<std::mutex> g(lock_);
    handler_->forgetPeer(f.src);
  }
  if (out.removePeer) {
    air_.delPeer(cfg_.id, f.src);
    printf("[sim-gw %s] Controller entkoppelt\n", cfg_.name);
  } else if (out.pairedChanged) {
    printf("[sim-gw %s] Controller gekoppelt\n", cfg_.name);
  }
  if (out.dropReason) {
    ++rejected_;
  }
}

void MockGateway::applyLocal(const lvp_set_state_t &c) {
  std::lock_guard<std::mutex> g(lock_);
  light_.apply(c);
}

void MockGateway::handleCmd(Cmd c, int v, uint32_t now) {
  switch (c) {
  case Cmd::SetOnline:
    online_ = v != 0;
    printf("[sim-gw %s] %s\n", cfg_.name, online_ ? "online" : "offline (empfaengt und sendet nichts)");
    break;
  case Cmd::SetBle: {
    bleWanted_ = v != 0;
    std::lock_guard<std::mutex> g(lock_);
    light_.setBle(bleWanted_);
    printf("[sim-gw %s] Band %s\n", cfg_.name, bleWanted_ ? "verbunden" : "getrennt");
    break;
  }
  case Cmd::SetMusic:
    applyLocal({LVP_SET_MUSIC, 0, 0, 0, 0, 0, (uint8_t)(v ? 1 : 0)});
    break;
  case Cmd::SetPower:
    applyLocal({LVP_SET_POWER, (uint8_t)(v ? 1 : 0), 0, 0, 0, 0, 0});
    break;
  case Cmd::SetBrightness:
    applyLocal({LVP_SET_BRIGHTNESS, 0, (uint8_t)v, 0, 0, 0, 0});
    break;
  case Cmd::OpenPairing: {
    std::lock_guard<std::mutex> g(lock_);
    pairing_.openWindow(now, v > 0 ? (uint32_t)v : 60000); // wie BOOT-Taste am XIAO
    printf("[sim-gw %s] Kopplungsfenster offen\n", cfg_.name);
    break;
  }
  }
}

void MockGateway::run() {
  uint32_t start = simMillis(), lastBeacon = 0, lastNotify = 0, bleSentAt = 0;
  bool notifyPending = false, bleStarted = false, beaconNow = true;
  uint32_t lastRev = 0;
  bool lastBle = false;

  while (running_) {
    Event e;
    uint32_t now;
    if (q_.pop(e, 20)) {
      now = simMillis();
      if (e.isFrame) {
        handleFrame(e.frame, now);
      } else {
        handleCmd(e.cmd, e.value, now);
        beaconNow |= e.cmd == Cmd::OpenPairing || e.cmd == Cmd::SetOnline; // wie GW_EVT_PAIRING_OPENED
      }
    }
    now = simMillis();

    // simuliertes Band: Verbindung nach kurzer Zeit, dann der plausible Startzustand
    if (!bleStarted && now - start >= cfg_.bleConnectDelayMs) {
      bleStarted = true;
      std::lock_guard<std::mutex> g(lock_);
      light_.setBle(bleWanted_);
      if (cfg_.initialState) {
        light_.apply({(uint8_t)(LVP_SET_POWER | LVP_SET_BRIGHTNESS | LVP_SET_COLOR), cfg_.initPower, cfg_.initBrightness, cfg_.initR,
                      cfg_.initG, cfg_.initB, 0});
      }
    }
    // Zustandsaenderung erkennen (eigene Befehle, Band, Antworten): wie GW_EVT_STATE_CHANGED
    {
      std::lock_guard<std::mutex> g(lock_);
      const LightSnapshot &s = light_.snapshot();
      if (s.revision != lastRev || s.ble != lastBle) {
        lastRev = s.revision;
        lastBle = s.ble;
        notifyPending = true;
        bleSentAt = now;
      }
      // simulierter BLE-Task: angenommene Revision nach 40 ms als gesendet markieren
      if (s.ble && s.sentRevision < s.revision && now - bleSentAt >= 40) {
        light_.markSent(s.revision);
        notifyPending = true;
      }
    }
    if (online_ && notifyPending && now - lastNotify >= 100) {
      notifyPending = false;
      lastNotify = now;
      notifyPaired();
    }
    if (online_ && (beaconNow || now - lastBeacon >= cfg_.beaconMs)) {
      beaconNow = false;
      lastBeacon = now;
      uint8_t buf[LVP_MAX_FRAME];
      size_t len;
      {
        std::lock_guard<std::mutex> g(lock_);
        len = handler_->buildAnnounce(buf, sizeof(buf), now);
      }
      broadcast(buf, len);
    }
  }
}

MockGatewaySnapshot MockGateway::snapshot() {
  MockGatewaySnapshot s{};
  std::lock_guard<std::mutex> g(lock_);
  memcpy(s.id, cfg_.id, LVP_ID_LEN);
  strncpy(s.name, handler_->identity().name, LVP_NAME_MAX);
  s.online = online_;
  s.ble = light_.snapshot().ble;
  s.pairingOpen = pairing_.windowOpen(simMillis());
  s.hasKey = cfg_.hasKey;
  if (cfg_.hasKey) {
    levi_key_to_hex(cfg_.key, s.keyHex, sizeof(s.keyHex));
  }
  s.pairedCount = pairing_.count();
  s.light = light_.snapshot();
  s.rxFrames = rx_;
  s.txFrames = tx_;
  s.rejected = rejected_;
  return s;
}
