#pragma once
// Zuverlaessigkeitsschicht ueber ESP-NOW (ohne Plattformabhaengigkeit, auf dem Host testbar).
//
// Sender (Controller): lvl_txq_t verwaltet offene Anfragen, wiederholt sie nach Zeitablauf
// mit linear wachsender Wartezeit und meldet endgueltige Zeitueberschreitungen.
// Empfaenger (Gateway): lvl_dedup_t erkennt Wiederholungen derselben Anfrage (gleiche Sitzung
// und Sequenznummer) und liefert die gespeicherte Antwort erneut, ohne den Befehl doppelt auszufuehren.
#include <stdbool.h>
#include <stdint.h>
#include "leviluma_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LVL_MAX_PENDING
#define LVL_MAX_PENDING 8
#endif

#ifndef LVL_DEDUP_PEERS
#define LVL_DEDUP_PEERS 4
#endif

typedef struct {
  bool used;
  uint8_t peer[LVP_ID_LEN];
  uint16_t seq;
  uint8_t type;
  uint8_t tries; // bisherige Sendungen
  uint32_t first_ms, last_ms;
  uint32_t tag; // frei fuer den Aufrufer
  uint8_t len;
  uint8_t frame[LVP_MAX_FRAME];
} lvl_pending_t;

typedef struct {
  lvl_pending_t slot[LVL_MAX_PENDING];
  uint32_t timeout_ms; // Wartezeit nach der ersten Sendung; danach timeout_ms * Versuch
  uint8_t max_tries;   // Sendungen insgesamt (1 = keine Wiederholung)
} lvl_txq_t;

typedef enum {
  LVL_NONE = 0,
  LVL_RESEND,  // Rahmen erneut senden (slot bleibt offen)
  LVL_TIMEOUT, // endgueltig gescheitert (slot wurde freigegeben, Kopie in *out)
} lvl_action_t;

void lvl_txq_init(lvl_txq_t *q, uint32_t timeout_ms, uint8_t max_tries);
// Neue offene Anfrage (erste Sendung zum Zeitpunkt now). Rueckgabe: Slot-Index oder -1, wenn voll.
int lvl_txq_add(lvl_txq_t *q, const uint8_t peer[LVP_ID_LEN], uint16_t seq, uint8_t type, const uint8_t *frame, uint8_t len,
                uint32_t now, uint32_t tag);
// Antwort eingetroffen: passenden Slot (Peer + ref_seq) freigeben und nach *out kopieren.
bool lvl_txq_complete(lvl_txq_t *q, const uint8_t peer[LVP_ID_LEN], uint16_t ref_seq, lvl_pending_t *out);
// Naechste faellige Aktion. Bei LVL_RESEND zeigt *idx auf den Slot (Rahmen in q->slot[*idx]).
// Bei LVL_TIMEOUT ist der Slot frei und *out enthaelt die Kopie. Aufrufer ruft solange auf, bis LVL_NONE.
lvl_action_t lvl_txq_poll(lvl_txq_t *q, uint32_t now, int *idx, lvl_pending_t *out);
// Alle offenen Anfragen an einen Peer verwerfen (z. B. beim Entkoppeln). Rueckgabe: Anzahl.
int lvl_txq_cancel_peer(lvl_txq_t *q, const uint8_t peer[LVP_ID_LEN]);
// Gibt es eine offene Anfrage dieses Typs an diesen Peer?
bool lvl_txq_has(const lvl_txq_t *q, const uint8_t peer[LVP_ID_LEN], uint8_t type);
int lvl_txq_count(const lvl_txq_t *q);
// Die offene Anfrage an diesen Peer (hoechstens eine pro Peer, wenn der Aufrufer das einhaelt) oder NULL.
const lvl_pending_t *lvl_txq_find_peer(const lvl_txq_t *q, const uint8_t peer[LVP_ID_LEN]);

typedef struct {
  bool used;
  uint8_t peer[LVP_ID_LEN];
  uint32_t session;
  uint16_t last_seq;
  uint32_t lru;
  uint8_t rsp_len;
  uint8_t rsp[LVP_MAX_FRAME];
} lvl_dedup_entry_t;

typedef struct {
  lvl_dedup_entry_t e[LVL_DEDUP_PEERS];
  uint32_t tick;
} lvl_dedup_t;

typedef enum {
  LVL_DEDUP_NEW = 0, // neue Anfrage: ausfuehren und Antwort mit lvl_dedup_store merken
  LVL_DEDUP_REPEAT,  // Wiederholung: gespeicherte Antwort erneut senden (rsp_len kann 0 sein)
  LVL_DEDUP_STALE,   // aeltere Anfrage als die zuletzt gesehene: verwerfen
} lvl_dedup_result_t;

void lvl_dedup_init(lvl_dedup_t *d);
lvl_dedup_result_t lvl_dedup_check(lvl_dedup_t *d, const uint8_t peer[LVP_ID_LEN], uint32_t session, uint16_t seq, const uint8_t **rsp,
                                   uint8_t *rsp_len);
void lvl_dedup_store(lvl_dedup_t *d, const uint8_t peer[LVP_ID_LEN], const uint8_t *rsp, uint8_t len);
void lvl_dedup_forget(lvl_dedup_t *d, const uint8_t peer[LVP_ID_LEN]);

// Online-Erkennung: zuletzt gesehen vor hoechstens timeout_ms (Ueberlauf von now beruecksichtigt).
static inline bool lvl_online(uint32_t last_seen_ms, uint32_t now, uint32_t timeout_ms) {
  return last_seen_ms != 0 && (uint32_t)(now - last_seen_ms) <= timeout_ms;
}

#ifdef __cplusplus
}
#endif
