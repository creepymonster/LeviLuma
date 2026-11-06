# LeviLuma

Steuerung von BLE-LED-Leisten der ELK-/Lotus-Lantern-Familie (`ELK-BLEDDM` u. a.): Je Leiste ein Gateway, bedient über einen Controller per ESP-NOW, optional über Zigbee (FRITZ!Box).

Die ursprüngliche LeviLuma-Firmware (Waveshare ESP32-S3-Touch-LCD-1.28, steuert eine Leiste direkt per BLE, mit Web-UI) ist im Branch [`legacy`](https://github.com/creepymonster/LeviLuma/tree/legacy) verfügbar.

Das Repository enthält diese unabhängigen Firmware-Projekte:

| Projekt | Ordner | Hardware | Framework | Rolle |
|---|---|---|---|---|
| **Next Controller** | [`next/controller/`](next/controller/) | RockBase NM-CYD-C5 (ESP32-C5, 2,8″) | ESP-IDF 5.5 (nativ) | Zentrale mit LVGL-Oberfläche, steuert Gateways per ESP-NOW, optional Zigbee (FRITZ!Box) |
| **Next Gateway** | [`next/gateway/`](next/gateway/) | Seeed XIAO ESP32S3 | ESP-IDF 5.5 (nativ) | Je Leiste ein Gateway: BLE zur Leiste, ESP-NOW zum Controller, autoritativer LightState und Kalibrierung |
| **Next Round** (temporär) | [`next/controller-round/`](next/controller-round/) | Waveshare ESP32-S3-Touch-LCD-1.28 (rund) | ESP-IDF 5.5 (nativ) | Fernbedienung für genau ein Gateway per ESP-NOW, runde LVGL-Oberfläche; Alternative zum C5-Controller |
| **Next Diagnose** | [`next/debug/`](next/debug/) | Seeed XIAO ESP32S3 | ESP-IDF 5.5 (nativ) | Passiver ESP-NOW-Mitschnitt für den seriellen Monitor |

Die Oberflächen beider Next-Controller laufen auch am PC: [`next/simulator/`](next/simulator/) (Desktop-Simulator mit SDL2, simuliertes Gateway, kein ESP-IDF nötig).

Gemeinsamer Code von Next: [`next/shared/`](next/shared/) (ESP-NOW-Protokoll, Speicher-Hilfen).
Dokumentation zu Next: [`next/README.md`](next/README.md) und [`next/docs/`](next/docs/).

```
LeviLuma/
├── LeviLuma.code-workspace   VS-Code-Workspace (alle Next-Projekte)
├── next/
│   ├── controller/           RockBase NM-CYD-C5 (ESP-IDF, LVGL, optional Zigbee)
│   ├── gateway/              XIAO ESP32S3 (ESP-IDF, NimBLE, ESP-NOW)
│   ├── controller-round/     Waveshare 1,28″ rund (ESP-IDF, LVGL, ESP-NOW), temporär
│   ├── debug/                XIAO ESP32S3, passiver ESP-NOW-Mitschnitt
│   ├── simulator/            Desktop-Simulator der Controller-Oberflächen (SDL2)
│   ├── shared/protocol/      gemeinsames ESP-NOW-Protokoll
│   ├── shared/common/        NVS-Speicher, Schlüsselformat
│   ├── tests/                Host-Tests (Protokoll, Gateway, Controller)
│   ├── tools/                Abhängigkeiten holen, UI-Vorschau
│   └── docs/                 Protokoll, Kopplung, Kalibrierung, Zigbee, Build/Flash
├── hardware/boards/          3D-Modelle der Boards
└── .github/workflows/        CI (Next-Firmware, Host-Tests)
```

## Schnellstart

**VS Code:** `LeviLuma.code-workspace` öffnen. Empfohlen wird die Erweiterung *ESP-IDF*. Jeder Workspace-Ordner ist ein eigenständiges Projekt.

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
