# LeviLuma – LED-Steuerung mit Waveshare ESP32-S3 Touch LCD 1.28

Firmware für den Waveshare ESP32-S3-Touch-LCD-1.28 (rundes Display), die einen BLE-LED-Strip der ELK-/Lotus-Lantern-Familie steuert.
Die Bedienung läuft am Display (Touch) und über eine Weboberfläche im WLAN.

Technik: PlatformIO, Arduino, LovyanGFX (Display und Touch), LVGL 9, NimBLE-Arduino.

## Start
1. `include/Config.example.h` nach `include/Config.h` kopieren und WLAN-Zugangsdaten eintragen. `Config.h` nicht ins Repo committen.
2. Projektordner in VS Code mit PlatformIO öffnen.
3. Build, Upload und Serial Monitor (115200 Baud).
4. Weboberfläche: `http://leviluma.local/` oder die IP des ESP32.

## Bedienung
**Display:** Drei Seiten untereinander: oben An/Aus, Mitte Farbe, unten Helligkeit. Hoch und runter wischen wechselt die Seite.
- An/Aus: die Pille 500 ms halten.
- Farbe: Tippen springt zur nächsten Farbe, am Ende wieder vorn.
- Helligkeit: Balken wischen oder tippen.
- Nach längerer Pause springt die Anzeige zurück auf An/Aus, und das Display wird gedimmt.

**Web:** Dieselben drei Karten untereinander, dazu Kalibrierung der Farben.

## Konfiguration (`include/Config.h`)
Pflicht: `LEVI_WIFI_SSID`, `LEVI_WIFI_PASSWORD`, `LEVI_WIFI_HOSTNAME`, `LEVI_WIFI_TX_DBM`.

Optional, mit Standardwert (alle in `Config.example.h` dokumentiert):
- `LEVI_DISPLAY_ROTATION` – 0, 90, 180 oder 270 Grad (andere Werte brechen den Build ab).
- `LEVI_TOUCH_OFFSET_ROTATION` – Touch-Drehung in LovyanGFX (0–7). Muss für die Montage am Gerät geprüft werden.
- `LEVI_TOUCH_BRIDGE_MS` – Überbrückung kurzer Touch-Aussetzer (Standard 40 ms).
- `LEVI_PICKER_RGB` – Farbset des Farbwählers (LCD und Web), flache Liste R,G,B,… mit 1–24 Farben. Unabhängig von der Kalibrierung.
- `LEVI_TOUCH_TEST` – 1 zeigt nur einen Touch-Testscreen: Tippen zeichnet einen Kreis mit den Koordinaten.
- `LEVI_RGB565_SWAP` – Byte-Reihenfolge der Display-Übertragung (Standard 1). Bei vertauschten Farben auf 0 setzen.
- `LEVI_FLUSH_STATS` – 1 loggt alle 5 s die Wartezeiten auf die Display-Übertragung.
- `LEVI_RECENTER_MS` – Zeit ohne Bedienung bis zur Rückkehr auf An/Aus (Standard 60000).
- `LEVI_DISPLAY_SPI_MHZ`, `LEVI_DISPLAY_INVERT`, `LEVI_BACKLIGHT_ACTIVE_HIGH`, `LEVI_BRIGHTNESS_MAX`
- `LEVI_CALIB_*`, `LEVI_CALIB_POINTS` – Standard-Kalibrierung. Eigene Kalibrierung über die Web-Oberfläche.
- `LEVI_MIC_EQ_MODE`, `LEVI_FADE_STEPS`

## Weboberfläche und API
Die API hat keine Authentifizierung. Nur in einem vertrauenswürdigen Netz betreiben.

| Endpunkt | Zweck |
|---|---|
| `GET /api/state` | Zustand als JSON, inklusive `revision` und `sentRevision` |
| `POST /api/power?on=0\|1` | Ein/Aus |
| `POST /api/brightness?value=0..100` | Helligkeit |
| `POST /api/color?r=&g=&b=` | Farbe mit Kalibrierung |
| `POST /api/raw?r=&g=&b=` | Farbe ohne Kalibrierung |
| `POST /api/music?on=0\|1` | Musikmodus |
| `GET /api/calib` | Kalibrierung lesen |
| `GET /api/palette` | Farbset des Farbwählers als JSON (`colors`, `music`) |
| `POST /api/calib?p=…` | 51 Werte (17 Farben × RGB, je 0–255) speichern |
| `POST /api/calib/reset` | Kalibrierung auf den Standard zurücksetzen |

`sentRevision` zeigt, bis zu welcher Änderung alle Befehle an das Band gesendet wurden. Das ist eine Sendebestätigung, keine Bestätigung, dass das Band sie ausgeführt hat.

## Hardware
- Waveshare ESP32-S3-Touch-LCD-1.28, **16 MB Flash** (per `esptool flash_id` geprüft).
- Display GC9A01, 240×240, SPI: SCLK 10, MOSI 11, CS 9, DC 8, RESET 14. Hintergrundbeleuchtung PWM auf GPIO 2.
- Touch CST816S, I2C: SDA 6, SCL 7, RESET 13. Der Touch-Interrupt wird nicht genutzt, die Abfrage läuft zyklisch.
- LED-Strip: BLE, Gerätenamen beginnen mit `ELK-BLEDDM`, `ELK-BLEDOM`, `ELK-BLEDOB`, `MELK` oder `LEDBLE`. Service `FFF0`, Schreib-Characteristic `FFF3`.

## Getestet am Gerät
- Ein/Aus, RGB und Helligkeit (natives Paket).
- Touch, Seitenwechsel, Farbpunkte, Helligkeitsbalken.
- Musikmodus und Mikrofon-EQ sind nicht als getestet markiert.

## Bekannte Grenzen
- Der LED-Status ist optimistisch: Das Band bestätigt Befehle nicht zuverlässig.
- Die Touch-Drehung für die Montage am Gerät ist per `LEVI_TOUCH_OFFSET_ROTATION` einzustellen und bei Bedarf zu prüfen.
- Das Waveshare ESP32-C6-Touch-LCD-2.8 wird noch nicht unterstützt.
- Die Farbauswahl als Blasen-Wabe (Apple-Watch-Stil) ist geplant, aber nicht umgesetzt.
