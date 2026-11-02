#pragma once
// Individueller Gateway-Schluessel = ESP-NOW-LMK (16 Byte). Eingabe und Anzeige als 32 Hex-Zeichen.
// Der Schluessel wird nie per Funk uebertragen: er wird an Gateway (Kconfig oder serielle Konsole)
// und Controller (Eingabe in der Oberflaeche) getrennt hinterlegt. Die Verschluesselung uebernimmt
// ESP-NOW selbst (CCMP mit LMK, siehe ESP-IDF-Dokumentation zu esp_now_add_peer).
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LEVI_KEY_LEN 16
#define LEVI_KEY_HEX_LEN 32

#ifdef __cplusplus
extern "C" {
#endif

// Liest 32 Hex-Zeichen (Gross/Klein, Leerzeichen, ':' und '-' werden ignoriert).
// Ungueltig: falsche Laenge, fremde Zeichen, Schluessel nur aus Nullen.
bool levi_key_parse_hex(const char *s, uint8_t out[LEVI_KEY_LEN]);
// Schreibt 32 Hex-Zeichen in Grossbuchstaben plus Nullterminator (cap >= 33).
void levi_key_to_hex(const uint8_t key[LEVI_KEY_LEN], char *out, size_t cap);
bool levi_key_is_zero(const uint8_t key[LEVI_KEY_LEN]);

#ifdef __cplusplus
}
#endif
