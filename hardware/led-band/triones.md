# Bandfamilie Triones / HappyLighting

Generische BLE-Controller für LED-Leisten und Birnen, gesteuert über die Apps **HappyLighting**, **Triones** oder **LED BLE**. In Home Assistant deckt sie die eingebaute Integration `led_ble` ab.

**Stand: nicht am Gerät getestet.** Alles hier stammt aus den unten genannten Fremdquellen. Das Next-Gateway nutzt davon nur Ein/Aus und Farbe, die Helligkeit wird in die Farbe eingerechnet (siehe [Umsetzung im Gateway](#umsetzung-im-gateway)).

## Quellen

| Kürzel | Quelle | Lizenz | Inhalt |
|---|---|---|---|
| saberlight | [madhead/saberlight](https://gitlab.com/madhead/saberlight), `protocols/Triones/protocol.md` | nicht angegeben | Protokollbeschreibung (Status, Ein/Aus, Farbe, Weiß, Effekte, Uhr) |
| led-ble | [Bluetooth-Devices/led-ble](https://github.com/Bluetooth-Devices/led-ble) | Apache-2.0 | Bibliothek hinter Home Assistant `led_ble`: Namen, UUIDs, Modelltabelle |
| flux_led | [lightinglibs/flux_led](https://github.com/lightinglibs/flux_led), `protocol.py` (`ProtocolLEDENETOriginal*`) | LGPL-3.0 | Paketaufbau, den `led_ble` verwendet |
| ha-triones | [sysofwan/ha-triones](https://github.com/sysofwan/ha-triones) | MIT | Home-Assistant-Integration (Ein/Aus, Farbe, Weiß, Status) |
| HappyLighting-py | [MikeCoder96/HappyLighting-py](https://github.com/MikeCoder96/HappyLighting-py) | AGPL-3.0 | aus der App-APK nachgebaut (Farbe, Ein/Aus, Effekte, Mikrofon) |

Aus den Quellen sind nur Fakten (Bytefolgen, UUIDs) übernommen, kein Code.

## Erkennung

| Merkmal | Wert | Quelle |
|---|---|---|
| Gerätenamen | beginnen mit `Triones`, `LEDBlue`, `LEDBLE`, `Dream~`, `QHM-` | led-ble README und Modelltabelle (`Triones:C10511000166`, `Dream~MAC`, `LEDBlue-C2AF4BD5`, `LEDBLE-DE1254F9`, `QHM-095F`), ha-triones |
| Achtung `LEDBLE` | Den Namen nutzen auch ELK-Bänder (Legacy-Namensliste) und das Fremdprojekt [8none1/ledble-ledlamp](https://github.com/8none1/ledble-ledlamp) (`LEDBLE-01`, 7E-Pakete an `FFE1`). Der Name allein entscheidet deshalb nicht. | |
| Modellnummer | Byte 1 der Statusantwort (`0x04`, `0x10`, `0x15`, `0x54`, `0xE3` …) | led-ble `model_db.py` |

## GATT

| Zweck | Service | Characteristic | Quelle |
|---|---|---|---|
| Schreiben | `FFD5` | `FFD9` | saberlight |
| Schreiben (andere Geräte) | `FFE5` | `FFE9` | ha-triones, led-ble |
| Benachrichtigung (Status) | `FFD0` | `FFD4` | saberlight |
| Benachrichtigung (andere Geräte) | `FFE0` | `FFE4` | ha-triones, led-ble |

led-ble prüft zum Schreiben außerdem `FF01`; das gehört zu älteren LEDnetWF-Geräten (siehe [zengge.md](zengge.md)). Schreibart: HappyLighting-py schreibt ausdrücklich ohne Antwort und wählt eine Characteristic mit „Write without Response“ und „Write“; ha-triones nutzt den Standard von bleak.

## Befehle

Keine Prüfsumme, feste Anfangs- und Endbytes. Hex.

| Befehl | Paket | Länge | Quelle | Im Gateway |
|---|---|---|---|---|
| Ein | `CC 23 33` | 3 | saberlight, flux_led, ha-triones, HappyLighting-py | ja |
| Aus | `CC 24 33` | 3 | dieselben | ja |
| Farbe R, G, B | `56 RR GG BB 00 F0 AA` | 7 | saberlight, ha-triones, flux_led (RGBW-Variante) | ja |
| Weiß (Helligkeit WW) | `56 xx xx xx WW 0F AA` (xx beliebig) | 7 | saberlight, ha-triones | nein |
| Farbe, kurze Variante | `56 RR GG BB AA` | 5 | flux_led `ProtocolLEDENETOriginal` (reine RGB-Geräte) | nein |
| Farbe und Weiß gemeinsam | `56 RR GG BB WW 00 AA` (Modus `00` = alle Kanäle) | 7 | flux_led `LevelWriteMode` | nein |
| Effekt | `BB MM SS 44` (MM = Effekt, SS = Geschwindigkeit) | 4 | saberlight, flux_led, HappyLighting-py | nein |
| Statusabfrage | `EF 01 77`, Antwort per Benachrichtigung | 3 | saberlight, led-ble (`STATE_COMMAND`) | nein |
| Uhr stellen | `10 JH JL MO TT hh mm ss WT 00 01` | 11 | saberlight | nein |
| Mikrofon an | `01 F0 32 00 00 18` | 6 | HappyLighting-py `writeMicState` | nein |
| Mikrofon aus | `01 0F 1E 00 00 18` | 6 | HappyLighting-py | nein |

Einzelheiten:

- **Farbe, Byte 5:** `F0` = Farbmodus (Weiß wird ignoriert), `0F` = Weißmodus (RGB wird ignoriert). HappyLighting-py schreibt in Byte 4 einen festen Wert (25), der im Farbmodus keine Rolle spielt.
- **Helligkeit:** Es gibt **kein eigenes Helligkeitspaket** für Farben. ha-triones und HappyLighting rechnen die Helligkeit in RGB ein; nur im Weißmodus steuert Byte 4 die Helligkeit.
- **Uhr:** JH = Jahr / 100, JL = Jahr % 100, MO = Monat (1 = Januar), TT = Tag, WT = Wochentag (laut Text Sonntag = 0, im Beispiel der Quelle aber Sonntag = 7).
- **Geschwindigkeit (Effekt):** `01` = am schnellsten, saberlight nennt `FF` als langsamsten Wert, das Beispiel nutzt `1F`. flux_led rechnet 0–100 % auf 1–31 um (umgekehrt, 1 = schnell).

### Statusantwort (`EF 01 77`)

12 Byte, per Benachrichtigung (saberlight):

| Byte | Bedeutung |
|---|---|
| 0 | immer `66` |
| 1 | Modellnummer (led-ble: „model_num aka byte 1“) |
| 2 | `23` = an, `24` = aus |
| 3 | Modus: `25`–`38` eingebauter Effekt, `41` Farbe/Weiß statisch |
| 4 | unbekannt |
| 5 | Geschwindigkeit |
| 6–8 | R, G, B |
| 9 | Weiß-Helligkeit |
| 10 | unbekannt (led-ble/flux_led: Versionsnummer) |
| 11 | immer `99` |

### Eingebaute Effekte (`BB MM SS 44`)

| MM | Effekt | MM | Effekt |
|---|---|---|---|
| `25` | Sieben Farben überblenden | `2F` | Grün/Blau überblenden |
| `26` | Rot pulsieren | `30` | Sieben Farben blitzen |
| `27` | Grün pulsieren | `31` | Rot blitzen |
| `28` | Blau pulsieren | `32` | Grün blitzen |
| `29` | Gelb pulsieren | `33` | Blau blitzen |
| `2A` | Cyan pulsieren | `34` | Gelb blitzen |
| `2B` | Lila pulsieren | `35` | Cyan blitzen |
| `2C` | Weiß pulsieren | `36` | Lila blitzen |
| `2D` | Rot/Grün überblenden | `37` | Weiß blitzen |
| `2E` | Rot/Blau überblenden | `38` | Sieben Farben springen |

## Umsetzung im Gateway

- Erkennung: Name als Hinweis (`BandFam::fromName`), entscheidend ist das Schreibziel `FFD5`/`FFD9` bzw. `FFE5`/`FFE9` (`BandFam::matchGatt`).
- Pakete: `BandDriver` in `gateway/components/light_core/` (Ein/Aus, Farbe). Die Helligkeit wird in RGB eingerechnet (`BandDriver::scale`); eine Helligkeitsänderung sendet die zuletzt gezeigte Farbe neu.
- Musikmodus und Mikrofon-EQ gibt es für diese Familie im Gateway nicht. Der Musikschalter des Protokolls wird übergangen, Farben gehen weiter ans Band.
- Status wird nicht gelesen. Wie bei ELK gilt „gesendet“, nicht „vom Band bestätigt“.
