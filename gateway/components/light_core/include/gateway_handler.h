#pragma once
// Protokolllogik des Gateways ohne Funk: eingehenden Rahmen pruefen, Duplikate erkennen,
// Kopplung pruefen, Befehl auf den autoritativen LightState anwenden und die Antwort erzeugen.
// Die Plattformschicht (main/espnow_link.cpp) sendet die Antwort und verwaltet die ESP-NOW-Peers.
// Nicht threadsicher: Aufrufer haelt den Gateway-Mutex.
#include <stddef.h>
#include <stdint.h>
#include "calib_store.h"
#include "leviluma_link.h"
#include "leviluma_proto.h"
#include "light_model.h"
#include "pairing_table.h"

class GatewayHandler {
public:
  struct Identity {
    uint8_t id[LVP_ID_LEN] = {0};
    char name[LVP_NAME_MAX + 1] = "LeviLuma";
    uint16_t fwVersion = 0x0100;
    bool keyConfigured = false;
    uint32_t session = 0;
  };

  struct Output {
    uint8_t reply[LVP_MAX_FRAME];
    uint8_t replyLen = 0;
    bool replyBroadcast = false; // ANNOUNCE geht immer per Broadcast (unverschluesselt)
    bool stateChanged = false;   // Zustand geaendert: andere gekoppelte Controller benachrichtigen
    bool calibChanged = false;
    bool ensurePeer = false;     // Absender als ESP-NOW-Peer eintragen (mit LMK, falls Schluessel)
    bool pairedChanged = false;
    bool removePeer = false;     // nach dem Senden der Antwort Peer entfernen (Entkoppeln)
    uint8_t dropReason = 0;      // lvp_err_t, wenn der Rahmen verworfen wurde (nur Log)
  };

  GatewayHandler(LightModel &light, CalibStore &cal, PairingTable &pairing, const Identity &id)
      : light_(light), cal_(cal), pairing_(pairing), id_(id) {
    lvl_dedup_init(&dedup_);
  }

  void handle(const uint8_t src[LVP_ID_LEN], bool toBroadcast, const uint8_t *data, size_t len, uint32_t nowMs, Output &out);

  // Beacon/Antwort auf DISCOVER
  size_t buildAnnounce(uint8_t *buf, size_t cap, uint32_t nowMs);
  // Unaufgeforderte Zustandsmeldung an gekoppelte Controller
  size_t buildStateNotify(uint8_t *buf, size_t cap);

  const Identity &identity() const {
    return id_;
  }
  void setName(const char *name);
  void setKeyConfigured(bool on) {
    id_.keyConfigured = on;
  }
  void forgetPeer(const uint8_t mac[LVP_ID_LEN]) {
    lvl_dedup_forget(&dedup_, mac);
  }

private:
  size_t reply(lvp_msg_t &m, const lvp_header_t &req, Output &out);
  size_t nack(const lvp_header_t &req, lvp_err_t e, Output &out);
  uint16_t nextSeq() {
    seq_ = lvp_seq_next(seq_);
    return seq_;
  }

  LightModel &light_;
  CalibStore &cal_;
  PairingTable &pairing_;
  Identity id_;
  lvl_dedup_t dedup_;
  uint16_t seq_ = 0;
};
