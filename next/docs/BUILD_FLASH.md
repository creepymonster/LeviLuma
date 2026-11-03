# Bauen und Flashen

## Voraussetzungen

| Projekt | Werkzeug | Version |
|---|---|---|
| Legacy | PlatformIO, Plattform `espressif32@6.10.0` (Arduino 2.0.17) | wie `legacy/platformio.ini` |
| Gateway, Controller | ESP-IDF | **v5.5.6** (getestet; ESP32-C5 wird ab 5.5 offiziell unterstützt) |
| Host-Tests | gcc/g++, CMake ≥ 3.16 | – |

Zigbee nutzt esp-zigbee-lib 2.0.5 (Commit `e2fdf4a` von `espressif/esp-zigbee-sdk`), empfohlen mit ESP-IDF 5.5.x.

## VS Code

1. `LeviLuma.code-workspace` im Repository-Wurzelordner öffnen („Datei → Arbeitsbereich aus Datei öffnen“).
2. Empfohlene Erweiterungen installieren: **PlatformIO IDE** und **ESP-IDF** (Espressif).
3. **Legacy:** PlatformIO erkennt `legacy/platformio.ini`; Build/Upload/Monitor über die PlatformIO-Leiste. Vorher `legacy/include/Config.h` aus der Vorlage anlegen.
4. **Next:** Die ESP-IDF-Erweiterung einmal einrichten („ESP-IDF: Configure ESP-IDF Extension“, Version 5.5.6). Dann im Workspace-Ordner *Next – XIAO S3* bzw. *Next – RockBase C5* über die Statusleiste Ziel (`esp32s3` / `esp32c5`), Port und Flash-Methode (UART) wählen und *Build*, *Flash*, *Monitor* ausführen. `menuconfig` öffnet die Projekteinstellungen (Menüs „LeviLuma …“).

Für den Zigbee-Build in VS Code: in den Ordnereinstellungen des Controllers `idf.sdkconfigDefaults` auf `["sdkconfig.defaults","sdkconfig.defaults.zigbee"]` setzen und einen eigenen Build-Ordner wählen – oder die Kommandozeile unten nutzen.

## Kommandozeile

```sh
. $IDF_PATH/export.sh

# Gateway (XIAO ESP32S3)
cd next/gateway
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor

# Controller ohne Zigbee (Standard)
cd next/controller
idf.py set-target esp32c5
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor     # CH340-Port des Boards

# Controller mit Zigbee (eigener Build-Ordner und eigene sdkconfig)
idf.py -B build-zigbee -D SDKCONFIG=build-zigbee/sdkconfig \
       -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.zigbee" set-target esp32c5 build
idf.py -B build-zigbee -D SDKCONFIG=build-zigbee/sdkconfig -p /dev/ttyUSB0 flash monitor

# Host-Tests
next/tests/run_tests.sh
```

Beim ersten CMake-Lauf des Controllers werden LVGL v9.6.0 und lvgl_theme_shadcn v0.3.1 (und mit Zigbee die esp-zigbee-lib) per Git nach `next/controller/third_party/` geholt (nicht im Repository). Vorab geht das mit `next/tools/fetch_deps.sh [--zigbee]`. Der ESP Component Manager ist für den Controller abgeschaltet, damit keine Registry nötig ist.

## Einstellungen (menuconfig)

- **LeviLuma ESP-NOW** (beide Projekte, müssen übereinstimmen): Kanal (Standard 1), PMK (16 Zeichen), Beacon-Abstand, Wartezeit, Sendungen.
- **LeviLuma Gateway:** Name, individueller Schlüssel (32 Hex-Zeichen, leer = keiner), Kopplungsfenster, Taste/LED-GPIOs, Helligkeitsbereich, Mikrofon-EQ, Überblendung, Standard-Kalibrierung.
- **LeviLuma Controller:** Name, Zigbee an/aus, Display- und Touch-Pins/Takt, Ausrichtung, Abschaltzeiten.

## Flash-Layout

- Controller: `partitions.csv` – NVS 128 KB, App 4 MB (Build ohne Zigbee ca. 1,6 MB, mit Zigbee ca. 1,9 MB), `zb_fct`.
- Gateway: 4-MB-Layout „single app large“.

## Sandbox-Hinweis zu den Builds in diesem Auftrag

In der Ausführungsumgebung waren die PlatformIO- und Espressif-Registries gesperrt. Für den Nachweis wurden ESP-IDF v5.5.6 per Git, die Toolchains von GitHub-Releases und die PlatformIO-Pakete (Plattform 6.10.0, Arduino 2.0.17, Toolchain esp-2021r2-patch5, esptool 4.5.1, SCons 4.11.1, Bibliotheken LovyanGFX 1.2.36, lvgl 9.6.0, lvgl_theme_shadcn 0.3.1, NimBLE-Arduino 1.4.3) manuell aus den Original-Quellen bereitgestellt. Am Repository wurde dafür nichts geändert; auf einem normalen Rechner lädt PlatformIO diese Pakete selbst (bei `^`-Angaben ggf. neuere Patch-Versionen).
