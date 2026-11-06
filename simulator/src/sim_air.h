#pragma once
// Simuliertes ESP-NOW-Funkmedium (Ersatz fuer WLAN-Treiber und ESP-NOW-Stack im Simulator).
//
// Nachgebildet ist nur, was die LeviLuma-Logik sieht:
//  - Broadcast an alle anderen Teilnehmer, immer unverschluesselt (wie ESP-NOW).
//  - Unicast nur an eingetragene Peers (sonst schlaegt send() fehl wie ESP_ERR_ESPNOW_NOT_FOUND).
//  - Verschluesselung (LMK) als Vergleich: Ein Unicast kommt nur an, wenn Sender und Empfaenger fuer
//    einander denselben Schluessel (oder beide keinen) eingetragen haben. Sonst wird er verworfen, wie
//    ein Rahmen, den ESP-NOW nicht entschluesseln kann. Es wird nichts verschluesselt, keine eigene
//    Kryptografie.
//  - Einstellbarer zufaelliger Verlust je Rahmen.
// Kanal, Funkzeiten, MAC-Bestaetigung und Reichweite werden nicht nachgebildet.
//
// Threadsicher. Die Empfangsfunktion eines Teilnehmers darf nur in eine Queue kopieren (wie der
// ESP-NOW-Empfangscallback), weil sie im Thread des Senders laeuft.
#include <stddef.h>
#include <stdint.h>
#include <array>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <vector>
#include "leviluma_proto.h"

struct SimFrame {
  uint8_t src[LVP_ID_LEN];
  bool broadcast;
  uint8_t len;
  uint8_t data[LVP_MAX_FRAME];
};

class SimAir {
public:
  using Mac = std::array<uint8_t, LVP_ID_LEN>;
  using Sink = std::function<void(const SimFrame &)>;

  struct Stats {
    uint32_t delivered = 0, lost = 0, keyMismatch = 0, noPeer = 0;
  };

  void attach(const uint8_t mac[LVP_ID_LEN], Sink sink);
  void detach(const uint8_t mac[LVP_ID_LEN]);
  // lmk = nullptr: unverschluesselt
  void setPeer(const uint8_t self[LVP_ID_LEN], const uint8_t peer[LVP_ID_LEN], const uint8_t *lmk);
  void delPeer(const uint8_t self[LVP_ID_LEN], const uint8_t peer[LVP_ID_LEN]);
  bool hasPeer(const uint8_t self[LVP_ID_LEN], const uint8_t peer[LVP_ID_LEN]);
  // false wie esp_now_send bei unbekanntem Peer; ein verlorener Rahmen gilt als gesendet (keine Rueckmeldung)
  bool send(const uint8_t src[LVP_ID_LEN], const uint8_t dst[LVP_ID_LEN], const uint8_t *data, size_t len);
  bool broadcast(const uint8_t src[LVP_ID_LEN], const uint8_t *data, size_t len);

  void setLossPercent(int pct);
  int lossPercent();
  Stats stats();

private:
  struct Peer {
    bool encrypted = false;
    std::array<uint8_t, 16> lmk{};
  };
  struct Node {
    Sink sink;
    std::map<Mac, Peer> peers;
  };
  static Mac mac(const uint8_t *m);
  bool lose(); // unter m_

  std::mutex m_;
  std::map<Mac, Node> nodes_;
  int lossPct_ = 0;
  std::mt19937 rng_{12345};
  Stats stats_;
};
