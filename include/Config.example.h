#pragma once

// ---- Pflicht: WLAN und Hardware ----

#define LEVI_WIFI_SSID "DEIN_WLAN"
#define LEVI_WIFI_PASSWORD "DEIN_PASSWORT"
#define LEVI_WIFI_HOSTNAME "leviluma"
#define LEVI_WIFI_TX_DBM 8.5f

#define LEVI_DISPLAY_SPI_MHZ 40
#define LEVI_DISPLAY_INVERT true
#define LEVI_BACKLIGHT_ACTIVE_HIGH true

// Drehung des Displays in Grad: 0, 90, 180 oder 270. Touch und Anzeige drehen sich mit.
// Nur die Ausrichtung des Panels; die Bedienoberflaeche bleibt gleich.
#define LEVI_DISPLAY_ROTATION 0

// ---- Kalibrierung ----

// Kalibrierung (Standard nach Reset). Ohne Eintrag gilt FastLED TypicalSMD5050.
// Linear-Gains pro Kanal (nur genutzt, wenn LEVI_CALIB_POINTS nicht gesetzt ist):
#define LEVI_CALIB_GR 1.000f
#define LEVI_CALIB_GG 0.690f
#define LEVI_CALIB_GB 0.940f

// Eigene Kalibrierung aus der Web-UI: 51 Werte (17 Zielfarben x R,G,B), Export aus dem Kalibrierungsbereich.
// Beispiel (Platzhalter, bitte durch deine Werte ersetzen):
// #define LEVI_CALIB_POINTS 255,255,255, 255,243,224, ...

// Helligkeitsbereich des Bandes: 100 = Werte 0..100 (Rust-Referenz), 255 = Prozent auf 0..255 (Python-Referenz)
#define LEVI_BRIGHTNESS_MAX 100

// ---- Optionale Einstellungen (auskommentiert = Standardwert im Code) ----
// Pflicht ist nur WLAN (oben). Diese Datei enthaelt bewusst nur Platzhalter, keine echten Zugangsdaten.

// Sekunden ohne Bedienung bis zur Rueckkehr auf die Mittelseite (Standard 60000 ms):
// #define LEVI_RECENTER_MS 60000

// Mikrofon-EQ-Modus nach "Mikrofon an" (Standard 0):
// #define LEVI_MIC_EQ_MODE 0

// Farbset des Farbwaehlers (LCD und Web-UI), flache Liste R,G,B,R,G,B,... mit 1 bis 24 Farben.
// Unabhaengig von der Kalibrierung. Standard: 17 Farben der Web-UI (Weiss bis Cyan, siehe Palette.h).
// Farbset des Farbwaehlers (LCD und Web-UI). Eintraege: LEVI_PICKER_ENTRY("Name", R, G, B),
// 1 bis 24 Stueck, Name hoechstens 15 Zeichen. Unabhaengig von der Kalibrierung.
// Standard: 15 benannte Farben (siehe Palette.h).
// #define LEVI_PICKER_LIST \
//   LEVI_PICKER_ENTRY("Warmweiß", 255, 243, 224), \
//   LEVI_PICKER_ENTRY("Rot", 255, 64, 64), \
//   LEVI_PICKER_ENTRY("Blau", 64, 64, 255)

// Anzahl der Schritte bei der Farbueberblendung (Standard 8):
// #define LEVI_FADE_STEPS 8

// Touch-Drehung in LovyanGFX, 0..7 (Standard 0; am Geraet pruefen):
// #define LEVI_TOUCH_OFFSET_ROTATION 0

// Touch: Ueberbrueckung kurzer Aussetzer in ms (Standard 40). Groesser = traegerer Release:
// #define LEVI_TOUCH_BRIDGE_MS 40

// Touch-Testscreen: 1 = Tippen zeichnet einen Kreis an der Tippposition (Bildschirm wird vorher geleert).
// Zum Pruefen der Touch-Koordinaten und der Drehung. Standard 0 (normale Bedienung).
// #define LEVI_TOUCH_TEST 0

// Farben vertauscht? Dann 0 setzen (RGB565-Byte-Reihenfolge der Display-Uebertragung). Standard 1.
// #define LEVI_RGB565_SWAP 1

// Wartezeiten auf die DMA-Uebertragung (Mittel und Maximum) alle 5 s ins Log. Standard 0.
// #define LEVI_FLUSH_STATS 0
