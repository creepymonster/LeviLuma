#pragma once
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
