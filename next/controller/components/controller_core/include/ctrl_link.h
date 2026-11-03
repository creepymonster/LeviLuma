#pragma once
// Controller-Seite des ESP-NOW-Protokolls ohne Plattformabhaengigkeit:
// Erkennung, Kopplung (optional mit Schluessel), Steuerbefehle mit Wiederholung, Zustandsabgleich,
// Online-/Offline-Erkennung und Kalibrierung. Funk ueber die Schnittstelle Radio.
// Nicht threadsicher: der Aufrufer (Controller main) haelt den App-Mutex.
//
// Senderegel je Gateway (unabhaengig fuer jedes Gateway):
//  - Hoechstens EINE Anfrage ist unterwegs (alle Typen). Wiederholungen nutzen dieselbe Sequenznummer
//    und erzeugen keine neuen Eintraege. Nach endgueltigem Timeout wird nichts erneut eingereiht.
//  - Zusaetzlich wartet hoechstens EIN Steuerbefehl. Ein neuer Lichtbefehl ersetzt den wartenden
//    vollstaendig (kein Zusammenfuehren, keine FIFO-Schlange).
//  - Kalibrierung speichern/zuruecksetzen ist ein expliziter Vorgang: Er wird nie von einem Lichtbefehl
//    verdraengt (der Lichtbefehl wird mit LVP_ERR_BUSY abgewiesen). Er ersetzt nur einen wartenden Lichtbefehl.
//  - Zustands-/Kalibrierabfragen ueberholen keine offene Anfrage und stauen sich nicht: sie werden als
//    Bedarf (ein Merker je Art) vorgemerkt und gesendet, sobald nichts mehr offen ist und kein Befehl wartet.
//    Sonst koennte das Gateway die Wiederholung eines verlorenen Befehls als veraltet verwerfen.
//
// Grundsatz: Der Controller zeigt nur bestaetigte Zustaende an. Ein Steuerbefehl gilt erst als
// ausgefuehrt, wenn das Gateway mit STATE antwortet (autoritativer LightState).
#include <stddef.h>
#include <stdint.h>
#include "gateway_registry.h"
#include "leviluma_link.h"
#include "leviluma_proto.h"

class Radio {
public:
  virtual ~Radio() = default;
  virtual bool send(const uint8_t mac[LVP_ID_LEN], const uint8_t *data, size_t len) = 0;
  virtual bool broadcast(const uint8_t *data, size_t len) = 0;
  // Peer eintragen/aendern: lmk = nullptr -> unverschluesselt
  virtual bool setPeer(const uint8_t mac[LVP_ID_LEN], const uint8_t *lmk) = 0;
  virtual void delPeer(const uint8_t mac[LVP_ID_LEN]) = 0;
};

class CtrlLink {
public:
  struct Config {
    uint8_t ownId[LVP_ID_LEN] = {0};
    char ownName[LVP_NAME_MAX + 1] = "LeviLuma";
    uint32_t session = 1;
    uint32_t timeoutMs = 250;
    uint8_t tries = 4;
    uint32_t beaconMs = 2000;     // Beacon-Abstand der Gateways
    uint32_t discoverEveryMs = 15000;
  };

  // Rueckmeldungen an Oberflaeche und Zigbee
  class Listener {
  public:
    virtual ~Listener() = default;
    virtual void onGatewayState(int idx) {}  // bestaetigter Zustand geaendert (oder online/offline)
    virtual void onPairResult(int idx, bool ok) {}
  };

  CtrlLink(GatewayRegistry &reg, Radio &radio, const Config &cfg);
  void setListener(Listener *l) {
    listener_ = l;
  }

  // Start: gekoppelte Gateways als Peers eintragen, Erkennung und Zustandsabfrage
  void start(uint32_t nowMs);
  void onFrame(const uint8_t src[LVP_ID_LEN], bool broadcast, const uint8_t *data, size_t len, uint32_t nowMs);
  void tick(uint32_t nowMs);

  void discover(uint32_t nowMs);
  // Kopplung starten. key = nullptr: ohne Schluessel. Ergebnis ueber pairPhase/onPairResult.
  lvp_err_t pair(int idx, const uint8_t *key, uint32_t nowMs);
  // Entkoppeln: UNPAIR an das Gateway (best effort) und lokal sofort loeschen.
  bool unpair(int idx, uint32_t nowMs);
  // Lichtbefehl. Ist eine Anfrage unterwegs, wartet der Befehl und ersetzt einen bereits wartenden
  // Lichtbefehl vollstaendig (neuester gewinnt). LVP_ERR_BUSY, wenn eine wartende Kalibrierung
  // (speichern/zuruecksetzen) nicht verdraengt werden darf.
  lvp_err_t setState(int idx, const lvp_set_state_t &cmd, uint32_t nowMs);
  // Abfragen: true = gesendet oder als Bedarf vorgemerkt; false = nicht gekoppelt oder dieselbe Abfrage laeuft bereits.
  bool requestState(int idx, uint32_t nowMs);
  bool requestCalib(int idx, uint32_t nowMs);
  // Kalibrierung speichern/zuruecksetzen: false = nicht gekoppelt oder es wartet schon eine Kalibrierung (nicht verloren
  // geben, spaeter erneut versuchen). Ein wartender Lichtbefehl wird ersetzt.
  bool sendCalib(int idx, const uint8_t points[LVP_CAL_BYTES], uint32_t nowMs);
  bool resetCalib(int idx, uint32_t nowMs);

  bool online(int idx, uint32_t nowMs) const;
  // Lichtbefehl an dieses Gateway unterwegs oder wartend (Anzeige "wird gesendet")
  bool commandPending(int idx) const;
  // Kalibrierung laeuft: Abfrage/Speichern/Zuruecksetzen unterwegs, wartend oder vorgemerkt
  bool calibPending(int idx) const;
  // Steigt bei jeder fuer die Anzeige relevanten Aenderung
  uint32_t version() const {
    return version_;
  }
  uint32_t offlineTimeoutMs() const {
    return cfg_.beaconMs * 3 + 500;
  }

  struct Stats {
    uint32_t sent = 0, resent = 0, timeouts = 0, rx = 0, rxErrors = 0, nacks = 0;
  };
  const Stats &stats() const {
    return stats_;
  }

private:
  bool request(int idx, lvp_msg_t &m, uint32_t nowMs);
  void handleResponse(int idx, const lvp_msg_t &m, const lvl_pending_t *req, uint32_t nowMs);
  void applyState(int idx, const lvp_state_t &s, uint32_t gwSession);
  const lvl_pending_t *inFlight(int idx) const;
  // Naechstes Wartende senden, wenn nichts offen ist: Befehl, dann Zustandsabfrage, dann Kalibrierabfrage
  void pump(int idx, uint32_t nowMs);
  void onTimeout(const lvl_pending_t &p, uint32_t nowMs);
  void changed(int idx);

  GatewayRegistry &reg_;
  Radio &radio_;
  Config cfg_;
  Listener *listener_ = nullptr;
  lvl_txq_t txq_;
  uint16_t seq_ = 0;
  uint32_t version_ = 1;
  uint32_t lastDiscover_ = 0;
  bool started_ = false;
  bool onlineCache_[GatewayRegistry::MAX_ENTRIES] = {false};
  // Hoechstens ein wartender Steuerbefehl je Gateway
  enum class WaitKind : uint8_t { None, Light, SetCalib, ResetCalib };
  struct Waiting {
    WaitKind kind = WaitKind::None;
    lvp_set_state_t light = {};
    uint8_t points[LVP_CAL_BYTES] = {0};
  };
  Waiting wait_[GatewayRegistry::MAX_ENTRIES];
  bool needState_[GatewayRegistry::MAX_ENTRIES] = {false}; // zusammengefasster Abfragebedarf
  bool needCalib_[GatewayRegistry::MAX_ENTRIES] = {false};
  Stats stats_;
};
