#include "gateway_handler.h"
#include <string.h>

void GatewayHandler::setName(const char *name) {
  strncpy(id_.name, name ? name : "", LVP_NAME_MAX);
  id_.name[LVP_NAME_MAX] = 0;
}

size_t GatewayHandler::reply(lvp_msg_t &m, const lvp_header_t &req, Output &out) {
  m.hdr.flags = LVP_FLAG_RESPONSE;
  m.hdr.ref_seq = req.seq;
  m.hdr.seq = nextSeq();
  m.hdr.session = id_.session;
  size_t n = lvp_encode(&m, out.reply, sizeof(out.reply), nullptr);
  out.replyLen = (uint8_t)n;
  return n;
}

size_t GatewayHandler::nack(const lvp_header_t &req, lvp_err_t e, Output &out) {
  lvp_msg_t m;
  lvp_init(&m, LVP_MSG_NACK);
  m.u.nack.error = (uint8_t)e;
  return reply(m, req, out);
}

size_t GatewayHandler::buildAnnounce(uint8_t *buf, size_t cap, uint32_t nowMs) {
  lvp_msg_t m;
  lvp_init(&m, LVP_MSG_ANNOUNCE);
  m.hdr.seq = nextSeq();
  m.hdr.session = id_.session;
  memcpy(m.u.announce.gateway_id, id_.id, LVP_ID_LEN);
  m.u.announce.fw_version = id_.fwVersion;
  const LightSnapshot &s = light_.snapshot();
  m.u.announce.caps = (uint8_t)((id_.keyConfigured ? LVP_GW_KEY_REQUIRED : 0) | (pairing_.windowOpen(nowMs) ? LVP_GW_PAIRING_OPEN : 0) |
                                (s.ble ? LVP_GW_BLE_CONNECTED : 0) | (pairing_.count() > 0 ? LVP_GW_PAIRED : 0));
  m.u.announce.state_revision = s.revision;
  memcpy(m.u.announce.name, id_.name, sizeof(m.u.announce.name));
  return lvp_encode(&m, buf, cap, nullptr);
}

size_t GatewayHandler::buildStateNotify(uint8_t *buf, size_t cap) {
  lvp_msg_t m;
  lvp_init(&m, LVP_MSG_STATE);
  m.hdr.flags = LVP_FLAG_NOTIFY;
  m.hdr.seq = nextSeq();
  m.hdr.session = id_.session;
  m.u.state = light_.toProto();
  return lvp_encode(&m, buf, cap, nullptr);
}

void GatewayHandler::handle(const uint8_t src[LVP_ID_LEN], bool toBroadcast, const uint8_t *data, size_t len, uint32_t nowMs, Output &out) {
  out = Output();
  lvp_msg_t in;
  lvp_err_t e = lvp_decode(data, len, &in);

  if (e == LVP_ERR_SHORT || e == LVP_ERR_MAGIC || e == LVP_ERR_CRC || e == LVP_ERR_LENGTH) {
    out.dropReason = (uint8_t)e; // fremder oder beschaedigter Rahmen: still verwerfen
    return;
  }
  if (in.hdr.flags & (LVP_FLAG_RESPONSE | LVP_FLAG_NOTIFY)) {
    out.dropReason = LVP_ERR_UNSUPPORTED; // Gateway stellt keine Anfragen
    return;
  }
  if (e != LVP_OK) {
    if (!toBroadcast) { // Unicast-Anfrage mit falschem Inhalt beantworten, Broadcasts ignorieren
      bool passOn = e == LVP_ERR_VERSION || e == LVP_ERR_UNSUPPORTED || e == LVP_ERR_OUT_OF_RANGE;
      nack(in.hdr, passOn ? e : LVP_ERR_BAD_REQUEST, out);
    }
    out.dropReason = (uint8_t)e;
    return;
  }

  // Erkennung: immer beantworten (per Broadcast), Absender als Peer vormerken.
  if (in.hdr.type == LVP_MSG_DISCOVER) {
    out.replyLen = (uint8_t)buildAnnounce(out.reply, sizeof(out.reply), nowMs);
    out.replyBroadcast = true;
    out.ensurePeer = true;
    return;
  }
  if (toBroadcast) {
    out.dropReason = LVP_ERR_BAD_REQUEST; // Steuerbefehle nur per Unicast
    return;
  }

  const uint8_t *cached = nullptr;
  uint8_t cachedLen = 0;
  lvl_dedup_result_t d = lvl_dedup_check(&dedup_, src, in.hdr.session, in.hdr.seq, &cached, &cachedLen);

  if (d == LVL_DEDUP_STALE) {
    out.dropReason = LVP_ERR_BAD_REQUEST;
    return;
  }
  if (d == LVL_DEDUP_REPEAT) { // Wiederholung: gleiche Antwort, Befehl nicht erneut ausfuehren
    memcpy(out.reply, cached, cachedLen);
    out.replyLen = cachedLen;
    return;
  }

  lvp_msg_t m;

  switch (in.hdr.type) {
  case LVP_MSG_PAIR_REQ: {
    lvp_err_t pe = LVP_OK;

    if (!pairing_.contains(src)) {
      pe = pairing_.mayPair(id_.keyConfigured, nowMs);
      if (pe == LVP_OK) {
        pe = pairing_.add(src, in.u.pair_req.name);
        out.pairedChanged = pe == LVP_OK;
      }
    }
    if (pe != LVP_OK) {
      nack(in.hdr, pe, out);
      break;
    }
    lvp_init(&m, LVP_MSG_PAIR_RSP);
    m.u.pair_rsp.result = LVP_OK;
    memcpy(m.u.pair_rsp.gateway_id, id_.id, LVP_ID_LEN);
    memcpy(m.u.pair_rsp.name, id_.name, sizeof(m.u.pair_rsp.name));
    reply(m, in.hdr, out);
    out.ensurePeer = true;
    break;
  }
  default:
    if (!pairing_.contains(src)) {
      nack(in.hdr, LVP_ERR_NOT_PAIRED, out);
      break;
    }
    switch (in.hdr.type) {
    case LVP_MSG_UNPAIR:
      if (!pairing_.remove(src)) {
        nack(in.hdr, LVP_ERR_STORAGE, out);
        break;
      }
      lvp_init(&m, LVP_MSG_ACK);
      reply(m, in.hdr, out);
      out.pairedChanged = true;
      out.removePeer = true;
      break;
    case LVP_MSG_GET_STATE:
      lvp_init(&m, LVP_MSG_STATE);
      m.u.state = light_.toProto();
      reply(m, in.hdr, out);
      break;
    case LVP_MSG_SET_STATE: {
      lvp_err_t ae = light_.apply(in.u.set_state);
      if (ae != LVP_OK) {
        nack(in.hdr, ae, out);
        break;
      }
      lvp_init(&m, LVP_MSG_STATE); // bestaetigter Zustand nach Anwendung
      m.u.state = light_.toProto();
      reply(m, in.hdr, out);
      out.stateChanged = true;
      break;
    }
    case LVP_MSG_GET_CALIB:
    case LVP_MSG_SET_CALIB:
    case LVP_MSG_RESET_CALIB: {
      if (in.hdr.type == LVP_MSG_SET_CALIB && !cal_.save(in.u.set_calib.points)) {
        nack(in.hdr, LVP_ERR_STORAGE, out);
        break;
      }
      if (in.hdr.type == LVP_MSG_RESET_CALIB && !cal_.reset()) {
        nack(in.hdr, LVP_ERR_STORAGE, out);
        break;
      }
      if (in.hdr.type != LVP_MSG_GET_CALIB) {
        light_.setCalibRevision(cal_.revision());
        out.calibChanged = true;
        out.stateChanged = true;
      }
      lvp_init(&m, LVP_MSG_CALIB);
      m.u.calib.custom = cal_.custom() ? 1 : 0;
      m.u.calib.calib_revision = cal_.revision();
      cal_.points(m.u.calib.points);
      reply(m, in.hdr, out);
      break;
    }
    default:
      nack(in.hdr, LVP_ERR_UNSUPPORTED, out);
      break;
    }
    break;
  }
  if (out.replyLen) {
    lvl_dedup_store(&dedup_, src, out.reply, out.replyLen);
  }
}
