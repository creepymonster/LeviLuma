#pragma once
// Auswertung von WLAN-/ESP-NOW-Rahmen (ohne LeviLuma-Wissen, ohne ESP-IDF-Abhaengigkeit, auf dem Host testbar).
//
// Aufbau eines ESP-NOW-Rahmens laut ESP-IDF-Dokumentation (api-reference/network/esp_now):
//   MAC-Kopf (24) | Kategorie 127 (1) | OUI 18:FE:34 (3) | Zufallswerte (4) | Vendor-Element(e) | FCS (4)
//   Element: ID 221 (1) | Laenge (1) | OUI 18:FE:34 (3) | Typ 4 (1) | Version (1) | Body (0..250)
//   Adressfeld 1 = Ziel, 2 = Absender, 3 = Broadcast; ToDS/FromDS = 0.
// Verschluesselte Rahmen (CCMP) tragen im MAC-Kopf das Protected-Bit; ihr Inhalt ist hier nicht lesbar.
#include <stddef.h>
#include <stdint.h>

namespace dbg {

constexpr size_t MAX_CAPTURE = 320; // erfasste Bytes je Rahmen (ESP-NOW v1: hoechstens 293 inkl. FCS)
constexpr size_t MAC_HDR_LEN = 24;
constexpr uint8_t ESPNOW_CATEGORY = 127;
constexpr uint8_t ESPNOW_OUI[3] = {0x18, 0xFE, 0x34};
constexpr uint8_t ESPNOW_ELEMENT_ID = 221;
constexpr uint8_t ESPNOW_ELEMENT_TYPE = 4;

enum class Source : uint8_t {
  Promiscuous = 0,    // WLAN-Promiscuous-Modus: kompletter 802.11-Rahmen (Management-Rahmen)
  EspNowCallback = 1, // normaler ESP-NOW-Empfang: nur Nutzdaten, Adressen aus esp_now_recv_info_t
};

// Eine erfasste Nachricht, vom Empfangs-Callback in die Queue kopiert.
struct Capture {
  Source source;
  uint32_t t_ms;     // Laufzeit beim Empfang im Callback (esp_timer)
  bool has_rx_us;    // Hardware-Zeitstempel verfuegbar
  uint32_t rx_us;    // rx_ctrl.timestamp (lokale Zeit in us)
  bool has_rssi;
  int8_t rssi;       // dBm
  uint8_t channel;   // 0 = nicht verfuegbar
  bool fcs_included; // data/orig_len enthalten am Ende 4 Byte FCS (Promiscuous: sig_len)
  uint16_t orig_len; // Laenge laut Treiber
  uint16_t len;      // tatsaechlich kopierte Bytes (<= MAX_CAPTURE)
  bool has_addr;     // src/dst gesetzt (nur EspNowCallback)
  uint8_t src[6];
  uint8_t dst[6];
  uint8_t data[MAX_CAPTURE];
};

enum class Verdict : uint8_t {
  Ok = 0,    // ESP-NOW-Element vollstaendig vorhanden, Body lesbar
  Encrypted, // Protected-Bit gesetzt: Inhalt verschluesselt, nicht lesbar
  NotEspNow, // erkennbar kein ESP-NOW-Rahmen
  Truncated, // Erfassung kuerzer als der Rahmen: Body unvollstaendig
  Malformed, // Rahmen in sich unstimmig (Laengen, fehlendes Element)
  TooShort,  // zu kurz fuer den MAC-Kopf bzw. die ESP-NOW-Kopffelder
};

struct FrameInfo {
  Verdict verdict = Verdict::NotEspNow;
  const char *reason = ""; // Klartext zum Urteil (statischer Text)

  bool has_mac_header = false; // false bei EspNowCallback
  bool has_addrs = false;
  uint8_t dst[6] = {0}, src[6] = {0}, addr3[6] = {0};
  bool broadcast = false, multicast = false; // multicast = Gruppenadresse, aber nicht Broadcast

  // Frame Control / MAC-Kopf (nur mit has_mac_header)
  uint16_t frame_control = 0;
  uint8_t fc_type = 0, fc_subtype = 0;
  bool retry = false, protected_bit = false, to_ds = false, from_ds = false;
  uint16_t seq_ctrl = 0; // Sequenzfeld (Rahmenzaehler 12 Bit << 4 | Fragment)
  size_t mac_hdr_len = 0;

  // ESP-NOW-Kopf (nur wenn lesbar)
  bool has_category = false;
  uint8_t random[4] = {0};
  int element_count = 0; // Vendor-Elemente (alle IDs) im Rahmen
  int espnow_elements = 0; // davon ESP-NOW-Elemente
  bool has_element = false; // erstes ESP-NOW-Element gelesen
  uint8_t element_len = 0;
  uint8_t element_version_raw = 0; // ganzes Versions-Byte, Bedeutung der Bits je ESP-NOW-Version

  // Laengen (alle in Byte, eindeutig getrennt)
  size_t frame_len_driver = 0; // Laenge laut Treiber (mit FCS, falls fcs_included)
  size_t frame_len_no_fcs = 0; // ohne FCS
  size_t captured_len = 0;     // tatsaechlich erfasst
  bool fcs_present = false;
  uint8_t fcs[4] = {0};

  // Nutzdaten: ESP-NOW-Body bzw. bei Encrypted die Rohbytes nach dem MAC-Kopf
  const uint8_t *body = nullptr;
  size_t body_len = 0;      // vorhandene Bytes
  size_t body_declared = 0; // laut Element (Laenge - 5); 0, wenn unbekannt
  bool body_declared_known = false;
  bool body_complete = false; // body_len deckt alles ab, was der Rahmen verspricht
};

// Schneller Vorfilter fuer den Callback: moeglicherweise ESP-NOW? (Action-/Action-No-Ack-Rahmen und entweder
// unverschluesselt mit Kategorie 127 und Espressif-OUI oder Protected mit Broadcast im Adressfeld 3.)
// Pruefen nur die ersten Bytes, keine Allokation.
bool is_espnow_candidate(const uint8_t *frame, size_t len);

// Wertet einen Rahmen aus. Prueft jede Laenge vor dem Zugriff; verweist mit body auf c.data.
void parse_capture(const Capture &c, FrameInfo &fi);

const char *verdict_name(Verdict v);
const char *source_name(Source s);

} // namespace dbg
