# AGENTS.md – Hinweise für KI-Agenten

Dieses Dokument beschreibt das Projekt LeviLuma für KI-Assistenten, die daran arbeiten. Kurz: Firmware für einen ESP32-S3 mit rundem Touch-Display, die einen BLE-LED-Strip steuert. Die README enthält Nutzerdokumentation, dieses Dokument enthält das, was man für sicheres Arbeiten im Code wissen muss.

Die Kommunikation mit dem Entwickler (Reimar) läuft auf Deutsch. Kommentare, Log-Meldungen und UI-Texte sind ebenfalls deutsch.

## Was das Projekt macht

LeviLuma steuert einen LED-Strip der ELK-/Lotus-Lantern-Familie über BLE (Service `0xFFF0`, Schreib-Characteristic `0xFFF3`, 9-Byte-Pakete mit Start `0x7E` und Ende `0xEF`). Die Steuerung läuft über zwei Oberflächen, die denselben Zustand verändern:

- **LCD-UI** auf dem Display des Boards (LVGL, Touch).
- **Web-UI** im WLAN, erreichbar unter `leviluma.local` (mDNS) oder über die IP.

Gesteuert werden: Ein/Aus, Helligkeit (1–100 %), Farbe (Farbset aus `LEVI_PICKER_RGB`, Standard 17 Farben, und freie RGB-Werte), Musikmodus (Mikrofon) und eine Kalibrierung der Farben über die Web-UI.

## Hardware

### Controller-Board: Waveshare ESP32-S3-Touch-LCD-1.28 (rund)
- MCU: ESP32-S3, Dual-Core, WLAN und BLE.
- Flash: **16 MB** (per `esptool flash_id` am Gerät bestätigt). Die Partitionstabelle ist deshalb `default_16MB.csv` (siehe `platformio.ini`).
- Display: GC9A01, 240×240, rund, SPI. Angesteuert über **LovyanGFX** (`Panel_GC9A01`), Pins in `include/LGFX_Board.h`.
- Hintergrundbeleuchtung: PWM über GPIO 2 (`backlight()` in `src/main.cpp`).
- Touch: **CST816S** kapazitiv über I2C (SDA 6, SCL 7, Reset 13, Adresse 0x15). Die Touch-Abfrage läuft über LovyanGFX (`Touch_CST816S`).
- Der CST816S braucht eine verdrahtete Reset-Leitung, sonst antwortet er nicht.

### LED-Strip (ELK / Lotus Lantern)
- BLE, Namen beginnen mit `ELK-BLEDDM`, `ELK-BLEDOM`, `ELK-BLEDOB`, `MELK` oder `LEDBLE` (`bleNameMatches()` in `src/ble.cpp`).
- Befehle in `include/LotusProtocol.h`: Ein/Aus, Farbe, Helligkeit, Musikmodus, Mikrofon-EQ.
- Laut README sind Ein/Aus, RGB und Helligkeit am Gerät getestet. Musikmodus und Mikrofon-EQ sind nicht als getestet markiert.
- Das Band braucht nach dem Verbinden etwa 1 s, bevor die Services gelesen werden. Das ist im BLE-Task berücksichtigt.

### Ziel-Board (geplant)
Ein Waveshare ESP32-C6-Touch-LCD-2.8 soll später unterstützt werden. Dessen Pins, Reset und Hintergrundbeleuchtung laufen teils über einen CH32-IO-Expander. Das ist **noch nicht** umgesetzt. Board-spezifische Werte gehören deshalb in eine eigene Konfiguration und nicht in den gemeinsamen Code.

## Architektur

| Datei | Aufgabe |
|---|---|
| `src/main.cpp` | Setup, Schleife, LCD-Oberfläche (LVGL), Touch, Flush, WLAN-Verwaltung |
| `src/state.cpp` | Geteilter Zustand (`State`), Setter, Kalibrierung inkl. NVS |
| `src/ble.cpp` | BLE-Task: Scan, Verbindung, Senden der Befehle |
| `src/web.cpp` | HTTP-Server und Endpunkte |
| `include/AppState.h` | Gemeinsame Deklarationen: `State`, `LEVI_LOG`, Setter, Kalibrierfunktionen |
| `include/Ble.h`, `include/Web.h` | Schnittstellen der Module |
| `include/WebUi.h` | Die Web-Oberfläche als HTML/CSS/JS in einem String (`LEVI_HTML`) |
| `include/CalibCore.h` | Farbkalibrierung (RBF-Modell, 17 Zielfarben) |
| `include/Palette.h` | Farbset des Farbwaehlers (`LeviPalette`, Standard aus `LEVI_PICKER_RGB`). Getrennt von den Kalibrierzielen |
| `include/ColorMath.h` | Farbmischung und Überblendung |
| `include/LotusProtocol.h` | BLE-Pakete des Bands |
| `include/LGFX_Board.h` | Display- und Touch-Konfiguration für LovyanGFX |
| `include/lv_conf.h` | LVGL-Konfiguration (LVGL 9) |
| `include/Config.example.h` | Vorlage für die Nutzerkonfiguration |

### Zustand und Nebenläufigkeit
- Es laufen mindestens drei Kontexte: die Arduino-Schleife (LVGL, Web-Server, WLAN), der BLE-Task (auf Core 0) und der LVGL-Flush über DMA.
- **Gemeinsamer Zustand** (`State`) wird nur über die Setter in `state.cpp` geändert, unter `stateLock`. Leser holen sich mit `snapshot()` eine Kopie.
- **`selected`** (Index der aktiven Farbe) gehört dem Loop-Task. Der BLE-Task darf es nicht lesen oder schreiben.
- **Kalibrierung** (`calModel`, `calCustom`) wird unter `calLock` gelesen und geschrieben. Schreiber rechnen auf einer Kopie und tauschen dann aus.
- **`sentRevision`** zeigt, bis zu welcher Revision alle Befehle an das Band gesendet wurden. Das ist eine Sendebestätigung, keine Ausführungsbestätigung des Bands.
- Den BLE-Task nicht blockieren lassen; er sendet einen Befehl pro Zeitfenster mit Pause.

### LCD-UI
- Drei Kacheln **untereinander**: oben An/Aus, Mitte Farbe, unten Helligkeit. Wischen geht nur vertikal (`lv_tileview`), damit der Helligkeitsslider nicht mit dem Seitenwechsel kollidiert.
- Die Mitte-Pille „An/Aus“ wird 500 ms gehalten (Füllanimation).
- Der Farbwähler nutzt das Farbset aus `LeviPalette` (`LEVI_PICKER_RGB`), nicht die Kalibrierziele. Die Kalibrierung wirkt beim Senden trotzdem (über `setRGB`).
- Auf der Farbseite schaltet ein Tipp zur nächsten Farbe, am Ende wieder vorne.
- Nach `LEVI_RECENTER_MS` ohne Berührung springt die Ansicht ohne Animation zurück auf An/Aus. Nach 20 s wird das Display gedimmt.
- Im Modus `LEVI_TOUCH_TEST 1` zeigt das Display nur einen Touch-Testscreen (Kreis und Koordinaten).
- Überbrückung kurzer Touch-Aussetzer: `LEVI_TOUCH_BRIDGE_MS`.
- Anzeige wird nur bei geänderten Werten aktualisiert (`updateUI` mit Teilbereichen).
- Der Flush ist asynchron mit Doppelpuffer (DMA). `flushRelease()` gibt den Puffer frei.

### Farbklima der LCD-UI
Festgelegt. Schwarzer Hintergrund, zwei Akzente aus der Palette. Die LED-Farbe erscheint nicht als Bedienelement-Farbe, sondern nur als Farbpunkt-Auswahl im Farbwähler.

- Hintergrund: `#000000`
- Fläche (Aus-Pille): `#13141A`
- Trennlinie: `#262832`
- Ungefüllter Regler-Bereich: `#3A3C48` mit 1 px Kante `#5A5D6B`
- Überschriften und Statustexte (Bereichsnamen wie „Ausgeschaltet“): `#FF6B6B` (Koralle, 7,6:1 auf Schwarz)
- Komponenten (An-Pille, Rahmen der Aus-Pille, gefüllter Regler): `#FFE66D` (Gelb, 16,8:1 auf Schwarz)
- Schrift auf der gelben An-Pille: `#000000`
- Text primär (Prozentwert, Werte): `#F4F6F8`
- Text sekundär (Hinweise, „W“/„B“, Aus-Beschriftung): `#9AA3AE`
- Halten-Füllung: weiß mit 18 % Deckkraft
- Deaktiviert: Deckkraft 40 %, keine Farbänderung

Regeln: Nur die beiden Akzente plus Neutraltöne. Keine Schatten oder Glows. Text mindestens 4,5:1 Kontrast zu seinem Untergrund (WCAG AA).

Noch nicht im Code umgesetzt. Nach der Umsetzung zeigen die Screenshots in `screenshots/` den Stand im Code.

### Web-UI
- Eine Seite, ausgeliefert aus `WebUi.h`. Drei Karten untereinander in derselben Reihenfolge wie am Display (An/Aus, Farbe, Helligkeit).
- Endpunkte (in `src/web.cpp`):
  - `GET /api/state` – Zustand als JSON (inkl. `revision` und `sentRevision`).
  - `POST /api/power?on=0|1`
  - `POST /api/brightness?value=0..100`
  - `POST /api/raw?r=&g=&b=` – RGB ohne Kalibrierung.
  - `POST /api/color?r=&g=&b=` – RGB mit Kalibrierung.
  - `POST /api/music?on=0|1`
  - `GET /api/palette` – Farbset des Farbwaehlers als JSON, z. B. `{"colors":["#ffffff",…],"music":true}`. Die Web-UI holt es beim Laden.
  - `GET /api/calib`, `POST /api/calib?p=<51 Werte, komma-getrennt 0..255>`, `POST /api/calib/reset`
- Eingaben werden mit `integer()` geprüft (nur Ziffern, 1–3 Stellen, Grenzen).
- **Keine Authentifizierung.** Die API ist nur für vertrauenswürdige Netze gedacht (steht in der README).

## Konfiguration
- Die echte Konfiguration liegt in `include/Config.h`. Diese Datei ist per `.gitignore` ausgeschlossen und enthält WLAN-Zugangsdaten. **Niemals committen, nie echte Zugangsdaten in andere Dateien schreiben.**
- `include/Config.example.h` ist die Vorlage. Sie enthält nur Platzhalter und die optionalen Einstellungen als Kommentar. Neue Optionen dort dokumentieren, mit ihrem Standardwert.
- Optionen werden im Code mit `#ifndef … #define … #endif` mit Standardwert versehen.

## Build und Test
- PlatformIO, Umgebung `waveshare_esp32s3`, Framework Arduino, Plattform `espressif32@6.10.0`.
- Bibliotheken: LovyanGFX `^1.2.7`, LVGL `^9.2.0`, NimBLE-Arduino `^1.4.3`.
- Der Entwickler baut und testet **lokal auf der Hardware**. Behauptungen über Build- oder Testergebnisse nur machen, wenn sie tatsächlich ausgeführt wurden. Im Zweifel sagen, dass es nicht geprüft wurde.
- Logging: `LEVI_LOG(...)` schreibt auf die Konsole (`printf`), nicht auf `Serial`.

## Worauf man achten sollte
- **Keine blockierenden Aufrufe in der Schleife.** Kein `delay()` über wenige ms, kein synchroner WLAN-Scan (der Scan ist asynchron).
- **Keine SPI-Zugriffe außerhalb des Flush**, solange die DMA läuft. Der Bus bleibt in `setup()` für das Display belegt.
- **LVGL-Zeit** kommt über `lv_tick_set_cb(levTickMs)` nach `lv_init()`. Kein `lv_tick_inc` verwenden.
- **Touch-Drehung:** Die Koordinaten kommen bereits gedreht von LovyanGFX. `LEVI_TOUCH_OFFSET_ROTATION` (0–7) steuert das. Der richtige Wert für 270 Grad ist **noch nicht bestimmt**. Nicht stillschweigend ändern, sondern am Gerät testen lassen (`LEVI_TOUCH_TEST`).
- **RGB565-Byte-Reihenfolge:** `LEVI_RGB565_SWAP` (Standard 1). Bei vertauschten Farben zuerst diesen Schalter prüfen.
- **`LEVI_DISPLAY_ROTATION`** nur 0, 90, 180 oder 270 (per `static_assert` erzwungen).
- **Nicht vermischen:** Der BLE-Task greift nur auf Kopien zu. Neue geteilte Werte gehören in `State` oder in einen eigenen Mutex.
- **Bei Änderungen am Protokoll** (`LotusProtocol.h`) nur Pakete ändern, die am Gerät getestet wurden, oder klar als ungetestet kennzeichnen.
- **Keine Schnellschüsse bei Refactorings.** Das Projekt ist gerade in Module aufgeteilt (`ble`, `web`, `state`). Dabei Abhängigkeiten über `AppState.h` führen.

## Git-Konventionen
- Commit-Nachrichten auf Deutsch, Betreff im Imperativ oder als kurze Beschreibung, Details im Body.
- Commits, die ein KI-Agent erstellt, enden mit den Zeilen `Co-Authored-By` und `Claude-Session`, wie in der laufenden Sitzung vorgegeben.
- Vor dem Push `git pull --rebase`. Kein Force-Push.
- Nur committen oder pushen, wenn der Entwickler das verlangt.

## Offene Punkte
- Helligkeit: natives BLE-Paket (`Lotus::brightness`) bestätigt.
- Touch-Drehung: `LEVI_TOUCH_OFFSET_ROTATION` am Gerät bestimmen.
- Musikmodus und Mikrofon-EQ sind nicht am Gerät bestätigt.
- Waveshare C6 2.8 als zweites Board: Konfiguration fehlt.
- Die Wabe mit Blasen-Physik für die Farbauswahl (iPhone-Gefühl) ist als späteres Vorhaben geplant. Sie braucht einen eigenen Timer und darf nicht mit den Tileview-Gesten kollidieren.
