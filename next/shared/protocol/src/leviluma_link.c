// Zuverlaessigkeitsschicht: offene Anfragen mit Wiederholung, Duplikaterkennung beim Empfaenger.
#include "leviluma_link.h"
#include <string.h>

void lvl_txq_init(lvl_txq_t *q, uint32_t timeout_ms, uint8_t max_tries) {
  memset(q, 0, sizeof(*q));
  q->timeout_ms = timeout_ms ? timeout_ms : 1;
  q->max_tries = max_tries ? max_tries : 1;
}

int lvl_txq_add(lvl_txq_t *q, const uint8_t peer[LVP_ID_LEN], uint16_t seq, uint8_t type, const uint8_t *frame, uint8_t len,
                uint32_t now, uint32_t tag) {
  if (len > LVP_MAX_FRAME) {
    return -1;
  }
  for (int i = 0; i < LVL_MAX_PENDING; i++) {
    lvl_pending_t *p = &q->slot[i];

    if (p->used) {
      continue;
    }
    memset(p, 0, sizeof(*p));
    p->used = true;
    memcpy(p->peer, peer, LVP_ID_LEN);
    p->seq = seq;
    p->type = type;
    p->tries = 1;
    p->first_ms = now;
    p->last_ms = now;
    p->tag = tag;
    p->len = len;
    memcpy(p->frame, frame, len);
    return i;
  }
  return -1;
}

bool lvl_txq_complete(lvl_txq_t *q, const uint8_t peer[LVP_ID_LEN], uint16_t ref_seq, lvl_pending_t *out) {
  for (int i = 0; i < LVL_MAX_PENDING; i++) {
    lvl_pending_t *p = &q->slot[i];

    if (p->used && p->seq == ref_seq && lvp_id_equal(p->peer, peer)) {
      if (out) {
        *out = *p;
      }
      p->used = false;
      return true;
    }
  }
  return false;
}

lvl_action_t lvl_txq_poll(lvl_txq_t *q, uint32_t now, int *idx, lvl_pending_t *out) {
  for (int i = 0; i < LVL_MAX_PENDING; i++) {
    lvl_pending_t *p = &q->slot[i];

    if (!p->used) {
      continue;
    }
    uint32_t wait = q->timeout_ms * p->tries; // linear wachsend: 1x, 2x, 3x ...

    if ((uint32_t)(now - p->last_ms) < wait) {
      continue;
    }
    if (p->tries >= q->max_tries) {
      if (out) {
        *out = *p;
      }
      p->used = false;
      if (idx) {
        *idx = i;
      }
      return LVL_TIMEOUT;
    }
    p->tries++;
    p->last_ms = now;
    if (idx) {
      *idx = i;
    }
    return LVL_RESEND;
  }
  return LVL_NONE;
}

int lvl_txq_cancel_peer(lvl_txq_t *q, const uint8_t peer[LVP_ID_LEN]) {
  int n = 0;
  for (int i = 0; i < LVL_MAX_PENDING; i++) {
    if (q->slot[i].used && lvp_id_equal(q->slot[i].peer, peer)) {
      q->slot[i].used = false;
      n++;
    }
  }
  return n;
}

bool lvl_txq_has(const lvl_txq_t *q, const uint8_t peer[LVP_ID_LEN], uint8_t type) {
  for (int i = 0; i < LVL_MAX_PENDING; i++) {
    if (q->slot[i].used && q->slot[i].type == type && lvp_id_equal(q->slot[i].peer, peer)) {
      return true;
    }
  }
  return false;
}

int lvl_txq_count(const lvl_txq_t *q) {
  int n = 0;
  for (int i = 0; i < LVL_MAX_PENDING; i++) {
    n += q->slot[i].used ? 1 : 0;
  }
  return n;
}

void lvl_dedup_init(lvl_dedup_t *d) {
  memset(d, 0, sizeof(*d));
}

static lvl_dedup_entry_t *dedup_find(lvl_dedup_t *d, const uint8_t peer[LVP_ID_LEN]) {
  for (int i = 0; i < LVL_DEDUP_PEERS; i++) {
    if (d->e[i].used && lvp_id_equal(d->e[i].peer, peer)) {
      return &d->e[i];
    }
  }
  return NULL;
}

lvl_dedup_result_t lvl_dedup_check(lvl_dedup_t *d, const uint8_t peer[LVP_ID_LEN], uint32_t session, uint16_t seq, const uint8_t **rsp,
                                   uint8_t *rsp_len) {
  lvl_dedup_entry_t *e = dedup_find(d, peer);
  d->tick++;

  if (rsp) {
    *rsp = NULL;
  }
  if (rsp_len) {
    *rsp_len = 0;
  }

  if (e && e->session == session) {
    e->lru = d->tick;

    if (seq == e->last_seq) {
      if (rsp) {
        *rsp = e->rsp;
      }
      if (rsp_len) {
        *rsp_len = e->rsp_len;
      }
      return LVL_DEDUP_REPEAT;
    }
    if (!lvp_seq_after(seq, e->last_seq)) {
      return LVL_DEDUP_STALE;
    }
  } else if (!e) {
    // freien oder am laengsten unbenutzten Eintrag nehmen
    e = &d->e[0];
    for (int i = 0; i < LVL_DEDUP_PEERS; i++) {
      if (!d->e[i].used) {
        e = &d->e[i];
        break;
      }
      if (d->e[i].lru < e->lru) {
        e = &d->e[i];
      }
    }
    memset(e, 0, sizeof(*e));
    e->used = true;
    memcpy(e->peer, peer, LVP_ID_LEN);
  }
  // neue Sitzung (Neustart des Absenders) oder neuere Sequenz
  e->session = session;
  e->last_seq = seq;
  e->rsp_len = 0;
  e->lru = d->tick;
  return LVL_DEDUP_NEW;
}

void lvl_dedup_store(lvl_dedup_t *d, const uint8_t peer[LVP_ID_LEN], const uint8_t *rsp, uint8_t len) {
  lvl_dedup_entry_t *e = dedup_find(d, peer);

  if (!e || len > LVP_MAX_FRAME) {
    return;
  }
  memcpy(e->rsp, rsp, len);
  e->rsp_len = len;
}

void lvl_dedup_forget(lvl_dedup_t *d, const uint8_t peer[LVP_ID_LEN]) {
  lvl_dedup_entry_t *e = dedup_find(d, peer);

  if (e) {
    e->used = false;
  }
}
