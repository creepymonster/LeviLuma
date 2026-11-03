# LeviLuma

Steuerung von BLE-LED-Leisten der ELK-/Lotus-Lantern-Familie (`ELK-BLEDDM` u. a.) mit eigenem Touch-Display.
Das Repository enthält vier unabhängige Firmware-Projekte:

| Projekt | Ordner | Hardware | Framework | Rolle |
|---|---|---|---|---|
| **Legacy** | [`legacy/`](legacy/) | Waveshare ESP32-S3-Touch-LCD-1.28 (rund) | PlatformIO, Arduino | Bisheriges Gerät: Display + Web-UI, steuert eine Leiste direkt per BLE |
| **Next Controller** | [`next/controller/`](next/controller/) | RockBase NM-CYD-C5 (ESP32-C5, 2,8″) | ESP-IDF 5.5 (nativ) | Zentrale mit LVGL-Oberfläche, steuert Gateways per ESP-NOW, optional Zigbee (FRITZ!Box) |
| **Next Gateway** | [`next/gateway/`](next/gateway/) | Seeed XIAO ESP32S3 | ESP-IDF 5.5 (nativ) | Je Leiste ein Gateway: BLE zur Leiste, ESP-NOW zum Controller, autoritativer LightState und Kalibrierung |
| **Next Round** (temporär) | [`next/controller-round/`](next/controller-round/) | Waveshare ESP32-S3-Touch-LCD-1.28 (rund) | ESP-IDF 5.5 (nativ) | Fernbedienung für genau ein Gateway per ESP-NOW, runde LVGL-Oberfläche; Alternative zum C5-Controller |

Die Oberflächen beider Next-Controller laufen auch am PC: [`next/simulator/`](next/simulator/) (Desktop-Simulator mit SDL2, simuliertes Gateway, kein ESP-IDF nötig).

Gemeinsamer Code von Next: [`next/shared/`](next/shared/) (ESP-NOW-Protokoll, Speicher-Hilfen).
Dokumentation zu Next: [`next/README.md`](next/README.md) und [`next/docs/`](next/docs/).

```
LeviLuma/
├── LeviLuma.code-workspace   VS-Code-Workspace (alle drei Projekte)
├── legacy/                   Waveshare-Projekt (PlatformIO), unverändert verschoben
├── next/
│   ├── controller/           RockBase NM-CYD-C5 (ESP-IDF, LVGL, optional Zigbee)
│   ├── gateway/              XIAO ESP32S3 (ESP-IDF, NimBLE, ESP-NOW)
│   ├── controller-round/     Waveshare 1,28″ rund (ESP-IDF, LVGL, ESP-NOW), temporär
│   ├── shared/protocol/      gemeinsames ESP-NOW-Protokoll
│   ├── shared/common/        NVS-Speicher, Schlüsselformat
│   ├── tests/                Host-Tests (Protokoll, Gateway, Controller)
│   ├── tools/                Abhängigkeiten holen, UI-Vorschau
│   └── docs/                 Protokoll, Kopplung, Kalibrierung, Zigbee, Build/Flash
└── .github/workflows/        CI (Legacy-Build, Legacy-Screenshots, Next-Firmware, Tests)
```

## Schnellstart

**VS Code:** `LeviLuma.code-workspace` öffnen. Empfohlen werden die Erweiterungen *PlatformIO IDE* (für Legacy) und *ESP-IDF* (für Next). Jeder der vier Workspace-Ordner ist ein eigenständiges Projekt.

**Legacy (PlatformIO):**
```sh
cp legacy/include/Config.example.h legacy/include/Config.h   # WLAN-Daten eintragen
pio run -d legacy                 # bauen
pio run -d legacy -t upload       # flashen
```

**Next (ESP-IDF 5.5):**
```sh
. $IDF_PATH/export.sh
cd next/gateway    && idf.py set-target esp32s3 build flash monitor
cd next/controller && idf.py set-target esp32c5 build flash monitor
idf.py -C next/controller-round set-target esp32s3 build      # runder Controller (temporär)
# Controller mit Zigbee:
cd next/controller && idf.py -B build-zigbee -D SDKCONFIG=build-zigbee/sdkconfig \
    -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.zigbee" set-target esp32c5 build
# Host-Tests (ohne Hardware):
next/tests/run_tests.sh
```

Details: [`next/docs/BUILD_FLASH.md`](next/docs/BUILD_FLASH.md).

## Legacy-Migration

Das Waveshare-Projekt wurde vollständig per `git mv` nach `legacy/` verschoben (`platformio.ini`, `src/`, `include/`, `tools/screenshots/`, `screenshots/`, `README.md`). Die interne Struktur ist unverändert, am Code wurde nichts geändert.
Nötige Pfadanpassungen außerhalb von `legacy/`:

- `.gitignore`: `include/Config.h` → `legacy/include/Config.h` (die echte Konfiguration mit WLAN-Daten bleibt ausgeschlossen).
- `.github/workflows/build.yml`: Pfadfilter auf `legacy/…`, Build mit `pio run -d legacy`, Config-Vorlage aus `legacy/include/`.
- `.github/workflows/screenshots.yml`: Pfadfilter und Aufrufe auf `legacy/…`, Ausgabe nach `legacy/screenshots/`.

Die `platformio.ini` selbst ist unverändert (`-I include` ist relativ zum Projektordner).

## Hinweise

- Die Legacy-Web-API hat keine Authentifizierung (nur vertrauenswürdige Netze). Next hat keine Weboberfläche.
- Was am Gerät getestet ist und was nicht, steht in `legacy/README.md` und `next/README.md` (Abschnitt „Teststand“).
