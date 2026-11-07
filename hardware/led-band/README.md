# LED-Bänder

Alles, was das Projekt über die BLE-LED-Leisten weiß, die Legacy und das Next-Gateway steuern. Jede Angabe nennt ihre Herkunft. Legacy liegt im Branch [`legacy`](https://github.com/creepymonster/LeviLuma/tree/legacy); Legacy-Pfade unten sind relativ zu dessen Root.

## Bandfamilien

Das Next-Gateway kann mehrere Familien von BLE-Bändern ansteuern. Legacy kennt nur ELK.

| Familie | Namen (Beispiele) | Schreibziel (Service / Characteristic) | Im Gateway genutzt | Stand | Befehle |
|---|---|---|---|---|---|
| **ELK / Lotus Lantern** | `ELK-BLEDDM`, `ELK-BLEDOM`, `ELK-BLEDOB`, `MELK`, `LEDBLE` | `FFF0` / `FFF3` | Ein/Aus, Farbe, Helligkeit, Musik, Mikrofon-EQ | Ein/Aus, Farbe, Helligkeit **am Gerät getestet (Legacy)** | dieses Dokument, unten |
| **Triones / HappyLighting** | `Triones`, `LEDBlue`, `LEDBLE`, `Dream~`, `QHM-` | `FFD5` / `FFD9` oder `FFE5` / `FFE9` | Ein/Aus, Farbe (Helligkeit eingerechnet) | **ungetestet** | [triones.md](triones.md) |
| **Zengge / LEDnetWF** | `LEDnetWF…`, `IOTWF…` | `FFFF` / `FF01` | Ein/Aus, Farbe (Helligkeit im Farbpaket) | **ungetestet** | [zengge.md](zengge.md) |
| **BJ_LED (MohuanLED)** | `BJ_LED` | beliebiger Service / `EE01` | Ein/Aus, Farbe (Helligkeit eingerechnet) | **ungetestet** | [bjled.md](bjled.md) |

### Wie das Gateway die Familie bestimmt

1. **Scan:** Kandidaten sind Geräte, deren Name zu einer Familie passt (`BandFam::fromName`). Ohne gemerktes Band wird das stärkste davon gewählt, mit gemerktem Band nur dieses (`BandMemory`, unverändert). Der Name ist nur ein **Hinweis**: `LEDBLE` z. B. nutzen ELK- und Triones-Geräte.
2. **Nach dem Verbinden** liest das Gateway alle Services und Characteristics und sucht das erste bekannte, beschreibbare Schreibziel (`BandFam::matchGatt`). Die Profile der Namensfamilie werden zuerst geprüft, danach die übrigen. **Die GATT-Struktur entscheidet.** Wird nichts gefunden, trennt das Gateway und versucht es nach 4 s erneut.
3. **Zengge** braucht zusätzlich Produkt-ID und BLE-Version aus den Herstellerdaten der Werbung (ersatzweise aus dem langen Namen), um das richtige Farbpaket zu wählen. Nicht unterstützte Zengge-Varianten (IOTBT, nur Weiß, Dimmer, Schalter) werden nach dem Erkennen getrennt.
4. Die Familie wird **nicht gespeichert**, sondern bei jeder Verbindung neu bestimmt. Gemerkt wird wie bisher nur die Adresse. Konsole `band` zeigt die Familie der letzten Verbindung, das Log die gefundene Familie und alle Characteristics (Debug-Stufe).

### Aufbau im Code

| Teil | Datei | Aufgabe |
|---|---|---|
| Erkennung | `gateway/components/light_core/include/band_family.h` | Namen, Profile (Service/Characteristic), GATT-Abgleich, Zengge-Herstellerdaten. Hosttestbar. |
| Paketbau | `gateway/components/light_core/include/band_driver.h` | Ein Paketbauer für alle Familien (`BandDriver`), Fähigkeiten je Familie (`BandCaps`: eigenes Helligkeitspaket, Musik, Mikrofon-EQ). Hosttestbar. |
| Reihenfolge | `gateway/components/light_core/src/ble_scheduler.cpp` | Unverändert: Musik, Mikrofon-EQ, Helligkeit, Ein/Aus, Farbe. Ohne eigenes Helligkeitspaket sendet eine Helligkeitsänderung die Farbe neu; Musik/EQ werden bei Familien ohne diese Funktion übergangen. |
| Funk | `gateway/main/ble_link.cpp` | Scan (mit Herstellerdaten), GATT-Suche, Schreiben mit variabler Paketlänge. |
| Tests | `tests/test_gateway.cpp` | Namen, GATT-Abgleich, Zengge-Angaben, Pakete aller Familien (Erwartungswerte aus den Quellen), Scheduler mit eingerechneter Helligkeit. |

**Neue Familie hinzufügen:** Namen und Profil in `band_family.cpp`, Pakete in `band_driver.cpp` (`power`, `color`, ggf. `brightness`/`music`/`micEq` und `caps`), Tests in `tests/test_gateway.cpp`, Doku hier als eigene Datei. Die Kalibrierung gilt für alle Familien gleich (auf die Farbe vor dem Paketbau).

**Neue Funktion (z. B. Effekte):** Die Befehle aller Familien stehen in den Familiendateien. Dafür braucht es zuerst eine Erweiterung des ESP-NOW-Protokolls (`shared/protocol/`) und des LightState; erst dann ein weiterer Schritt im `BleScheduler` und eine Methode im `BandDriver`.

# ELK / Lotus Lantern

Bisher einzige Familie, die Legacy und Next am Gerät gesteuert haben. Kennzeichnung der Angaben:

- **„am Gerät getestet (Legacy)“**: im Legacy-Projekt am Band ausprobiert (laut `README.md` und `include/LotusProtocol.h` im Branch `legacy`).
- **„am Gerät beobachtet“**: in Logs des Entwicklers mit dem Band `ELK-BLEDDM 96` gesehen. Das ist kein systematischer Test aller Bänder der Familie.
- **„ungetestet“**: nur aus Vorlagen oder der Original-App übernommen.

## Erkennung

| Merkmal | Wert | Herkunft |
|---|---|---|
| Gerätenamen | beginnen mit `ELK-BLEDDM`, `ELK-BLEDOM`, `ELK-BLEDOB`, `MELK` oder `LEDBLE` | Legacy `bleNameMatches()` (`src/ble.cpp`), Next `BandMemory::nameMatches()` |
| Beispiel | `ELK-BLEDDM 96`, Adresse `be:68:78:27:cf:02` (öffentlich) | am Gerät beobachtet |
| Name in der Werbung | oft erst in der Scan-Antwort, daher aktiver Scan | Legacy und Next scannen aktiv; ohne Antenne fehlten am XIAO die Namen |
| Sichtbarkeit | Solange ein Gerät verbunden ist, wirbt das Band nicht (verschwindet z. B. aus LightBlue) | am Gerät beobachtet |
| Verbindungen | nur eine gleichzeitig | am Gerät beobachtet |

## GATT

| Merkmal | Wert | Herkunft |
|---|---|---|
| Service | `0xFFF0` | Legacy, Next |
| Characteristic | `0xFFF3`, Handle 3 | am Gerät beobachtet (Next-Log) |
| Eigenschaften von `0xFFF3` | `0x0E`: Read, Write, Write without Response | am Gerät beobachtet (Next-Log) |
| Schreibart | Legacy schreibt **ohne Antwort**. Das Next-Gateway schreibt standardmäßig **mit Bestätigung** („Write“, Bestätigung nach ca. 60–350 ms), mit `ble rsp off` ohne Antwort wie Legacy | am Gerät beobachtet (Next-Gateway mit Bestätigung, über mehr als zwei Minuten ohne Ausfall) |
| Wartezeit | etwa 1 s nach dem Verbinden, bevor die Dienste gelesen werden | Legacy `ble.cpp`, in Next übernommen |

## Befehle (Hex)

9-Byte-Pakete, Start `7E`, Ende `EF`. Quelle: Legacy `include/LotusProtocol.h`; die Next-Kopie liegt in `gateway/components/light_core/include/LotusProtocol.h`.

| Befehl | Paket | Stand |
|---|---|---|
| Ein | `7E0404F00001FF00EF` (wie die Original-App) | am Gerät getestet (Legacy) |
| Aus | `7E0404000000FF00EF` | am Gerät getestet (Legacy) |
| Farbe R, G, B | `7E070503` **RR GG BB** `10EF` (`10` wie in der dekompilierten Original-App) | am Gerät getestet (Legacy) |
| Helligkeit | `7E0401` **BB** `FFFFFF00EF` (BB = 0–100 hex, z. B. `64` = 100 %, `32` = 50 %) | am Gerät getestet (Legacy), natives Helligkeitspaket |
| Musikmodus an | `7E040701FFFFFF00EF` | **ungetestet** |
| Musikmodus aus | `7E040700FFFFFF00EF` | **ungetestet** (wird trotzdem nach jedem Verbinden gesendet, siehe unten) |
| Mikrofon-EQ | `7E0503` **MM** `04FFFF00EF` (MM = Modus \| `0x80`: `80` normal, `81` weich, `82` dynamisch, `83` disko) | **ungetestet** |

Beispiele zum Abtippen:

| Farbe | Paket |
|---|---|
| Rot 255,0,0 | `7E070503FF000010EF` |
| Grün 0,255,0 | `7E07050300FF0010EF` |
| Blau 0,0,255 | `7E0705030000FF10EF` |
| Weiß 255,255,255 | `7E070503FFFFFF10EF` |

### Weitere Befehle (nicht genutzt, ungetestet)

Aus dem Rust-Projekt [TheSylex/ELK-BLEDOM-bluetooth-led-strip-controller](https://github.com/TheSylex/ELK-BLEDOM-bluetooth-led-strip-controller) (`src/device.rs`, keine Lizenzdatei; nur Fakten übernommen). Dort ist **Byte 1 immer `00`**, bei uns `04`/`05`/`07` wie in der Original-App. Ob das Band Byte 1 auswertet, ist nicht geprüft.

| Befehl | Paket | Hinweis |
|---|---|---|
| Ein / Aus | `7E0004F00001FF00EF` / `7E0004000000FF00EF` | wie unsere Pakete, nur Byte 1 = `00` |
| Farbe | `7E000503` **RR GG BB** `00EF` | Byte 7 = `00` statt `10` |
| Helligkeit | `7E0001` **BB** `00000000EF` (0–100) | Füllbytes `00` statt `FF` |
| Effekt | `7E0003` **EE** `03000000EF` | EE siehe Tabelle unten |
| Effektgeschwindigkeit | `7E0002` **SS** `00000000EF` (0–100) | |
| Uhrzeit setzen | `7E0083` **hh mm ss WT** `00EF` | WT = Wochentag 1–7, Montag = 1 |
| Zeitplan Ein | `7E0082` **hh mm** `0000` **TT** `EF` | TT = Wochentage als Bitmaske (Mo `01`, Di `02`, Mi `04`, Do `08`, Fr `10`, Sa `20`, So `40`), `+80` = aktiv |
| Zeitplan Aus | `7E0082` **hh mm** `0001` **TT** `EF` | wie oben |

Befehl `03` wählt einen Modus, Byte 4 den Typ: `03` = eingebauter Effekt (oben), `04` = Mikrofon-EQ (unser `Lotus::micEq`, Byte 3 = `80`–`83`). Das ist aus dem Vergleich beider Quellen geschlossen, nicht am Band geprüft.

| EE | Effekt | EE | Effekt |
|---|---|---|---|
| `87` | Springen Rot/Grün/Blau | `92` | Überblenden Rot/Grün |
| `88` | Springen sieben Farben | `93` | Überblenden Rot/Blau |
| `89` | Überblenden Rot/Grün/Blau | `94` | Überblenden Grün/Blau |
| `8A` | Überblenden sieben Farben | `95` | Blinken sieben Farben |
| `8B` | Überblenden Rot | `96` | Blinken Rot |
| `8C` | Überblenden Grün | `97` | Blinken Grün |
| `8D` | Überblenden Blau | `98` | Blinken Blau |
| `8E` | Überblenden Gelb | `99` | Blinken Gelb |
| `8F` | Überblenden Cyan | `9A` | Blinken Cyan |
| `90` | Überblenden Magenta | `9B` | Blinken Magenta |
| `91` | Überblenden Weiß | `9C` | Blinken Weiß |

Die Firmware rechnet Farben vor dem Senden mit der Kalibrierung um (`CalibCore.h`). Das Gateway-Log (`BLE: Farbe … Paket …`) zeigt deshalb das tatsächlich gesendete Paket, nicht die gewählte Farbe.

**Mit LightBlue senden:** Gateway bzw. Legacy-Gerät vom Strom nehmen (sonst ist das Band belegt), mit dem Band verbinden, Service `FFF0` → Characteristic `FFF3` → Hex ohne Leerzeichen, „Write without response“.

## Ansteuerung durch die Firmware

Gilt für Legacy (`src/ble.cpp`) und Next (`BleScheduler`, `gateway/components/light_core/`).
Im Next-Gateway gilt das für alle Familien. Abweichungen bei Familien ohne eigenes Helligkeitspaket oder ohne Musikmodus: siehe [Aufbau im Code](#aufbau-im-code).

- **Ein Befehl pro Zeitfenster**, nach jedem gesendeten Befehl 200 ms Pause. Helligkeit und Farbe nie im selben Schritt.
- **Reihenfolge**: Musikmodus, Mikrofon-EQ, Helligkeit, Ein/Aus, Farbe.
- **Nach jedem Verbinden**: „Musik aus“ erzwingen (Modus des Bands unbekannt), dann Helligkeit und den übrigen Zustand neu senden.
- **Ein/Aus nach dem Start unbekannt.** Ein Paket geht erst nach einem Befehl raus. **Farben werden nur gesendet, wenn das Licht als „an“ gilt.**
- **Rohe Farben** (Kalibrierassistent) höchstens alle 400 ms.
- **Überblendung**: `LEVI_FADE_STEPS` bzw. `CONFIG_LEVILUMA_FADE_STEPS`, Standard 1 (springt direkt).
- **Keine Ausführungsbestätigung.** `sentRevision` heißt nur „gesendet“, nicht „vom Band ausgeführt“.

## Einstellungen

| Zweck | Legacy (`Config.h`) | Next-Gateway (Kconfig) | Standard |
|---|---|---|---|
| Helligkeitsbereich des Bands | `LEVI_BRIGHTNESS_MAX` | `CONFIG_LEVILUMA_BRIGHTNESS_MAX` | 100 (Werte 0–100). 255 = Prozent auf 0–255 umgerechnet (Python-Vorlage) |
| Mikrofon-EQ-Modus | `LEVI_MIC_EQ_MODE` | `CONFIG_LEVILUMA_MIC_EQ_MODE` | 0 |
| Überblendung | `LEVI_FADE_STEPS` | `CONFIG_LEVILUMA_FADE_STEPS` | 1 |
| Kalibrierung (Kanal-Gains) | `LEVI_CALIB_GR/GG/GB` | `CONFIG_LEVILUMA_CALIB_GAIN_R/G/B` (Promille) | 1,000 / 0,690 / 0,940 (FastLED TypicalSMD5050) |
| Kalibrierung (10 Zielfarben) | `LEVI_CALIB_POINTS` | `CONFIG_LEVILUMA_CALIB_POINTS` | leer (Gains verwenden) |

Details zur Kalibrierung: `docs/KALIBRIERUNG.md`.

## Verbindungsverhalten

- **Parameterwunsch:** Etwa 1,5 s nach dem Verbinden verlangt das Band per Link-Layer-Verfahren Intervall 22,5–47,5 ms, Latenz 0, Supervision-Timeout 1 s. Legacy (NimBLE-Arduino) und Next antworten mit eigenen Werten (30–50 ms, Timeout 2,56 s). Herkunft: Next-Log, NimBLE-Arduino 1.4.3 `NimBLEClient.cpp`.
- **MTU:** Legacy handelt 255 aus, Next 256. Das Band stellt nach dem Verbinden **selbst** eine MTU-Anfrage (im Legacy-Log zweites `mtu update event`, im Next-Log mit GATT-Server zweite Zeile `BLE: MTU 256`).
- **Anfragen des Bands müssen beantwortet werden.** Bekommt das Band auf seine Anfrage keine Antwort, nimmt es nach dem ATT-Timeout von **30 s** keine Befehle mehr an. Die Funkverbindung bleibt dabei bestehen (RSSI lesbar, Band weiter unsichtbar).
  - Am Gerät beobachtet mit dem Next-Gateway, solange dessen NimBLE ohne GATT-Server gebaut war (`CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=n`).
  - Ohne GATT-Server verwirft NimBLE solche Anfragen stumm (`ble_att.c`).
  - Abhilfe im Gateway: Peripheral-Rolle und GATT-Server an (`gateway/sdkconfig.defaults`). **Am Gerät bestätigt** (Next-Gateway, Stand `f1df579`): Das Gateway beantwortet die MTU-Anfrage des Bands, alle Schreibvorgänge mit Bestätigung wurden über mehr als zwei Minuten Verbindung bestätigt (je etwa 70–350 ms), kein Hängen nach 30 s, keine Trennung.
  - Legacy hat den GATT-Server eingebaut und hielt die Verbindung am Gerät über 40 s ohne Ausfall.
- **Pairing:** Das Band verlangt keine Kopplung (kein Pairing-Ereignis im Next-Log).
- **Verbindungsaufbau bei schwachem Signal:** Bei −97 dBm schlug ein Verbindungsversuch mit Timeout fehl (`Status 13`), der nächste Versuch nach 4 s bei −87 dBm klappte (am Gerät beobachtet).
- **Wiederverbinden:** Legacy und Next versuchen es alle 4 s neu. Das Next-Gateway merkt sich nach der ersten erfolgreichen Verbindung die Adresse des Bands und verbindet danach nur noch dieses (`BandMemory`, Konsole `band forget`).

## Funk

- Der XIAO ESP32S3 (Next-Gateway) braucht seine externe Antenne. Ohne sie empfing er am Gerät rund 25 dB schwächer als ein MacBook am selben Ort, und das Band wurde nicht gefunden.
- Empfangspegel am Gerät beobachtet: im selben Raum mit Antenne etwa −70 dBm, durch zwei Türen und rund 6 m etwa −87 dBm (Verbindung klappte noch).

## Codestellen

| Was | Legacy (Branch `legacy`) | Next-Gateway |
|---|---|---|
| Pakete | `include/LotusProtocol.h` | `gateway/components/light_core/include/LotusProtocol.h` |
| Senden, Reihenfolge, Pausen | `src/ble.cpp` (`bleTask`) | `light_core/src/ble_scheduler.cpp`, `gateway/main/ble_link.cpp` |
| Bandauswahl | `bleNameMatches()` | `light_core/src/band_memory.cpp`, Namen in `light_core/src/band_family.cpp` |
| Familienerkennung, Paketbau | – (nur ELK) | `light_core/src/band_family.cpp`, `light_core/src/band_driver.cpp` |
| Diagnose | – | Gateway-Konsole: `status`, `band` (mit Familie der letzten Verbindung), `band forget`, `ble rsp on/off`, `ble off/on`; Log mit Hex-Paketen |
