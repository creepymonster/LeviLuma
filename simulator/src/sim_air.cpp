#include "sim_air.h"
#include <string.h>

SimAir::Mac SimAir::mac(const uint8_t *m) {
  Mac a;
  memcpy(a.data(), m, LVP_ID_LEN);
  return a;
}

void SimAir::attach(const uint8_t m[LVP_ID_LEN], Sink sink) {
  std::lock_guard<std::mutex> g(m_);
  nodes_[mac(m)].sink = std::move(sink);
}

void SimAir::detach(const uint8_t m[LVP_ID_LEN]) {
  std::lock_guard<std::mutex> g(m_);
  nodes_.erase(mac(m));
}

void SimAir::setPeer(const uint8_t self[LVP_ID_LEN], const uint8_t peer[LVP_ID_LEN], const uint8_t *lmk) {
  std::lock_guard<std::mutex> g(m_);
  Peer &p = nodes_[mac(self)].peers[mac(peer)];
  p.encrypted = lmk != nullptr;
  p.lmk.fill(0);
  if (lmk) {
    memcpy(p.lmk.data(), lmk, p.lmk.size());
  }
}

void SimAir::delPeer(const uint8_t self[LVP_ID_LEN], const uint8_t peer[LVP_ID_LEN]) {
  std::lock_guard<std::mutex> g(m_);
  auto it = nodes_.find(mac(self));
  if (it != nodes_.end()) {
    it->second.peers.erase(mac(peer));
  }
}

bool SimAir::hasPeer(const uint8_t self[LVP_ID_LEN], const uint8_t peer[LVP_ID_LEN]) {
  std::lock_guard<std::mutex> g(m_);
  auto it = nodes_.find(mac(self));
  return it != nodes_.end() && it->second.peers.count(mac(peer));
}

bool SimAir::lose() {
  if (lossPct_ <= 0) {
    return false;
  }
  return (int)(rng_() % 100) < lossPct_;
}

bool SimAir::send(const uint8_t src[LVP_ID_LEN], const uint8_t dst[LVP_ID_LEN], const uint8_t *data, size_t len) {
  if (len == 0 || len > LVP_MAX_FRAME) {
    return false;
  }
  static const uint8_t BC[LVP_ID_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  if (memcmp(dst, BC, LVP_ID_LEN) == 0) {
    return broadcast(src, data, len);
  }
  Sink sink;
  SimFrame f{};
  {
    std::lock_guard<std::mutex> g(m_);
    auto s = nodes_.find(mac(src));
    if (s == nodes_.end()) {
      return false;
    }
    auto sp = s->second.peers.find(mac(dst));
    if (sp == s->second.peers.end()) {
      ++stats_.noPeer;
      return false; // wie ESP_ERR_ESPNOW_NOT_FOUND
    }
    auto d = nodes_.find(mac(dst));
    if (d == nodes_.end()) {
      return true; // niemand empfaengt (ESP-NOW meldet das nur ueber den Sende-Callback)
    }
    // Schluesselvergleich: Rahmen des Senders muss beim Empfaenger mit demselben LMK eingetragen sein
    auto rp = d->second.peers.find(mac(src));
    bool rxEncrypted = rp != d->second.peers.end() && rp->second.encrypted;
    bool match = sp->second.encrypted == rxEncrypted && (!rxEncrypted || sp->second.lmk == rp->second.lmk);
    if (!match) {
      ++stats_.keyMismatch;
      return true;
    }
    if (lose()) {
      ++stats_.lost;
      return true;
    }
    ++stats_.delivered;
    sink = d->second.sink;
    memcpy(f.src, src, LVP_ID_LEN);
    f.broadcast = false;
    f.len = (uint8_t)len;
    memcpy(f.data, data, len);
  }
  if (sink) {
    sink(f);
  }
  return true;
}

bool SimAir::broadcast(const uint8_t src[LVP_ID_LEN], const uint8_t *data, size_t len) {
  if (len == 0 || len > LVP_MAX_FRAME) {
    return false;
  }
  std::vector<Sink> sinks;
  SimFrame f{};
  memcpy(f.src, src, LVP_ID_LEN);
  f.broadcast = true;
  f.len = (uint8_t)len;
  memcpy(f.data, data, len);
  {
    std::lock_guard<std::mutex> g(m_);
    Mac s = mac(src);
    for (auto &n : nodes_) {
      if (n.first == s || !n.second.sink) {
        continue;
      }
      if (lose()) {
        ++stats_.lost;
        continue;
      }
      ++stats_.delivered;
      sinks.push_back(n.second.sink);
    }
  }
  for (auto &k : sinks) {
    k(f);
  }
  return true;
}

void SimAir::setLossPercent(int pct) {
  std::lock_guard<std::mutex> g(m_);
  lossPct_ = pct < 0 ? 0 : pct > 100 ? 100 : pct;
}

int SimAir::lossPercent() {
  std::lock_guard<std::mutex> g(m_);
  return lossPct_;
}

SimAir::Stats SimAir::stats() {
  std::lock_guard<std::mutex> g(m_);
  return stats_;
}
