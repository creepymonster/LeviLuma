#pragma once
// LeviLuma ESP-NOW-Protokoll (gemeinsam fuer Controller und Gateway).
//
// Rahmen: 16 Byte Kopf + Nutzdaten, hoechstens 250 Byte (ESP_NOW_MAX_DATA_LEN, ESP-NOW v1).
// Byte-Reihenfolge: Little Endian fuer alle Mehrbyte-Felder.
// Serialisierung byteweise (keine Struct-Casts), jede Laenge wird beim Lesen geprueft.
// Ausfuehrliche Beschreibung: next/docs/ESP_NOW_PROTOKOLL.md
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LVP_MAGIC0 0x4C // 'L'
#define LVP_MAGIC1 0x56 // 'V'
#define LVP_VERSION 1   // Protokollversion; andere Versionen werden abgelehnt

#define LVP_HEADER_LEN 16
#define LVP_MAX_FRAME 250
#define LVP_MAX_PAYLOAD (LVP_MAX_FRAME - LVP_HEADER_LEN)

#define LVP_ID_LEN 6    // Geraete-ID = Basis-MAC aus dem eFuse (stabil, eindeutig)
#define LVP_NAME_MAX 24 // Anzeigename in Bytes (UTF-8), ohne Nullterminator
#define LVP_CAL_POINTS 10
#define LVP_CAL_BYTES (LVP_CAL_POINTS * 3) // 10 Zielfarben x R,G,B

// Nachrichtentypen
typedef enum {
  LVP_MSG_DISCOVER = 0x01,    // Controller -> Broadcast: Wer ist da?
  LVP_MSG_ANNOUNCE = 0x02,    // Gateway -> Broadcast: Ich bin da (Antwort und periodischer Beacon)
  LVP_MSG_PAIR_REQ = 0x10,    // Controller -> Gateway: Kopplung anfragen
  LVP_MSG_PAIR_RSP = 0x11,    // Gateway -> Controller: Kopplung angenommen
  LVP_MSG_UNPAIR = 0x12,      // Controller -> Gateway: Kopplung aufheben (Antwort ACK)
  LVP_MSG_GET_STATE = 0x20,   // Controller -> Gateway: Zustand abfragen (Antwort STATE)
  LVP_MSG_SET_STATE = 0x21,   // Controller -> Gateway: Steuerbefehl (Antwort STATE = bestaetigter Zustand)
  LVP_MSG_STATE = 0x22,       // Gateway -> Controller: autoritativer LightState
  LVP_MSG_GET_CALIB = 0x30,   // Controller -> Gateway: Kalibrierung lesen (Antwort CALIB)
  LVP_MSG_SET_CALIB = 0x31,   // Controller -> Gateway: Kalibrierung speichern (Antwort CALIB)
  LVP_MSG_RESET_CALIB = 0x32, // Controller -> Gateway: Kalibrierung auf Standard (Antwort CALIB)
  LVP_MSG_CALIB = 0x33,       // Gateway -> Controller: Kalibrierung
  LVP_MSG_ACK = 0x7E,         // Allgemeine positive Antwort ohne Nutzdaten
  LVP_MSG_NACK = 0x7F,        // Ablehnung mit Fehlercode
} lvp_msg_type_t;

// Kopf-Flags
#define LVP_FLAG_RESPONSE 0x01 // Antwort auf ref_seq
#define LVP_FLAG_NOTIFY 0x02   // Unaufgeforderte Meldung (z. B. Zustandsaenderung)

// Fehlercodes (NACK und Rueckgabewerte)
typedef enum {
  LVP_OK = 0,
  LVP_ERR_NOT_PAIRED = 1,     // Absender ist nicht gekoppelt
  LVP_ERR_BAD_REQUEST = 2,    // Nutzdaten ungueltig
  LVP_ERR_UNSUPPORTED = 3,    // Nachrichtentyp unbekannt
  LVP_ERR_BUSY = 4,           // voruebergehend nicht moeglich
  LVP_ERR_STORAGE = 5,        // NVS-Fehler
  LVP_ERR_PAIRING_CLOSED = 6, // Kopplungsfenster geschlossen
  LVP_ERR_VERSION = 7,        // Protokollversion passt nicht
  LVP_ERR_OUT_OF_RANGE = 8,   // Wert ausserhalb des Bereichs
  LVP_ERR_FULL = 9,           // keine freien Plaetze (z. B. Kopplungen)
  // Nur lokal (Decoder), werden nie gesendet:
  LVP_ERR_SHORT = 100,   // Rahmen zu kurz
  LVP_ERR_MAGIC = 101,   // falsche Kennung
  LVP_ERR_CRC = 102,     // Pruefsumme falsch
  LVP_ERR_LENGTH = 103,  // Laengenangabe passt nicht
  LVP_ERR_TOO_BIG = 104, // Puffer zu klein beim Kodieren
} lvp_err_t;

// Faehigkeiten/Status im ANNOUNCE
#define LVP_GW_KEY_REQUIRED 0x01 // Gateway nutzt einen individuellen Schluessel (ESP-NOW-LMK)
#define LVP_GW_PAIRING_OPEN 0x02 // Kopplung ohne Schluessel gerade erlaubt (Kopplungsfenster)
#define LVP_GW_BLE_CONNECTED 0x04
#define LVP_GW_PAIRED 0x08 // mit mindestens einem Controller gekoppelt

// Felder in SET_STATE (mask)
#define LVP_SET_POWER 0x01
#define LVP_SET_BRIGHTNESS 0x02
#define LVP_SET_COLOR 0x04 // RGB mit Kalibrierung (wie Legacy setRGB)
#define LVP_SET_RAW 0x08   // RGB ohne Kalibrierung (wie Legacy setRaw); zusammen mit LVP_SET_COLOR unzulaessig
#define LVP_SET_MUSIC 0x10
#define LVP_SET_ALL (LVP_SET_POWER | LVP_SET_BRIGHTNESS | LVP_SET_COLOR | LVP_SET_RAW | LVP_SET_MUSIC)

// Flags im STATE
#define LVP_STATE_MUSIC 0x01
#define LVP_STATE_RAW 0x02
#define LVP_STATE_BLE 0x04

#define LVP_BRIGHTNESS_MIN 1
#define LVP_BRIGHTNESS_MAX 100

typedef struct {
  uint8_t magic0, magic1;
  uint8_t version;
  uint8_t type;
  uint8_t flags;
  uint8_t len;      // Nutzdatenlaenge
  uint16_t seq;     // Sequenznummer des Absenders (pro Sitzung fortlaufend, 0 wird uebersprungen)
  uint16_t ref_seq; // bei Antworten: seq der Anfrage, sonst 0
  uint32_t session; // Zufallswert pro Start des Absenders
  uint16_t crc;     // CRC-16/CCITT-FALSE ueber Kopf (crc = 0) und Nutzdaten
} lvp_header_t;

typedef struct {
  uint8_t controller_id[LVP_ID_LEN];
  char name[LVP_NAME_MAX + 1];
} lvp_discover_t;

typedef struct {
  uint8_t gateway_id[LVP_ID_LEN];
  uint16_t fw_version; // Major << 8 | Minor
  uint8_t caps;        // LVP_GW_*
  uint32_t state_revision;
  char name[LVP_NAME_MAX + 1];
} lvp_announce_t;

typedef struct {
  uint8_t controller_id[LVP_ID_LEN];
  char name[LVP_NAME_MAX + 1];
} lvp_pair_req_t;

typedef struct {
  uint8_t result; // lvp_err_t
  uint8_t gateway_id[LVP_ID_LEN];
  char name[LVP_NAME_MAX + 1];
} lvp_pair_rsp_t;

typedef struct {
  uint8_t controller_id[LVP_ID_LEN];
} lvp_unpair_t;

typedef struct {
  uint8_t mask; // LVP_SET_*
  uint8_t power;
  uint8_t brightness; // 1..100
  uint8_t r, g, b;
  uint8_t music;
} lvp_set_state_t;

// Autoritativer LightState des Gateways.
typedef struct {
  uint32_t revision;      // steigt bei jeder angenommenen Aenderung
  uint32_t sent_revision; // bis zu dieser Revision an das Band gesendet (Sendebestaetigung, keine Ausfuehrungsbestaetigung)
  int8_t power;           // -1 unbekannt, 0 aus, 1 an
  uint8_t brightness;     // 1..100
  uint8_t r, g, b;
  uint8_t flags;          // LVP_STATE_*
  uint16_t calib_revision;
} lvp_state_t;

typedef struct {
  uint8_t custom; // 1 = gespeicherte Kalibrierung, 0 = Standard
  uint16_t calib_revision;
  uint8_t points[LVP_CAL_BYTES];
} lvp_calib_t;

typedef struct {
  uint8_t points[LVP_CAL_BYTES];
} lvp_set_calib_t;

typedef struct {
  uint8_t error; // lvp_err_t
} lvp_nack_t;

typedef struct {
  lvp_header_t hdr;
  union {
    lvp_discover_t discover;
    lvp_announce_t announce;
    lvp_pair_req_t pair_req;
    lvp_pair_rsp_t pair_rsp;
    lvp_unpair_t unpair;
    lvp_set_state_t set_state;
    lvp_state_t state;
    lvp_calib_t calib;
    lvp_set_calib_t set_calib;
    lvp_nack_t nack;
  } u;
} lvp_msg_t;

// Kopf vorbelegen (Kennung, Version, Typ). seq/session/ref_seq setzt der Aufrufer.
void lvp_init(lvp_msg_t *m, lvp_msg_type_t type);

// Kodiert m in buf. Gibt die Rahmenlaenge zurueck, 0 bei Fehler (*err, wenn angegeben).
size_t lvp_encode(const lvp_msg_t *m, uint8_t *buf, size_t cap, lvp_err_t *err);

// Dekodiert einen Rahmen. Prueft Kennung, Version, Laenge, CRC und die Nutzdaten des Typs.
// Unbekannte Typen ergeben LVP_ERR_UNSUPPORTED (der Kopf in m ist dann gueltig).
lvp_err_t lvp_decode(const uint8_t *buf, size_t len, lvp_msg_t *m);

// Nur den Kopf lesen (z. B. fuer eine NACK-Antwort bei ungueltigen Nutzdaten).
lvp_err_t lvp_decode_header(const uint8_t *buf, size_t len, lvp_header_t *h);

uint16_t lvp_crc16(const uint8_t *data, size_t len, uint16_t crc);

// Pruefung der Steuerwerte (gemeinsam fuer Controller-Vorpruefung und Gateway).
lvp_err_t lvp_validate_set_state(const lvp_set_state_t *s);

// Sequenzvergleich mit Ueberlauf: true, wenn a nach b liegt.
static inline bool lvp_seq_after(uint16_t a, uint16_t b) {
  return (int16_t)(uint16_t)(a - b) > 0;
}

// Naechste Sequenznummer (0 ist reserviert fuer "keine").
static inline uint16_t lvp_seq_next(uint16_t s) {
  s = (uint16_t)(s + 1);
  return s == 0 ? 1 : s;
}

const char *lvp_type_name(uint8_t type);
const char *lvp_err_name(uint8_t err);

// ID als Text "AA:BB:CC:DD:EE:FF" (out mindestens 18 Byte).
void lvp_id_to_str(const uint8_t id[LVP_ID_LEN], char *out, size_t cap);
bool lvp_id_equal(const uint8_t a[LVP_ID_LEN], const uint8_t b[LVP_ID_LEN]);

#ifdef __cplusplus
}
#endif
