#include "ctrl_link.h"
#include <string.h>

CtrlLink::CtrlLink(GatewayRegistry &reg, Radio &radio, const Config &cfg) : reg_(reg), radio_(radio), cfg_(cfg) {
  lvl_txq_init(&txq_, cfg.timeoutMs, cfg.tries);
}

void CtrlLink::changed(int idx) {
  version_++;
  if (listener_ && idx >= 0) {
    listener_->onGatewayState(idx);
  }
}

bool CtrlLink::online(int idx, uint32_t nowMs) const {
  return reg_.online(idx, nowMs, offlineTimeoutMs());
}

const lvl_pending_t *CtrlLink::inFlight(int idx) const {
  return lvl_txq_find_peer(&txq_, reg_.at(idx).id);
}

bool CtrlLink::commandPending(int idx) const {
  const lvl_pending_t *p = inFlight(idx);
  return (p && p->type == LVP_MSG_SET_STATE) || wait_[idx].kind == WaitKind::Light;
}

bool CtrlLink::calibPending(int idx) const {
  const lvl_pending_t *p = inFlight(idx);
  bool open = p && (p->type == LVP_MSG_GET_CALIB || p->type == LVP_MSG_SET_CALIB || p->type == LVP_MSG_RESET_CALIB);
  return open || wait_[idx].kind == WaitKind::SetCalib || wait_[idx].kind == WaitKind::ResetCalib || needCalib_[idx];
}

void CtrlLink::start(uint32_t nowMs) {
  for (int i = 0; i < reg_.size(); i++) {
    GatewayEntry &g = reg_.at(i);

    if (g.used && g.paired) {
      radio_.setPeer(g.id, g.hasKey ? g.key : nullptr);
      g.syncPending = true;
    }
  }
  started_ = true;
  discover(nowMs);
}

void CtrlLink::discover(uint32_t nowMs) {
  lvp_msg_t m;
  lvp_init(&m, LVP_MSG_DISCOVER);
  seq_ = lvp_seq_next(seq_);
  m.hdr.seq = seq_;
  m.hdr.session = cfg_.session;
  memcpy(m.u.discover.controller_id, cfg_.ownId, LVP_ID_LEN);
  memcpy(m.u.discover.name, cfg_.ownName, sizeof(m.u.discover.name));
  uint8_t buf[LVP_MAX_FRAME];
  size_t n = lvp_encode(&m, buf, sizeof(buf), nullptr);

  if (n) {
    radio_.broadcast(buf, n);
    stats_.sent++;
  }
  lastDiscover_ = nowMs ? nowMs : 1;
}

bool CtrlLink::request(int idx, lvp_msg_t &m, uint32_t nowMs) {
  GatewayEntry &g = reg_.at(idx);
  seq_ = lvp_seq_next(seq_);
  m.hdr.seq = seq_;
  m.hdr.session = cfg_.session;
  uint8_t buf[LVP_MAX_FRAME];
  size_t n = lvp_encode(&m, buf, sizeof(buf), nullptr);

  if (!n) {
    return false;
  }
  if (lvl_txq_add(&txq_, g.id, m.hdr.seq, m.hdr.type, buf, (uint8_t)n, nowMs, (uint32_t)idx) < 0) {
    return false; // Warteschlange voll
  }
  if (m.hdr.type == LVP_MSG_PAIR_REQ) {
    discover(nowMs); // Gateway traegt uns dabei als Peer ein (mit LMK, falls Schluessel)
  }
  radio_.send(g.id, buf, n);
  stats_.sent++;
  return true;
}

lvp_err_t CtrlLink::pair(int idx, const uint8_t *key, uint32_t nowMs) {
  GatewayEntry &g = reg_.at(idx);

  if (!g.used || g.paired) {
    return LVP_ERR_BAD_REQUEST;
  }
  if (reg_.pairedCount() >= GatewayRegistry::MAX_PAIRED) {
    return LVP_ERR_FULL;
  }
  g.hasKey = key != nullptr;
  if (key) {
    memcpy(g.key, key, LEVI_KEY_LEN);
  } else {
    memset(g.key, 0, LEVI_KEY_LEN);
  }
  radio_.setPeer(g.id, key);
  lvp_msg_t m;
  lvp_init(&m, LVP_MSG_PAIR_REQ);
  memcpy(m.u.pair_req.controller_id, cfg_.ownId, LVP_ID_LEN);
  memcpy(m.u.pair_req.name, cfg_.ownName, sizeof(m.u.pair_req.name));

  if (!request(idx, m, nowMs)) {
    return LVP_ERR_BUSY;
  }
  g.pairPhase = PairPhase::Pending;
  g.lastError = 0;
  g.lastTimeout = false;
  changed(idx);
  return LVP_OK;
}

// Fehlgeschlagene Kopplung: vorlaeufigen Schluessel und Peer verwerfen
static void pairFailedCleanup(GatewayEntry &g, Radio &radio) {
  if (!g.paired) {
    g.hasKey = false;
    memset(g.key, 0, sizeof(g.key));
    radio.delPeer(g.id);
  }
}

bool CtrlLink::unpair(int idx, uint32_t nowMs) {
  GatewayEntry &g = reg_.at(idx);

  if (!g.used || !g.paired) {
    return false;
  }
  // UNPAIR einmal senden (best effort); danach ist der Peer entfernt und Wiederholungen waeren unmoeglich.
  lvp_msg_t m;
  lvp_init(&m, LVP_MSG_UNPAIR);
  memcpy(m.u.unpair.controller_id, cfg_.ownId, LVP_ID_LEN);
  seq_ = lvp_seq_next(seq_);
  m.hdr.seq = seq_;
  m.hdr.session = cfg_.session;
  uint8_t buf[LVP_MAX_FRAME];
  size_t n = lvp_encode(&m, buf, sizeof(buf), nullptr);
  if (n) {
    radio_.send(g.id, buf, n);
    stats_.sent++;
  }
  lvl_txq_cancel_peer(&txq_, g.id);
  wait_[idx] = Waiting();
  needState_[idx] = needCalib_[idx] = false;
  radio_.delPeer(g.id);
  bool ok = reg_.unpair(idx);
  changed(idx);
  (void)nowMs;
  return ok;
}

lvp_err_t CtrlLink::setState(int idx, const lvp_set_state_t &cmd, uint32_t nowMs) {
  GatewayEntry &g = reg_.at(idx);

  if (!g.used || !g.paired) {
    return LVP_ERR_NOT_PAIRED;
  }
  lvp_err_t e = lvp_validate_set_state(&cmd);

  if (e != LVP_OK) {
    return e;
  }
  if (wait_[idx].kind == WaitKind::SetCalib || wait_[idx].kind == WaitKind::ResetCalib) {
    return LVP_ERR_BUSY; // wartende Kalibrierung nicht stillschweigend verdraengen
  }
  if (inFlight(idx)) {
    wait_[idx].kind = WaitKind::Light; // ersetzt einen wartenden Lichtbefehl vollstaendig
    wait_[idx].light = cmd;
    version_++;
    return LVP_OK;
  }
  lvp_msg_t m;
  lvp_init(&m, LVP_MSG_SET_STATE);
  m.u.set_state = cmd;

  if (!request(idx, m, nowMs)) {
    return LVP_ERR_BUSY;
  }
  g.lastTimeout = false;
  version_++;
  return LVP_OK;
}

void CtrlLink::pump(int idx, uint32_t nowMs) {
  GatewayEntry &g = reg_.at(idx);

  if (!g.used || !g.paired || inFlight(idx)) {
    return;
  }
  Waiting &w = wait_[idx];
  lvp_msg_t m;

  if (w.kind != WaitKind::None) {
    switch (w.kind) {
    case WaitKind::Light:
      lvp_init(&m, LVP_MSG_SET_STATE);
      m.u.set_state = w.light;
      break;
    case WaitKind::SetCalib:
      lvp_init(&m, LVP_MSG_SET_CALIB);
      memcpy(m.u.set_calib.points, w.points, LVP_CAL_BYTES);
      break;
    default:
      lvp_init(&m, LVP_MSG_RESET_CALIB);
      break;
    }
    if (request(idx, m, nowMs)) { // bei voller Warteschlange bleibt der Befehl stehen (naechster Tick)
      w = Waiting(); // lastTimeout bleibt bis zur naechsten Antwort stehen (Timeout des Vorgaengers nicht verschleiern)
      version_++;
    }
    return;
  }
  if (needState_[idx]) {
    lvp_init(&m, LVP_MSG_GET_STATE);
    needState_[idx] = !request(idx, m, nowMs);
    return;
  }
  if (needCalib_[idx]) {
    lvp_init(&m, LVP_MSG_GET_CALIB);
    needCalib_[idx] = !request(idx, m, nowMs);
  }
}

bool CtrlLink::requestState(int idx, uint32_t nowMs) {
  GatewayEntry &g = reg_.at(idx);
  const lvl_pending_t *p = g.used && g.paired ? inFlight(idx) : nullptr;

  if (!g.used || !g.paired || (p && p->type == LVP_MSG_GET_STATE)) {
    return false;
  }
  needState_[idx] = true; // Bedarf vormerken; gesendet wird nur, wenn nichts offen ist und kein Befehl wartet
  pump(idx, nowMs);
  return true;
}

bool CtrlLink::requestCalib(int idx, uint32_t nowMs) {
  GatewayEntry &g = reg_.at(idx);
  const lvl_pending_t *p = g.used && g.paired ? inFlight(idx) : nullptr;

  if (!g.used || !g.paired || (p && p->type == LVP_MSG_GET_CALIB)) {
    return false;
  }
  needCalib_[idx] = true;
  pump(idx, nowMs);
  return true;
}

bool CtrlLink::resetCalib(int idx, uint32_t nowMs) {
  GatewayEntry &g = reg_.at(idx);

  if (!g.used || !g.paired || wait_[idx].kind == WaitKind::SetCalib || wait_[idx].kind == WaitKind::ResetCalib) {
    return false;
  }
  wait_[idx] = Waiting();
  wait_[idx].kind = WaitKind::ResetCalib;
  g.lastError = 0;
  g.lastTimeout = false;
  pump(idx, nowMs);
  version_++;
  return true;
}

bool CtrlLink::sendCalib(int idx, const uint8_t points[LVP_CAL_BYTES], uint32_t nowMs) {
  GatewayEntry &g = reg_.at(idx);

  if (!g.used || !g.paired || wait_[idx].kind == WaitKind::SetCalib || wait_[idx].kind == WaitKind::ResetCalib) {
    return false;
  }
  wait_[idx] = Waiting();
  wait_[idx].kind = WaitKind::SetCalib;
  memcpy(wait_[idx].points, points, LVP_CAL_BYTES);
  g.lastError = 0;
  g.lastTimeout = false;
  pump(idx, nowMs);
  version_++;
  return true;
}

void CtrlLink::applyState(int idx, const lvp_state_t &s, uint32_t gwSession) {
  GatewayEntry &g = reg_.at(idx);
  bool sessionChanged = !g.sessionKnown || g.gwSession != gwSession;

  // Veraltete Meldungen (gleiche Sitzung, kleinere Revision) verwerfen
  if (g.stateValid && !sessionChanged) {
    if (s.revision < g.state.revision) {
      return;
    }
    if (s.revision == g.state.revision && s.sent_revision < g.state.sent_revision) {
      return;
    }
  }
  g.gwSession = gwSession;
  g.sessionKnown = true;
  g.state = s;
  g.stateValid = true;
  g.announcedRevision = s.revision;
  g.syncPending = false;
  needState_[idx] = false; // bestaetigter Zustand liegt vor
  changed(idx);
}

void CtrlLink::handleResponse(int idx, const lvp_msg_t &m, const lvl_pending_t *req, uint32_t nowMs) {
  GatewayEntry &g = reg_.at(idx);
  uint8_t reqType = req ? req->type : 0;

  switch (m.hdr.type) {
  case LVP_MSG_PAIR_RSP:
    if (reqType != LVP_MSG_PAIR_REQ) {
      break;
    }
    if (m.u.pair_rsp.result == LVP_OK) {
      lvp_err_t e = reg_.markPaired(idx);

      if (e == LVP_OK) {
        g.pairPhase = PairPhase::None;
        memcpy(g.announcedName, m.u.pair_rsp.name, sizeof(g.announcedName));
        g.syncPending = true;
        needState_[idx] = needCalib_[idx] = true; // nacheinander gesendet (pump)
      } else {
        g.pairPhase = PairPhase::Failed;
        g.lastError = (uint8_t)e;
        pairFailedCleanup(g, radio_);
      }
      changed(idx);
      if (listener_) {
        listener_->onPairResult(idx, e == LVP_OK);
      }
    }
    break;
  case LVP_MSG_NACK:
    stats_.nacks++;
    g.lastError = m.u.nack.error;

    if (reqType == LVP_MSG_PAIR_REQ) {
      g.pairPhase = PairPhase::Failed;
      pairFailedCleanup(g, radio_);
      if (listener_) {
        listener_->onPairResult(idx, false);
      }
    } else if (reqType == LVP_MSG_SET_STATE) {
      needState_[idx] = true; // Befehl abgelehnt: Anzeige auf den bestaetigten Zustand zuruecksetzen
    } else if (reqType == LVP_MSG_SET_CALIB || reqType == LVP_MSG_RESET_CALIB) {
      needCalib_[idx] = true; // abgelehnt: gespeicherte Kalibrierung neu lesen
    }
    changed(idx);
    break;
  case LVP_MSG_STATE:
    if (g.paired) {
      g.lastError = 0;
      g.lastTimeout = false;
      applyState(idx, m.u.state, m.hdr.session);
      if (!g.calibValid || g.calib.calib_revision != m.u.state.calib_revision) {
        needCalib_[idx] = true;
      }
    }
    break;
  case LVP_MSG_CALIB:
    if (g.paired) {
      g.calib = m.u.calib;
      g.calibValid = true;
      g.lastError = 0;
      g.lastTimeout = false;
      needCalib_[idx] = false;
      changed(idx);
    }
    break;
  case LVP_MSG_ACK:
  default:
    break;
  }
}

void CtrlLink::onFrame(const uint8_t src[LVP_ID_LEN], bool broadcast, const uint8_t *data, size_t len, uint32_t nowMs) {
  lvp_msg_t m;

  if (lvp_decode(data, len, &m) != LVP_OK) {
    stats_.rxErrors++;
    return;
  }
  stats_.rx++;

  if (m.hdr.type == LVP_MSG_DISCOVER) {
    return; // anderer Controller
  }
  if (m.hdr.type == LVP_MSG_ANNOUNCE) {
    // Die Absenderadresse ist die Gateway-ID (ESP-NOW-Adresse); fremde IDs im Inhalt ignorieren
    if (!lvp_id_equal(m.u.announce.gateway_id, src)) {
      stats_.rxErrors++;
      return;
    }
    int idx = reg_.addDiscovered(src, nowMs);
    if (idx < 0) {
      return;
    }
    GatewayEntry &g = reg_.at(idx);
    bool visible = strcmp(g.announcedName, m.u.announce.name) != 0 || g.caps != m.u.announce.caps || !onlineCache_[idx];
    memcpy(g.announcedName, m.u.announce.name, sizeof(g.announcedName));
    g.caps = m.u.announce.caps;
    g.fwVersion = m.u.announce.fw_version;
    g.lastSeenMs = nowMs ? nowMs : 1;
    g.announcedRevision = m.u.announce.state_revision;

    if (g.paired) {
      bool bleDiffers = g.stateValid && ((g.caps & LVP_GW_BLE_CONNECTED) != 0) != ((g.state.flags & LVP_STATE_BLE) != 0);
      if (!g.stateValid || !g.sessionKnown || g.gwSession != m.hdr.session || g.state.revision != m.u.announce.state_revision || bleDiffers) {
        g.syncPending = true;
      }
    }
    if (!onlineCache_[idx]) {
      onlineCache_[idx] = true;
    }
    if (visible) {
      changed(idx);
    }
    return;
  }

  int idx = reg_.find(src);

  if (idx < 0) {
    return; // unbekannter Absender
  }
  GatewayEntry &g = reg_.at(idx);
  g.lastSeenMs = nowMs ? nowMs : 1;

  if (m.hdr.flags & LVP_FLAG_RESPONSE) {
    lvl_pending_t req;
    bool matched = lvl_txq_complete(&txq_, src, m.hdr.ref_seq, &req);

    if (!matched && m.hdr.type != LVP_MSG_STATE) {
      return; // verspaetete Antwort auf eine bereits abgeschlossene Anfrage
    }
    handleResponse(idx, m, matched ? &req : nullptr, nowMs);
    pump(idx, nowMs);
    return;
  }
  if ((m.hdr.flags & LVP_FLAG_NOTIFY) && m.hdr.type == LVP_MSG_STATE && g.paired) {
    applyState(idx, m.u.state, m.hdr.session);
    if (!g.calibValid || g.calib.calib_revision != m.u.state.calib_revision) {
      needCalib_[idx] = true;
    }
    pump(idx, nowMs);
  }
}

void CtrlLink::onTimeout(const lvl_pending_t &p, uint32_t nowMs) {
  int idx = (int)p.tag;

  if (idx < 0 || idx >= reg_.size() || !lvp_id_equal(reg_.at(idx).id, p.peer)) {
    idx = reg_.find(p.peer);
  }
  stats_.timeouts++;

  if (idx < 0) {
    return;
  }
  GatewayEntry &g = reg_.at(idx);
  g.lastTimeout = true;

  switch (p.type) {
  case LVP_MSG_PAIR_REQ:
    g.pairPhase = PairPhase::Failed;
    pairFailedCleanup(g, radio_);
    if (listener_) {
      listener_->onPairResult(idx, false);
    }
    break;
  case LVP_MSG_SET_STATE:
    g.syncPending = true; // unklar, ob ausgefuehrt: Zustand beim naechsten Kontakt abgleichen
    break;
  case LVP_MSG_SET_CALIB:
  case LVP_MSG_RESET_CALIB:
    g.syncPending = g.paired;
    needCalib_[idx] = g.paired; // unklar, ob gespeichert: gespeicherte Kalibrierung abfragen
    break;
  default:
    g.syncPending = g.paired;
    break;
  }
  changed(idx);
  pump(idx, nowMs); // wartenden Befehl (ausschliesslich den letzten) senden; der alte wird nicht erneut eingereiht
}

void CtrlLink::tick(uint32_t nowMs) {
  int slot;
  lvl_pending_t p;
  lvl_action_t a;

  while ((a = lvl_txq_poll(&txq_, nowMs, &slot, &p)) != LVL_NONE) {
    if (a == LVL_RESEND) {
      lvl_pending_t &q = txq_.slot[slot];
      if (q.type == LVP_MSG_PAIR_REQ) {
        discover(nowMs);
      }
      radio_.send(q.peer, q.frame, q.len);
      stats_.resent++;
    } else {
      onTimeout(p, nowMs);
    }
  }

  for (int i = 0; i < reg_.size(); i++) {
    GatewayEntry &g = reg_.at(i);

    if (!g.used) {
      onlineCache_[i] = false;
      continue;
    }
    bool on = online(i, nowMs);

    if (on != onlineCache_[i]) {
      onlineCache_[i] = on;
      if (on && g.paired) {
        g.syncPending = true; // wieder erreichbar: Zustand abgleichen
      }
      changed(i);
    }
    if (g.paired && on && g.syncPending) {
      requestState(i, nowMs); // Bedarf vormerken; ueberholt keine offene Anfrage
    }
    pump(i, nowMs);
  }
  if (started_ && (uint32_t)(nowMs - lastDiscover_) >= cfg_.discoverEveryMs) {
    discover(nowMs);
  }
}
