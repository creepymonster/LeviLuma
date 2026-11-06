# AGENTS.md – Hinweise für KI-Agenten

Dieses Dokument beschreibt das Projekt LeviLuma für KI-Assistenten, die daran arbeiten. Das Repository (Branch `main`) enthält vier unabhängige Firmware-Projekte:

- **Next Controller** (`next/controller/`, ESP32-C5) und **Next Gateway** (`next/gateway/`, XIAO ESP32S3), beide natives ESP-IDF, siehe Abschnitt „LeviLuma Next“.
- **Next Round** (`next/controller-round/`, Waveshare ESP32-S3-Touch-LCD-1.28, ESP-IDF): temporäre Fernbedienung für genau ein Gateway, siehe „LeviLuma Next“.
- **Next Diagnose** (`next/debug/`, XIAO ESP32S3, ESP-IDF): passiver ESP-NOW-Mitschnitt für den seriellen Monitor, siehe „Diagnosegerät“ unten.

Dazu kommt **Next Simulator** (`next/simulator/`, Desktop, CMake + SDL2, keine Firmware): die Original-LVGL-Oberflächen beider Next-Controller am PC, siehe „Desktop-Simulator“ unten.

Die ursprüngliche Firmware („Legacy“: Waveshare ESP32-S3, steuert die Leiste direkt per BLE, PlatformIO/Arduino) liegt eigenständig im Branch [`Legacy`](https://github.com/creepymonster/LeviLuma/tree/Legacy) (Projekt im Root, eigene `AGENTS.md`). Auf `main` gibt es keinen `legacy/`-Ordner mehr. Verweise auf „Legacy“ in diesem Dokument und im Code meinen diesen Branch.

Die READMEs (`README.md`, `next/README.md`) enthalten Nutzerdokumentation, dieses Dokument enthält das, was man für sicheres Arbeiten im Code wissen muss.

Die Kommunikation mit dem Entwickler (Reimar) läuft auf Deutsch. Kommentare, Log-Meldungen und UI-Texte sind ebenfalls deutsch.

## LeviLuma Next

Zwei ESP-IDF-Projekte (ESP-IDF 5.5.6, kein Arduino), gemeinsamer Code in `next/shared/`. Nutzerdoku: `next/README.md`, `next/docs/`.

- **Autoritativer Zustand:** Das Gateway (XIAO) hält den LightState und die Kalibrierung seiner Leiste. Der Controller zeigt nur bestätigte Zustände (STATE-Antworten/-Meldungen). Kein optimistisches Übernehmen von Befehlen; keine zweite Zustandsverwaltung (auch nicht in Zigbee).
- **Protokoll:** nur in `next/shared/protocol/` ändern, beide Seiten nutzen dieselben Dateien. Version (`LVP_VERSION`) bei inkompatiblen Änderungen erhöhen und `next/docs/ESP_NOW_PROTOKOLL.md` nachziehen. Rahmen ≤ 250 Byte, Little Endian, byteweise Serialisierung.
- **Hosttestbare Logik** liegt in `gateway/components/light_core/` und `controller/components/controller_core/` (ohne ESP-IDF-Abhängigkeit, NVS über `KvStore`). Neue Logik dorthin, Plattformcode in `main/`. Tests: `next/tests/run_tests.sh` (muss grün sein); Auswertung siehe „Testauswertung“ unten.
- **Kopien aus Legacy:** `CalibCore.h`, `ColorMath.h` (Gateway) und `Palette.h` (Controller) sind byte-gleiche Kopien der Dateien unter `include/` im Branch `Legacy`; `LotusProtocol.h` nur ohne `<Arduino.h>`. `.github/workflows/tests.yml` prüft die Byte-Gleichheit gegen den aktuellen Stand von `Legacy`. Änderungen an der Kalibrierung in beiden Branches konsistent halten.
- **Nebenläufigkeit Gateway:** `gwLock` für Modell/Kalibrierung/Kopplungen/Handler/gemerktes Band (`BandMemory`); BLE-Task nur mit Kopien; ESP-NOW-Callback kopiert nur in eine Queue.
- **NimBLE im Gateway:** Peripheral-Rolle und GATT-Server bleiben an (`sdkconfig.defaults`, `#error` in `ble_link.cpp`), obwohl das Gateway nur als Central arbeitet. Ohne GATT-Server verwirft NimBLE Anfragen des Bands ohne Antwort, und das Band nimmt nach 30 s keine Befehle mehr an. Erkenntnisse zum Band (Namen, GATT, Pakete, Verhalten): `hardware/led-band/README.md`.
- **Nebenläufigkeit Controller:** `appLock` (rekursiv) für Register und Link; LVGL nur im LVGL-Task; Zigbee-Lock und App-Lock nie gleichzeitig halten.
- **Zigbee:** nur mit `CONFIG_LEVILUMA_ZIGBEE_ENABLED` (Standard aus). Ohne Flag kein Zigbee-Code im Build (`main/CMakeLists.txt` über die Build-Eigenschaft `LEVI_WITH_ZIGBEE`). Endpoint-IDs sind stabil und persistent (`ZbMap`); nie umnummerieren.
- **Sicherheit:** nur ESP-NOW-Bordmittel (PMK/LMK, CCMP). Keine eigene Kryptografie, Schlüssel nie übertragen.
- **Abhängigkeiten des Controllers** (LVGL 9.6.0, lvgl_theme_shadcn 0.3.1, esp-zigbee-lib) werden per Git nach `next/controller/third_party/` geholt (`cmake/third_party.cmake`), nicht committen.
- **Hardwaredaten** (Pins, Treiber) nur mit Quelle ändern; Quellen stehen in `next/README.md`. Next ist **nicht am Gerät getestet**; keine Hardwaretests behaupten.
- **Oberfläche (beide Controller):** Musikmodus ohne Bedienelement, bis er am Band getestet ist (Protokollbefehle bleiben). Statussymbole nur ESP-NOW-Verbindung (Gateway-Symbol) und Licht an/aus (Glühbirne). RockBase: Ein/Aus nur auf der Übersicht.
- **Round Controller** (`next/controller-round/`): nur ESP-NOW. Kein BLE, keine WLAN-Verbindung/-Suche/AP, kein `esp_netif`, kein Zigbee/HTTP/MQTT; `set(COMPONENTS main)` hält ungenutzte Komponenten aus dem Build, `next/tools/check_round_build.py` prüft das. Display/Touch über LovyanGFX mit der `LGFX_Board`-Konfiguration aus Legacy (Branch `Legacy`, `include/LGFX_Board.h`); Werte nur aus Legacy übernehmen. Genau ein Gateway (`Round::mayPair`). Nutzt `controller_core` aus `next/controller/` – Änderungen dort wirken auf beide Controller. **WLAN-Sendeleistung** (`LEVILUMA_WIFI_TX_POWER`, Standard 8,5 dBm wie Legacy `LEVI_WIFI_TX_POWER`) wird nur hier begrenzt: Das Waveshare-Board hat sonst Funkprobleme (am Gerät bestätigt, ESP-NOW-Kopplung ging erst mit Begrenzung). Board-spezifisch; nicht auf den RockBase-C5 oder das Gateway übertragen, dort gilt der ESP-IDF-Standard.
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
- Touch-Drehung im Round: `LEVILUMA_TOUCH_OFFSET_ROTATION` am Gerät bestimmen (Wert für 270° auch in Legacy unbestimmt).
- Musikmodus und Mikrofon-EQ sind nicht am Gerät bestätigt.
- Offene Punkte der ursprünglichen Firmware (Waveshare C6 2.8, Farbauswahl als Wabe) stehen in der `AGENTS.md` des Branches `Legacy`.
