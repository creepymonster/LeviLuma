# 3D-Modell des LeviLuma-Boards

Unveränderte STEP-Datei des Waveshare ESP32-S3-Touch-LCD-1.28. Die Dateiendung `.stp` des Originals wurde lediglich zu `.step` vereinheitlicht. Das PNG wurde aus der STEP-Geometrie erzeugt und zeigt zwei gegenüberliegende Schrägansichten.

| Board | STEP | Rendering |
|---|---|---|
| Waveshare ESP32-S3-Touch-LCD-1.28 | [STEP](waveshare-esp32-s3-touch-lcd-1.28.step) | [PNG](waveshare-esp32-s3-touch-lcd-1.28.png) |

Das nur geplante Waveshare ESP32-C6-Touch-LCD-2.8 gehört nicht zu dieser Sammlung.

## Hardware im Überblick

Die Angaben stammen aus der Projektkonfiguration (`include/LGFX_Board.h`) und den verlinkten Herstellerseiten. Nicht am Gerät nachgemessen.

| Merkmal | Wert |
|---|---|
| MCU | ESP32-S3, Dual-Core, WLAN und BLE |
| Flash | 16 MB (per `esptool flash_id` am Gerät bestätigt) |
| Display | GC9A01, 240×240, rund, SPI (LovyanGFX `Panel_GC9A01`) |
| Display-Pins | SCLK 10, MOSI 11, DC 8, CS 9, RST 14, kein MISO |
| Hintergrundbeleuchtung | PWM über GPIO 2 |
| Touch | CST816S, kapazitiv, I2C: SDA 6, SCL 7, RESET 13, Adresse 0x15, 400 kHz, kein Interrupt |
| Besonderheit | Der CST816S braucht die verdrahtete Reset-Leitung, sonst antwortet er nicht |
| Farbformat | RGB565, Byte-Reihenfolge per `LEVI_RGB565_SWAP` |

**LED-Leiste:** ELK-/Lotus-Lantern-Familie, BLE, Namen `ELK-BLEDDM`, `ELK-BLEDOM`, `ELK-BLEDOB`, `MELK`, `LEDBLE`. Service `0xFFF0`, Schreib-Characteristic `0xFFF3`, 9-Byte-Pakete (Start `0x7E`, Ende `0xEF`).

## Weiterführende Ressourcen

- [Hersteller-Ressourcen und Dokumente](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.28/Resources-And-Documents) (Schaltplan, Demos, 3D-Modell)
- [3D-Archiv](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.28/ESP32-S3-Touch-LCD-1.28.zip)
- [ESP32-S3 Datenblatt und Dokumente](https://www.espressif.com/en/support/documents/technical-documents?keys=ESP32-S3)
- [LovyanGFX](https://github.com/lovyan03/LovyanGFX), [LVGL](https://lvgl.io/), [lvgl_theme_shadcn](https://github.com/AlixANNERAUD/lvgl_theme_shadcn)
- [Projektübersicht](../../README.md)

## Quellen und Modellstand

Abruf: **3. November 2026**.

- **Waveshare:** [Hersteller-Ressourcen](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.28/Resources-And-Documents),
  [3D-Archiv](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.28/ESP32-S3-Touch-LCD-1.28.zip).
  Original: `ESP32-S3-Touch-LCD-1.28/ESP32-S3-Touch-LCD-1_28.stp`.
  Eine konkrete Platinenrevision ist im Dateinamen nicht angegeben.

Die STEP-Datei ist bytegleich mit dem aus dem Archiv entnommenen Original (SHA-256, Prüfung mit `sha256sum <datei>`):

| Datei | SHA-256 |
|---|---|
| `waveshare-esp32-s3-touch-lcd-1.28.step` | `4a2a3a950d1c7bac0749a60d4804bb508580d51a89cd72b4953a6c51451d0e58` |

Das Modell wurde nicht gegen die tatsächlich vorhandene Platinenrevision vermessen; für passgenaue Gehäuse insbesondere Steckverbinder, Display und Montagepunkte am eigenen Board prüfen.

Das Modell stammt vom Anbieter; dessen Rechte bleiben bestehen. Im heruntergeladenen Archiv war keine separate Lizenzdatei enthalten. Eine darüber hinausgehende Nutzungslizenz wird hier nicht behauptet.

## Vorschau

![Waveshare, zwei STEP-Ansichten](waveshare-esp32-s3-touch-lcd-1.28.png)
