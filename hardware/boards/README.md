# 3D-Modelle der LeviLuma-Boards

Unveränderte STEP-Dateien der drei im Projekt verwendeten Board-Modelle.
Die Dateiendung `.stp` des Waveshare-Originals wurde lediglich zu `.step`
vereinheitlicht. Die PNGs wurden aus den jeweiligen STEP-Geometrien
erzeugt und zeigen zwei gegenüberliegende Schrägansichten.

| Board | Verwendung | STEP | Rendering |
|---|---|---|---|
| Waveshare ESP32-S3-Touch-LCD-1.28 | Legacy und Next Round | [STEP](waveshare-esp32-s3-touch-lcd-1.28.step) | [PNG](waveshare-esp32-s3-touch-lcd-1.28.png) |
| Seeed Studio XIAO ESP32S3 | Next Gateway | [STEP](seeed-xiao-esp32s3.step) | [PNG](seeed-xiao-esp32s3.png) |
| RockBase NM-CYD-C5 | Next Controller | [STEP](rockbase-nm-cyd-c5.step) | [PNG](rockbase-nm-cyd-c5.png) |

Das nur geplante Waveshare ESP32-C6-Touch-LCD-2.8 gehört nicht zu dieser
Sammlung. XIAO Sense und Plus sind ebenfalls andere Hardwarevarianten.

## Quellen und Modellstand

Abruf: **3. November 2026**. Die Zuordnung zu den verwendeten Boards stammt aus
der [Projektübersicht](../../README.md) und [Next-Dokumentation](../../next/README.md).

- **Waveshare:** [Hersteller-Ressourcen](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.28/Resources-And-Documents),
  [3D-Archiv](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.28/ESP32-S3-Touch-LCD-1.28.zip).
  Original: `ESP32-S3-Touch-LCD-1.28/ESP32-S3-Touch-LCD-1_28.stp`.
  Eine konkrete Platinenrevision ist im Dateinamen nicht angegeben.
- **Seeed:** [Hersteller-Wiki](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/),
  [3D-Archiv](https://files.seeedstudio.com/wiki/SeeedStudio-XIAO-ESP32S3/res/seeed-studio-xiao-esp32s3-3d_model.zip).
  Original: `XIAO-ESP32S3 v2.step`. Standard-Board ohne Sense-Erweiterung und
  ohne externe Antenne; „v2“ bezeichnet den Dateistand, keine bestätigte PCB-Revision.
- **RockBase:** [Hardware-Repository](https://github.com/Yverr0y/NM-CYD-C5/tree/a1366c3d63363019371dac476a03a01b821ec5de/Documention/2-sch),
  [3D-Archiv](https://github.com/Yverr0y/NM-CYD-C5/blob/a1366c3d63363019371dac476a03a01b821ec5de/Documention/2-sch/3D_NM-CYD-C5-3D-step.zip).
  Original: `3D_NM-CYD-C5-3D.step`. Quelle auf Commit
  `a1366c3d63363019371dac476a03a01b821ec5de` fixiert.
  Das veröffentlichte Modell enthält die bestückte Platine **ohne LCD-Modul**.
  Der Platzbedarf des Displays lässt sich damit allein nicht bestimmen.

Die STEP-Dateien sind bytegleich mit den aus den Archiven entnommenen Originalen
(SHA-256, Prüfung mit `sha256sum <datei>`):

| Datei | SHA-256 |
|---|---|
| `waveshare-esp32-s3-touch-lcd-1.28.step` | `4a2a3a950d1c7bac0749a60d4804bb508580d51a89cd72b4953a6c51451d0e58` |
| `seeed-xiao-esp32s3.step` | `870ad45c7af5d92324cec4c6ae65fbf86e3b049d5a97ab9ffd9039ebd73455dd` |
| `rockbase-nm-cyd-c5.step` | `12348eee80ee7a9d248f80c20072b22a02bc9782169a676e4c54345e99de94cc` |

Die Modelle wurden nicht gegen die tatsächlich vorhandenen Platinenrevisionen
vermessen; für passgenaue Gehäuse insbesondere Steckverbinder, Display und
Montagepunkte am eigenen Board prüfen.

Die Modelle stammen von den jeweiligen Anbietern; deren Rechte bleiben bestehen.
In den drei heruntergeladenen Archiven war keine separate Lizenzdatei enthalten.
Eine darüber hinausgehende Nutzungslizenz wird hier nicht behauptet.

## Vorschau

### Waveshare ESP32-S3-Touch-LCD-1.28

![Waveshare, zwei STEP-Ansichten](waveshare-esp32-s3-touch-lcd-1.28.png)

### Seeed Studio XIAO ESP32S3

![XIAO, zwei STEP-Ansichten](seeed-xiao-esp32s3.png)

### RockBase NM-CYD-C5

![RockBase, zwei STEP-Ansichten](rockbase-nm-cyd-c5.png)
