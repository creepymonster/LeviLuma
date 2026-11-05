# AGENTS.md – Hinweise für KI-Agenten

Dieses Dokument beschreibt das Projekt LeviLuma für KI-Assistenten, die daran arbeiten. Das Repository enthält fünf unabhängige Firmware-Projekte:

- **Legacy** (`legacy/`): Firmware für einen ESP32-S3 mit rundem Touch-Display, die einen BLE-LED-Strip direkt steuert (PlatformIO, Arduino). Alle Abschnitte bis „LeviLuma Next“ beschreiben dieses Projekt; **Pfade dort sind relativ zu `legacy/`** (z. B. `src/main.cpp` = `legacy/src/main.cpp`).
- **Next Controller** (`next/controller/`, ESP32-C5) und **Next Gateway** (`next/gateway/`, XIAO ESP32S3), beide natives ESP-IDF, siehe Abschnitt „LeviLuma Next“.
- **Next Round** (`next/controller-round/`, Waveshare-Hardware von Legacy, ESP-IDF): temporäre Fernbedienung für genau ein Gateway, siehe „LeviLuma Next“.
- **Next Diagnose** (`next/debug/`, XIAO ESP32S3, ESP-IDF): passiver ESP-NOW-Mitschnitt für den seriellen Monitor, siehe „Diagnosegerät“ unten.

Dazu kommt **Next Simulator** (`next/simulator/`, Desktop, CMake + SDL2, keine Firmware): die Original-LVGL-Oberflächen beider Next-Controller am PC, siehe „Desktop-Simulator“ unten.

Die READMEs (`README.md`, `legacy/README.md`, `next/README.md`) enthalten Nutzerdokumentation, dieses Dokument enthält das, was man für sicheres Arbeiten im Code wissen muss.

Die Kommunikation mit dem Entwickler (Reimar) läuft auf Deutsch. Kommentare, Log-Meldungen und UI-Texte sind ebenfalls deutsch.

## Was das Projekt macht (Legacy)

LeviLuma steuert einen LED-Strip der ELK-/Lotus-Lantern-Familie über BLE (Service `0xFFF0`, Schreib-Characteristic `0xFFF3`, 9-Byte-Pakete mit Start `0x7E` und Ende `0xEF`). Die Steuerung läuft über zwei Oberflächen, die denselben Zustand verändern:

- **LCD-UI** auf dem Display des Boards (LVGL, Touch).
- **Web-UI** im WLAN, erreichbar unter `leviluma.local` (mDNS) oder über die IP.

Gesteuert werden: Ein/Aus, Helligkeit (1–100 %, Standard beim Einschalten 100 %), Farbe (Farbset aus `LEVI_PICKER_LIST` mit Namen, Standard 15 Farben, und freie RGB-Werte), Musikmodus (Mikrofon, derzeit ohne Bedienelement) und eine Kalibrierung der Farben über die Web-UI.

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
| `include/CalibCore.h` | Farbkalibrierung (RBF-Modell, 10 Zielfarben) |
| `include/Palette.h` | Farbset des Farbwaehlers (`LeviPalette`, Einträge mit Namen aus `LEVI_PICKER_LIST`), Standardliste nach Farbton sortiert (Weiß, dann Regenbogen). Getrennt von den Kalibrierzielen |
| `include/ColorMath.h` | Farbmischung und Überblendung |
| `include/LotusProtocol.h` | BLE-Pakete des Bands |
| `include/LGFX_Board.h` | Display- und Touch-Konfiguration für LovyanGFX |
| `include/lv_conf.h` | LVGL-Konfiguration (LVGL 9) |
| `include/Config.example.h` | Vorlage für die Nutzerkonfiguration |

### Zustand und Nebenläufigkeit
- Es laufen mindestens drei Kontexte: die Arduino-Schleife (LVGL, Web-Server, WLAN), der BLE-Task (auf Core 0) und der LVGL-Flush (synchron).
- **Gemeinsamer Zustand** (`State`) wird nur über die Setter in `state.cpp` geändert, unter `stateLock`. Leser holen sich mit `snapshot()` eine Kopie.
- **`selected`** (Index der aktiven Farbe) gehört dem Loop-Task. Der BLE-Task darf es nicht lesen oder schreiben.
- **Kalibrierung** (`calModel`, `calCustom`) wird unter `calLock` gelesen und geschrieben. Schreiber rechnen auf einer Kopie und tauschen dann aus.
- **`sentRevision`** zeigt, bis zu welcher Revision alle Befehle an das Band gesendet wurden. Das ist eine Sendebestätigung, keine Ausführungsbestätigung des Bands.
- Den BLE-Task nicht blockieren lassen; er sendet einen Befehl pro Zeitfenster mit Pause.

### LCD-UI
- Drei Kacheln **untereinander**: oben An/Aus, Mitte Farbe, unten Helligkeit. Wischen geht nur vertikal (`lv_tileview`), damit der Helligkeitsslider nicht mit dem Seitenwechsel kollidiert.
- Die Mitte-Pille „An/Aus“ schaltet nach etwa 100 ms Halten ohne Füllanimation.
- Die Farben sind nach Farbton sortiert (Weiß oben, dann im Uhrzeigersinn Koralle bis Rot). Der Farbwähler nutzt das Farbset aus `LeviPalette` (`LEVI_PICKER_LIST`), nicht die Kalibrierziele. Musik hat im Farbrad keinen Punkt, Musik schaltet nur die Web-UI. Die Punkte liegen auf einem Kreis; in der Mitte stehen Name und Hex-Wert. Die Kalibrierung wirkt beim Senden trotzdem (über `setRGB`).
- Auf der Farbseite schaltet ein Tipp zur nächsten Farbe, am Ende wieder vorne.
- Nach `LEVI_RECENTER_MS` ohne Berührung springt die Ansicht ohne Animation zurück auf An/Aus. Nach 20 s wird die Hintergrundbeleuchtung ausgeschaltet; eine Berührung schaltet sie wieder ein.
- Im Modus `LEVI_TOUCH_TEST 1` zeigt das Display nur einen Touch-Testscreen (Kreis und Koordinaten).
- Überbrückung kurzer Touch-Aussetzer: `LEVI_TOUCH_BRIDGE_MS`.
- Anzeige wird nur bei geänderten Werten aktualisiert (`updateUI` mit Teilbereichen).
- Der Flush ist synchron: `flush()` schreibt den Bereich per `pushPixels` und meldet LVGL danach fertig. Der DMA-Flush ist derzeit nicht aktiv, er war in Tests die Ursache eines schwarzen Displays.
- Layout: `buildLayout()` wählt die Variante, heute `layoutRound240()` (runde 240×240-Kacheln untereinander). Weitere Displays bekommen eine eigene Layout-Funktion.

### Farbklima der LCD-UI
Die LCD-UI nutzt das Theme `lvgl_theme_shadcn` (Dark Mode, Palette `zinc_dark` unverändert). Es ersetzt die frühere eigene Farbtabelle. Quelle: https://github.com/AlixANNERAUD/lvgl_theme_shadcn, eingebunden als `alix-anneraud/lvgl_theme_shadcn@^0.3.1` in `platformio.ini`. Voraussetzung: LVGL ≥ 9.6.

Initialisierung in `src/main.cpp` (`themeInit`):
- Palette unverändert `lv_theme_shadcn_palette_zinc_dark()`. Keine eigene Primärfarbe.
- Fonts: Montserrat 16 (small/normal), 28 (large/h3), 32 (h2), 48 (h1).

Verwendung:
- Hintergrund: Theme-Hintergrund (nahezu Schwarz).
- Mitte-Pille (An und Aus): Variante `DEFAULT` (gefüllt, Theme-Primärfarbe). Der Text wechselt zwischen „Einschalten“ und „Ausschalten“.
- Gefüllter Regler und Indikator: Theme-Primärfarbe (hell). Ungefüllter Teil: Theme-Track.
- Sekundärtext (z. B. Hex-Anzeige im Farbwähler): `LV_THEME_SHADCN_TEXT_MUTED`.
- Primärtext: Theme-Textfarbe.
- Deaktiviert (kein BLE): nur `LV_STATE_DISABLED`, ohne eigene Deckkraft und ohne Outline. Die Darstellung übernimmt das Theme, wie beim Demo-Button „Disabled“.

Eigene Farben, die nicht vom Theme kommen: Statuspunkte (Grün an `#79DA89`, Rot aus `#E5484D`, Weiß unbekannt), Symbole WLAN/Bluetooth/Glühbirne (Grau `#9AA3AE` bzw. `#888888`), Farbpunkte des Farbwählers aus `LeviPalette`.

Regeln: Keine Farben für Bedienelemente (Pille, Regler, Text, Hintergrund) überschreiben, damit das Theme wirkt. Abweichungen nur mit Begründung in dieser Datei festhalten.

### Web-UI
- Eine Seite, ausgeliefert aus `WebUi.h`. Drei Karten untereinander in derselben Reihenfolge wie am Display (An/Aus, Farbe, Helligkeit).
- Der Musik-Schalter ist aus der Web-UI entfernt. Der Endpunkt `/api/music` bleibt für den späteren Musik-Screen.
- Endpunkte (in `src/web.cpp`):
  - `GET /api/state` – Zustand als JSON (inkl. `revision` und `sentRevision`).
  - `POST /api/power?on=0|1`
  - `POST /api/brightness?value=0..100`
  - `POST /api/raw?r=&g=&b=` – RGB ohne Kalibrierung.
  - `POST /api/color?r=&g=&b=` – RGB mit Kalibrierung.
  - `POST /api/music?on=0|1`
  - `GET /api/palette` – Farbset als JSON, z. B. `{"colors":[{"name":"Rot","hex":"#ff4040"},…],"music":true}`. Die Web-UI holt es beim Laden.
  - `GET /api/calib`, `POST /api/calib?p=<30 Werte, komma-getrennt 0..255>`, `POST /api/calib/reset`
- Eingaben werden mit `integer()` geprüft (nur Ziffern, 1–3 Stellen, Grenzen).
- **Keine Authentifizierung.** Die API ist nur für vertrauenswürdige Netze gedacht (steht in der README).

## Konfiguration
- Die echte Konfiguration liegt in `legacy/include/Config.h`. Diese Datei ist per `.gitignore` ausgeschlossen und enthält WLAN-Zugangsdaten. **Niemals committen, nie echte Zugangsdaten in andere Dateien schreiben.**
- `include/Config.example.h` ist die Vorlage. Sie enthält nur Platzhalter und die optionalen Einstellungen als Kommentar. Neue Optionen dort dokumentieren, mit ihrem Standardwert.
- Optionen werden im Code mit `#ifndef … #define … #endif` mit Standardwert versehen.

## Build und Test
- PlatformIO, Umgebung `waveshare_esp32s3`, Framework Arduino, Plattform `espressif32@6.10.0`. Bauen aus dem Repository-Wurzelordner: `pio run -d legacy`.
- Bibliotheken: LovyanGFX `^1.2.7`, LVGL `^9.6.0`, `lvgl_theme_shadcn` `^0.3.1`, NimBLE-Arduino `^1.4.3`.
- Der Entwickler baut und testet **lokal auf der Hardware**. Behauptungen über Build- oder Testergebnisse nur machen, wenn sie tatsächlich ausgeführt wurden. Im Zweifel sagen, dass es nicht geprüft wurde.
- Logging: `LEVI_LOG(...)` schreibt auf die Konsole (`printf`), nicht auf `Serial`.

## Worauf man achten sollte
- **Keine blockierenden Aufrufe in der Schleife.** Kein `delay()` über wenige ms, kein synchroner WLAN-Scan (der Scan ist asynchron).
- **SPI nur über die Display-Initialisierung und den Flush** ansprechen.
- **LVGL-Zeit** kommt über `lv_tick_set_cb(levTickMs)` nach `lv_init()`. Kein `lv_tick_inc` verwenden.
- **Touch-Drehung:** Die Koordinaten kommen bereits gedreht von LovyanGFX. `LEVI_TOUCH_OFFSET_ROTATION` (0–7) steuert das. Der richtige Wert für 270 Grad ist **noch nicht bestimmt**. Nicht stillschweigend ändern, sondern am Gerät testen lassen (`LEVI_TOUCH_TEST`).
- **RGB565-Byte-Reihenfolge:** `LEVI_RGB565_SWAP` (Standard 1). Bei vertauschten Farben zuerst diesen Schalter prüfen.
- **`LEVI_DISPLAY_ROTATION`** nur 0, 90, 180 oder 270 (per `static_assert` erzwungen).
- **Nicht vermischen:** Der BLE-Task greift nur auf Kopien zu. Neue geteilte Werte gehören in `State` oder in einen eigenen Mutex.
- **Bei Änderungen am Protokoll** (`LotusProtocol.h`) nur Pakete ändern, die am Gerät getestet wurden, oder klar als ungetestet kennzeichnen.
- **Keine Schnellschüsse bei Refactorings.** Das Projekt ist gerade in Module aufgeteilt (`ble`, `web`, `state`). Dabei Abhängigkeiten über `AppState.h` führen.

## Screenshots
- `legacy/tools/screenshots/render.sh`: LCD-UI per Harness (LVGL 9.6.0 mit Theme, Framebuffer), Aufruf `render.sh <repo> <lvgl-src> <theme-src> <out-root>`, Ausgabe `<out-root>/<B>x<H>/`, Dateien `01_off_disconnected`, `02_on_center`, `03_color`, `04_brightness`.
- `legacy/tools/screenshots/webui.py`: Web-UI per Playwright (Chromium) mit gemockter API, Ausgabe `legacy/screenshots/web/`: `desktop_1280x800.png`, `mobile_390x844.png`.
- Beides läuft in `.github/workflows/screenshots.yml` bei Änderungen an `legacy/src`, `legacy/include` und `legacy/tools/screenshots`. Dateinamen sind englisch.
- Die Harness-Szenen sind nicht das Gerät. Touch und Farben auf dem Display können abweichen.

## LeviLuma Next

Zwei ESP-IDF-Projekte (ESP-IDF 5.5.6, kein Arduino), gemeinsamer Code in `next/shared/`. Nutzerdoku: `next/README.md`, `next/docs/`.

- **Autoritativer Zustand:** Das Gateway (XIAO) hält den LightState und die Kalibrierung seiner Leiste. Der Controller zeigt nur bestätigte Zustände (STATE-Antworten/-Meldungen). Kein optimistisches Übernehmen von Befehlen; keine zweite Zustandsverwaltung (auch nicht in Zigbee).
- **Protokoll:** nur in `next/shared/protocol/` ändern, beide Seiten nutzen dieselben Dateien. Version (`LVP_VERSION`) bei inkompatiblen Änderungen erhöhen und `next/docs/ESP_NOW_PROTOKOLL.md` nachziehen. Rahmen ≤ 250 Byte, Little Endian, byteweise Serialisierung.
- **Hosttestbare Logik** liegt in `gateway/components/light_core/` und `controller/components/controller_core/` (ohne ESP-IDF-Abhängigkeit, NVS über `KvStore`). Neue Logik dorthin, Plattformcode in `main/`. Tests: `next/tests/run_tests.sh` (muss grün sein); Auswertung siehe „Testauswertung“ unten.
- **Kopien aus Legacy:** `CalibCore.h`, `ColorMath.h` (Gateway) und `Palette.h` (Controller) sind byte-gleiche Kopien; `LotusProtocol.h` nur ohne `<Arduino.h>`. Änderungen an der Kalibrierung in beiden Projekten konsistent halten.
- **Nebenläufigkeit Gateway:** `gwLock` für Modell/Kalibrierung/Kopplungen/Handler/gemerktes Band (`BandMemory`); BLE-Task nur mit Kopien; ESP-NOW-Callback kopiert nur in eine Queue.
- **Nebenläufigkeit Controller:** `appLock` (rekursiv) für Register und Link; LVGL nur im LVGL-Task; Zigbee-Lock und App-Lock nie gleichzeitig halten.
- **Zigbee:** nur mit `CONFIG_LEVILUMA_ZIGBEE_ENABLED` (Standard aus). Ohne Flag kein Zigbee-Code im Build (`main/CMakeLists.txt` über die Build-Eigenschaft `LEVI_WITH_ZIGBEE`). Endpoint-IDs sind stabil und persistent (`ZbMap`); nie umnummerieren.
- **Sicherheit:** nur ESP-NOW-Bordmittel (PMK/LMK, CCMP). Keine eigene Kryptografie, Schlüssel nie übertragen.
- **Abhängigkeiten des Controllers** (LVGL 9.6.0, lvgl_theme_shadcn 0.3.1, esp-zigbee-lib) werden per Git nach `next/controller/third_party/` geholt (`cmake/third_party.cmake`), nicht committen.
- **Hardwaredaten** (Pins, Treiber) nur mit Quelle ändern; Quellen stehen in `next/README.md`. Next ist **nicht am Gerät getestet**; keine Hardwaretests behaupten.
- **Oberfläche (beide Controller):** Musikmodus ohne Bedienelement, bis er am Band getestet ist (Protokollbefehle bleiben). Statussymbole nur ESP-NOW-Verbindung (Gateway-Symbol) und Licht an/aus (Glühbirne). RockBase: Ein/Aus nur auf der Übersicht.
- **Round Controller** (`next/controller-round/`): nur ESP-NOW. Kein BLE, keine WLAN-Verbindung/-Suche/AP, kein `esp_netif`, kein Zigbee/HTTP/MQTT; `set(COMPONENTS main)` hält ungenutzte Komponenten aus dem Build, `next/tools/check_round_build.py` prüft das. Display/Touch über LovyanGFX mit der Legacy-`LGFX_Board`-Konfiguration; Werte nur aus Legacy übernehmen. Genau ein Gateway (`Round::mayPair`). Nutzt `controller_core` aus `next/controller/` – Änderungen dort wirken auf beide Controller.
- Bauen: `idf.py set-target esp32s3|esp32c5 build` im jeweiligen Projektordner (auch `next/debug/`); Zigbee-Variante siehe `next/docs/BUILD_FLASH.md`. CI: `.github/workflows/next.yml` (Firmware-Builds inkl. Round, Diagnose und Funkprüfungen), `.github/workflows/tests.yml` (Host-Tests, Legacy-Kopien).

### Diagnosegerät (`next/debug/`)

Eigenständiges ESP-IDF-Projekt (v5.5.6, esp32s3), das ESP-NOW-Verkehr **nur beobachtet** und ihn im seriellen Monitor ausgibt. Nutzerdoku und Empfangsgrenzen: `next/debug/README.md`.

- **Passiv bleiben:** kein `esp_now_send`, `esp_now_add_peer`, `esp_wifi_connect`, WLAN-Scan, keine Frame-Injektion, kein NVS-Zugriff, kein BLE/Zigbee/LVGL. Dafür keine Ausnahmen einführen (auch nicht „nur zum Testen“). `next/tools/check_debug_build.py` prüft das im Build (CI, `.github/workflows/next.yml`) und muss grün bleiben; neue verbotene Aufrufe dort in die Liste aufnehmen.
- **Empfang:** WLAN-Promiscuous-Modus (nur Management-Rahmen) liefert Unicasts zwischen fremden Geräten; der normale ESP-NOW-Callback nur Broadcasts und Rahmen an die eigene MAC. Beide Wege dürfen nie doppelt ausgeben (`dbg::Dedup`). Die Grenzen (ein Kanal, Verluste, verschlüsselte Rahmen) nicht beschönigen: verschlüsselte Nutzdaten (Protected-Bit) werden **nie** als dekodiert dargestellt; keine eigene Kryptografie, keine Schlüssel in Logs, Beispielen oder Kconfig.
- **Aufteilung:** `components/debug_core/` ist reine, hosttestbare Logik ohne ESP-IDF (Rahmenauswertung `dbg_frame`, LeviLuma-Dekodierung `dbg_levi`, Ausgabe `dbg_format`/`dbg_text`, Zähler/Filter/Doppelerkennung/Kanalplan `dbg_state`, Befehle `dbg_cmd`). Funk, Tasks und serielle Ein-/Ausgabe liegen in `main/`. Die Callbacks kopieren nur begrenzt in die Queue (keine Formatierung, keine Ausgabe); verworfene Nachrichten werden gezählt. Jede Länge vor dem Feldzugriff prüfen.
- **Protokoll:** Dekodierung nutzt `next/shared/protocol/` (`lvp_decode`, `lvp_crc16`, Namenstabellen), keine Kopie. **Neuer Nachrichtentyp oder neues Feld im Protokoll** (`lvp_msg_type_t`, `lvp_type_name`): in `dbg_levi.cpp` den Typ in `levi_type_supported` und `fill_fields` ergänzen (Feldnamen wie in den Protokoll-Structs, verständliche Werte), in `next/tests/test_debug.cpp` ein gültiges Beispiel in `sample_for` und ggf. Feldprüfungen in `test_dbg_levi_field_values` ergänzen. `test_dbg_levi_all_protocol_types` schlägt fehl, bis beides vorhanden ist (und zählt die bekannten Typen; die Zahl dort bewusst anpassen). Bei neuer `LVP_VERSION`: Verhalten bei unbekannter Version bleibt „Fehlergrund + Rohdaten, keine Felder erraten“.
- **Tests:** Gruppe „Diagnose“ in `next/tests/` (`test_debug.cpp`). Alle Testrahmen sind synthetisch und so gekennzeichnet; keine Mitschnitte als echt ausgeben. Beispielausgaben in der README nur aus dem echten Formatierer erzeugen und als synthetisch kennzeichnen. Hardwaretests sind nicht erfolgt, solange sie nicht am Gerät liefen.
- **Kconfig:** nicht gesetzte `bool`-Optionen fehlen in `sdkconfig.h`; im Code `#if CONFIG_…` bzw. die Makros `LEVI_DBG_*` aus `main/app.h` verwenden, nie direkt in Ausdrücken. Die Variante `sdkconfig.defaults.full` (alle Optionen) wird in der CI mitgebaut, damit auch die optionalen Pfade übersetzen.
- **Bauen:** `idf.py set-target esp32s3 build` in `next/debug/`. Flashen ersetzt die Firmware auf dem XIAO (siehe README); nicht ungefragt flashen oder `erase-flash` empfehlen.

### Desktop-Simulator (`next/simulator/`)

Nativer LVGL-Simulator (C++20, CMake ≥ 3.20, SDL2) für die Oberflächen von RockBase (`leviluma-sim-controller`) und Round (`leviluma-sim-round`). Zwei Programme, weil beide UIs gleichnamige Symbole (`uiBuild`, `appView`, `GatewayView` …) definieren. Nutzerdoku: `next/simulator/README.md`.

- **Keine UI-Kopien:** Die UI-Dateien aus `next/controller/main/` und `next/controller-round/main/` werden direkt übersetzt. Oberflächen nur dort ändern, nie im Simulator nachbauen. Neue UI-Quelldateien in `next/simulator/CMakeLists.txt` eintragen.
- **Nur Plattform ersetzen:** Simulator-Adapter liegen in `next/simulator/targets/<variante>/` (`app_adapter.cpp` = Gegenstück zu `main/app.cpp`, `port_adapter.cpp` = Gegenstück zu `main/lvgl_port.cpp`). Wird `app.h`, `lvgl_port.h` oder die von der UI genutzte Seite von `board.h` geändert, den Adapter nachziehen (fehlende Funktionen meldet der Linker, neue `GatewayView`-Felder nicht). Logik gehört weiter in `controller_core`/`round_core`/`light_core`, nicht in die Adapter.
- **Mock-Gateway** (`mocks/mock_gateway.cpp`) nutzt die echte `light_core`-Logik; Bedienung nur über `MockGateway::post()` (Queue), nie direkt an LVGL-Widgets. Alle simulierten Namen beginnen mit „SIM“, Schlüssel dort sind reine Simulationsschlüssel.
- **Threading wie Firmware:** LVGL und SDL nur im Hauptthread; Link-Thread unter `appLock`; Funkempfang kopiert nur in Queues.
- **Trennung:** Keine SDL-/Simulator-Abhängigkeit in Firmware-Code, keine ESP-IDF-/FreeRTOS-Abhängigkeit im Simulator (`shim/` enthält nur die zwei `esp_lcd`-Platzhalter für `board.h`; Test `sim_no_idf`). Firmware-Dateien nicht für den Simulator umbauen; ist eine Anpassung unvermeidbar, klein halten und in `next/simulator/README.md` begründen.
- **Versionen:** LVGL/Theme werden aus `next/controller/cmake/third_party.cmake` gelesen (nicht doppelt pflegen). `config/lv_conf.h` entspricht den LVGL-Einstellungen der `sdkconfig.defaults`; `sdkconfig.h` wird aus den Kconfig-Dateien erzeugt.
- **Tests:** `ctest --test-dir next/simulator/build` (Skripte in `next/simulator/tests/*.sim`, SDL-Treiber `offscreen`). Sie gehören nicht zur Host-Testsuite `next/tests` und sind keine Hardwaretests. Simulator-Ergebnisse nie als Gerätetest ausgeben.

### Testauswertung (`next/tests/README.md`)
Die Datei wird **automatisch** von `next/tests/tools/gen_report.py` aus den maschinenlesbaren Ergebnissen des Testprogramms erzeugt (`LEVI_TEST_RESULTS`, JSON Lines). Abschlusszeitpunkt ist der CI-Lauf nach dem Merge auf `main` (`.github/workflows/tests.yml`, Job `report`): Er legt die Datei per Bot-Commit (`[skip ci]`, nur diese Datei, kein Force-Push) ab und nennt darin den getesteten Commit. Jeder CI-Lauf erzeugt zusätzlich eine Vorschau als Artefakt.

- **Nicht von Hand ändern** und nicht während der Entwicklung oder nach lokalen Läufen aktualisieren; sie entsteht erst zum Abschluss aus realen Ergebnissen.
- Neue Tests müssen über die gemeinsame Testregistrierung (`RUN`, `RUN_D` mit Kurzbeschreibung, `SKIP` mit Grund aus `tests/test_util.h`) laufen, damit sie automatisch in der Auswertung erscheinen. Keine zweite Liste der Tests pflegen.
- Neue Testgruppen (`levi_group` in `tests/test_main.cpp`) oder Prüfverfahren (z. B. weitere Sanitizer, neue CI-Schritte) müssen in `gen_report.py` und den Workflow integriert werden.
- Änderungen an Testsuite oder Testausgabe (JSON-Ereignisse `meta`/`group`/`begin`/`test`/`summary`) müssen mit dem Generator kompatibel bleiben; Generator und Testprogramm gemeinsam ändern und beide Fehlerpfade prüfen.
- Fehlende Ergebnisse, übersprungene Tests und nicht ausgeführte Prüfungen müssen ausdrücklich sichtbar bleiben. Host-Tests, Firmware-Builds und Hardwaretests bleiben getrennt; Hardwaretests gelten nie als bestanden, solange sie nicht am Gerät gelaufen sind.
- Erfolge dürfen niemals ohne tatsächliche Ausführung behauptet werden. Der CI-Status kommt von den Testschritten; die Berichtserstellung darf ihn nicht verdecken.

## Git-Konventionen
- Commit-Nachrichten auf Deutsch, Betreff im Imperativ oder als kurze Beschreibung, Details im Body.
- Commits, die ein KI-Agent erstellt, enden mit den Zeilen `Co-Authored-By` und `Claude-Session`, wie in der laufenden Sitzung vorgegeben.
- Vor dem Push `git pull --rebase`. Kein Force-Push.
- Änderungen werden in einem eigenen Branch committet und gepusht, **nie direkt auf `main`**. Liegt der Arbeitsstand auf `main`, zuerst einen Branch anlegen (`git switch -c <name>`), dann committen.
- Pull Requests nur auf ausdrücklichen Wunsch des Entwicklers erstellen; gemergt wird nicht durch den Agenten.

## Offene Punkte
- Helligkeit: natives BLE-Paket (`Lotus::brightness`) bestätigt.
- Touch-Drehung: `LEVI_TOUCH_OFFSET_ROTATION` am Gerät bestimmen.
- Musikmodus und Mikrofon-EQ sind nicht am Gerät bestätigt.
- Waveshare C6 2.8 als zweites Board: Konfiguration fehlt.
- Die Wabe mit Blasen-Physik für die Farbauswahl (iPhone-Gefühl) ist als späteres Vorhaben geplant. Sie braucht einen eigenen Timer und darf nicht mit den Tileview-Gesten kollidieren.
