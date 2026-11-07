# Bandfamilie Zengge / LEDnetWF

Controller, Leisten und Birnen der Firma Zengge, gesteuert über die Apps **Zengge**, **Magic Home** bzw. **Surplife**. Sehr viele Produktvarianten mit unterschiedlichen Befehlen; welche gilt, entscheidet die **Produkt-ID** aus der Werbung.

**Stand: nicht am Gerät getestet.** Alles stammt aus der unten genannten Quelle. Das Next-Gateway nutzt davon nur Ein/Aus und Farbe, die Helligkeit steckt im Farbpaket (siehe [Umsetzung im Gateway](#umsetzung-im-gateway)).

## Quellen

| Kürzel | Quelle | Lizenz | Inhalt |
|---|---|---|---|
| lednetwf | [8none1/lednetwf_ble](https://github.com/8none1/lednetwf_ble): `protocol_docs/` (15 Kapitel, aus der dekompilierten App und Mitschnitten), `custom_components/lednetwf_ble/` (`protocol.py`, `device.py`, `const.py`), Mitschnitte in `pcap/` | MIT | vollständige Protokollbeschreibung |

Aus der Quelle sind nur Fakten (Bytefolgen, UUIDs, Produkt-IDs) übernommen, kein Code. Die Quelle ist sehr umfangreich (Effektlisten, Segmente, Mikrofon, LED-Einstellungen); die Tabellen unten fassen die Befehle zusammen und nennen das Kapitel für Einzelheiten.

## Erkennung

| Merkmal | Wert | Quelle |
|---|---|---|
| Gerätenamen | enthalten `LEDnetWF` oder `IOTWF` | lednetwf `01_quick_reference.md` |
| Langer Name | `LEDnetWF` + 2 Ziffern BLE-Version + 4 Hex Produkt-ID + 6 Hex MAC-Ende (20 Zeichen), z. B. `LEDnetWF02001D0CDA81` → Version 2, Produkt `0x1D` | lednetwf `protocol.product_id_from_name` |
| `IOTBT…` | eigene Variante (Telink-Mesh-Basis, Produkt `0x00`) mit anderen Befehlen (`0x71` ohne Prüfsumme, Farbe `0xE2`/`0xE1`/`0xE0`). **Im Gateway nicht unterstützt.** | lednetwf `17_device_configuration.md` |
| Company-ID der Herstellerdaten | `0x5A00`–`0x5AFF` (das untere Byte ist der Status `sta`), IOTBT `0x1102` | lednetwf `02_manufacturer_data.md` |

### Herstellerdaten (AD-Typ `0xFF`)

Nach der Company-ID (2 Byte, Little Endian):

| Byte | Bedeutung |
|---|---|
| 0 | `sta` (Status) |
| 1 | BLE-Version (Befehlsumfang, siehe unten) |
| 2–7 | MAC-Adresse |
| 8–9 | **Produkt-ID** (Big Endian) |
| 10 | Firmware (unteres Byte) |
| 11 | LED-Version |
| 12 | Bits 0–1 `check_key`, Bits 2–7 Firmware oberer Teil (ab v6) |
| 13 | Firmware-Flags |
| 14–24 | Zustand ab BLE v5: `14` an/aus (`23`/`24`), `15` Modus (`61` statisch, `25` Effekt), `16` Untermodus (`F0` RGB, `0F` Weiß oder Effekt), `18–20` R, G, B, `21` WW, `23` CW |

Ab BLE v7 senden die Geräte zusätzlich Service-Daten unter UUID `FFFF` (16 Byte, Geräteangaben); der Zustand steht dann ab Byte 3 der Herstellerdaten. Einzelheiten: lednetwf `02_manufacturer_data.md`.

### BLE-Version

| Version | Befehle |
|---|---|
| < 5 | Ein/Aus mit `0x71` |
| ≥ 5 | Ein/Aus mit `0x3B` |
| ≥ 8 | erweiterter Zustand |

Quelle: lednetwf `03_device_identification.md`. Die Home-Assistant-Integration sendet Ein/Aus trotzdem für alle Nicht-IOTBT-Geräte mit `0x3B` (`device.py`, `turn_on`).

### Produkt-IDs (Auswahl)

| Produkt-ID | Gerät | Farbpaket | Im Gateway |
|---|---|---|---|
| `0x04`, `0x06`, `0x07`, `0x08`, `0x0E`, `0x10`, `0x1A`, `0x1B`, `0x1E`, `0x20`, `0x25`, `0x26`, `0x29`, `0x33`, `0x35`, `0x3B`, `0x3C`, `0x44`, `0x48`, `0x54`, `0x5B` | einfache RGB-/RGBW-/RGBCW-Controller und Birnen („SIMPLE“) | `0x31` | ja |
| `0x27` | Ctrl_Mini_RGBW | ab BLE v5 `0x3B A1` „v2“, sonst `0x31` | ja |
| `0xA1`–`0xAD`, `0x56`, `0x80`, `0xD1` und unbekannte | Symphony (adressierbare Leisten), Ringleuchten | `0x3B A1` (HSV) | ja |
| `0x1D`, `0x53` | Fill-/Ringleuchten | `0x3B A1`, Effekte abweichend | ja (ungeprüft) |
| `0x09`, `0x16`, `0x1C`, `0x52`, `0x62`, `0xE1`, `0xE2` | nur Weiß (CCT), Deckenleuchten | `0x35` bzw. `0x3B B1` | nein (kein RGB) |
| `0x17`, `0x21`, `0x41` | Dimmer | – | nein |
| `0x0B`, `0x93`–`0x97` | Schalter, Steckdosen | – | nein |
| `0x00` | IOTBT | eigene Befehle | nein |

Vollständige Tabelle mit Geräteklassen: lednetwf `03_device_identification.md` und `const.py` (`PRODUCT_CAPABILITIES`).

## GATT

| Zweck | UUID | Quelle |
|---|---|---|
| Service | `FFFF` | lednetwf `01_quick_reference.md` |
| Schreiben | `FF01` | dieselbe, `const.py` |
| Benachrichtigung (Antworten) | `FF02` | dieselbe |

Die App aktiviert nach dem Verbinden die Benachrichtigung (`FF02`) und fordert eine MTU von 255 (ab BLE v8: 512) an. Die Integration schreibt standardmäßig ohne Antwort. Quelle: lednetwf `04_connection_transport.md`, `device.py`.

## Transportschicht

**Jeder Befehl muss verpackt werden; rohe Befehle wirken nicht.** Kopf (8 Byte), danach der Befehl:

| Byte | Bedeutung |
|---|---|
| 0 | Kopf-Flags: Bits 0–1 Version (`0`), Bits 2–3 Typ (0 JSON, 1 Hex, 2 ACK), Bit 4 Bestätigung erwartet, Bit 5 geschützt, Bit 6 segmentiert. Für einfache Befehle `00`. |
| 1 | Folgenummer 0–255 (je Paket hochzählen) |
| 2–3 | `80 00` = ein vollständiges Segment |
| 4–5 | Länge des Befehls (Big Endian) |
| 6 | Länge des Befehls + 1 |
| 7 | `0A` = Antwort erwartet (Abfragen), `0B` = ohne Antwort (Befehle) |
| 8… | Befehl (meist mit Prüfsumme als letztem Byte) |

Beispiel aus der Quelle: Statusabfrage mit Folgenummer 1 → `00 01 80 00 00 04 05 0A 81 8A 8B 40`.

Antworten kommen mit demselben 8-Byte-Kopf; manche Geräte verpacken die Nutzdaten als JSON (`{"code":0,"payload":"8133…"}`). Quelle: lednetwf `04_connection_transport.md`.

**Prüfsumme:** Summe aller Befehlsbytes, untere 8 Bit.

## Befehle (Inhalt nach dem Transportkopf)

`chk` = Prüfsumme. Hex.

### Ein/Aus

| Befehl | Paket | Geräte | Im Gateway |
|---|---|---|---|
| Ein/Aus modern | `3B PP 00 00 00 00 00 D2 D1 D0 00 00 chk` (PP: `23` an, `24` aus, `25` umschalten; D = Überblendzeit in 10 ms, Big Endian, Integration: `00 00 32` = 0,5 s) | BLE ≥ 5, laut Integration alle Nicht-IOTBT | ja |
| Ein/Aus alt | `71 23 0F A3` (an), `71 24 0F A4` (aus); Byte 2 `F0` = speichern, `0F` = nicht speichern | BLE < 5 | nein |
| Ein/Aus sehr alt | `11 1A 1B F0 chk` (an), `11 1A 1B 0F chk` (aus) | ältere Geräte | nein |
| IOTBT | `71 23` / `71 24` ohne Prüfsumme, cmdId `0A` | IOTBT | nein |

### Farbe und Weiß

| Befehl | Paket | Geräte | Im Gateway |
|---|---|---|---|
| RGB(CW) `0x31` | `31 RR GG BB WW CW MM PP chk` (MM: `F0` nur RGB, `0F` nur Weiß, `5A` alle; PP: `F0` speichern, `0F` nicht) | SIMPLE-Geräte | ja, `MM = F0`, `PP = 0F`, Helligkeit in RGB eingerechnet |
| RGBW 8 Byte | `31 RR GG BB WW 00 PP chk` bzw. `31 RR GG BB WW MM PP chk` | Produkte 51, 8, 4, 161 bzw. 68, 84, 6, 72 laut App | nein |
| HSV `0x3B A1` | `3B A1 HS HS BR 00 00 RR GG BB 00 00 chk`; HS = (Farbton 0–360 ≪ 7) \| Sättigung 0–100, Big Endian; BR = Helligkeit 0–100 | Symphony, BLE ≥ 5 | ja |
| HSV `0x3B A1` v2 | wie oben, aber Bytes 7–9 = Überblendzeit (10 ms, Big Endian), Bytes 10–11 = Verlauf; **RGB dort führt zu Minuten bis Stunden Überblendung** | Produkt `0x27` ab BLE v5 | ja, Bytes 7–11 = 0 |
| CCT `0x35` | `35 B1 TT BR 00 00 D1 D0 chk` (TT 0 = warm … 100 = kalt, BR 0–100, D = Dauer) | nur Weiß | nein |
| CCT `0x3B B1` | `3B B1 00 00 00 TT BR 00 00 1E 00 00 chk` | Symphony-CCT | nein |
| IOTBT | `E2 …` (Farbton), `E0 01 …`, `E1 03 …` (Segmente) | IOTBT-Varianten | nein |

Hinweise der Quelle:

- **`0x31` in 8 oder 9 Byte:** Laut App-Analyse nutzen einige Produkte (z. B. 51 = `0x33`, 8, 4) eine 8-Byte-Form. Die Integration sendet für alle SIMPLE-Geräte die 9-Byte-Form `31 R G B 00 00 F0 0F chk`; das Gateway folgt der Integration.
- **Kein zweiter Befehl direkt nach der Farbe:** Ein Helligkeitsbefehl (`3B 01`) kurz nach einem Farbbefehl übernimmt die *alte* Farbe mit neuer Helligkeit und hebt die Farbänderung auf (Issue #99). Helligkeit gehört in das Farbpaket.
- **Bytes 10–11 bei `3B A1` (Zeit):** nicht null führt zu Verzögerungen im Sekundenbereich. Immer `00 00`.
- **Farbton aus einem Mitschnitt lesen:** Das obere Byte von HS ist bei Sättigung ≤ 100 immer Farbton / 2. Ungerade Farbtöne erkennt man am unteren Byte (+128).

### Helligkeit

| Befehl | Paket | Geräte | Im Gateway |
|---|---|---|---|
| alt | `47 BR chk` (0–255) | ältere Geräte | nein |
| `0x3B 01` | `3B 01 00 00 BR 00 BR D2 D1 D0 G1 G0 chk` (BR 0–100, D Überblendung, G Verlauf) | Firmware ≥ 9 | nein (Helligkeit im Farbpaket, siehe Hinweis oben) |

### Zustand

| Befehl | Paket | Antwort |
|---|---|---|
| Statusabfrage | `81 8A 8B 40` (cmdId `0A`) | 14 Byte: `81`, Modus, an/aus (`23` = an), Modustyp (`61` statisch, `62` Musik, `25`–`38` Effekt), Untermodus, Weiß-Helligkeit, R, G, B, WW, LED-Version, CW, reserviert, Prüfsumme |
| LED-Einstellungen | `63 …` bzw. `44 …` | siehe lednetwf `16_query_formats_0x63_vs_0x44.md` |

Einzelheiten und Sonderfälle (Byte 12 je Produkt verschieden, Effekt 37 = `0x25` kollidiert mit der Effektkennung): lednetwf `08_state_query_response_parsing.md`.

### Effekte

| Geräte | Paket | Geschwindigkeit | Effekte |
|---|---|---|---|
| SIMPLE (`0x33`, `0x06`, `0x04`, `0x44` …) | `61 EE SS PP chk` (PP `F0` speichern / `0F` nicht) | 1–31, **umgekehrt** (1 = schnell) | 37–56 (Tabelle unten) |
| Adressierbare Leisten `0x54`, `0x55`, `0x62`, `0x5B` | `38 EE SS BR chk` | 0–100 direkt | 1–44 |
| Fill-/Ringleuchte `0x1D` | `38 EE SS BR` (**ohne** Prüfsumme) | 0–100 direkt | 1–113, `FF` = alle |
| Symphony `0xA1`–`0xAD` | `42 EE SS BR chk` | 0–100 direkt | 1–100 |
| Musikleisten `0x56`, `0x80` | `42 EE SS BR chk`; statische Effekte mit Hintergrundfarbe über `0x41` | 0–100 direkt | 1–99 |

**Helligkeit 0 in einem Effektbefehl schaltet das Gerät aus**; mindestens 1 senden. Effektnamen: lednetwf `07_effect_names.md`, Formate: `06_effect_commands.md`, Hintergrundfarbe: `15_static_effects_with_bg_color.md`.

SIMPLE-Effekte (`61 EE …`):

| EE | Effekt | EE | Effekt |
|---|---|---|---|
| 37 (`25`) | Sieben Farben überblenden | 47 (`2F`) | Grün/Blau überblenden |
| 38 (`26`) | Rot pulsieren | 48 (`30`) | Sieben Farben blitzen |
| 39 (`27`) | Grün pulsieren | 49 (`31`) | Rot blitzen |
| 40 (`28`) | Blau pulsieren | 50 (`32`) | Grün blitzen |
| 41 (`29`) | Gelb pulsieren | 51 (`33`) | Blau blitzen |
| 42 (`2A`) | Cyan pulsieren | 52 (`34`) | Gelb blitzen |
| 43 (`2B`) | Lila pulsieren | 53 (`35`) | Cyan blitzen |
| 44 (`2C`) | Weiß pulsieren | 54 (`36`) | Lila blitzen |
| 45 (`2D`) | Rot/Grün überblenden | 55 (`37`) | Weiß blitzen |
| 46 (`2E`) | Rot/Blau überblenden | 56 (`38`) | Sieben Farben springen |

Die Effektnummern stimmen mit den Triones-Effekten `25`–`38` überein (siehe [triones.md](triones.md)).

### Mikrofon (Musikmodus)

| Geräte | Paket |
|---|---|
| einfache Mikrofon-Controller `0x08`, `0x48` | `73 EN SE 0F chk` (EN `01` an / `00` aus, SE Empfindlichkeit 1–100), z. B. `73 01 21 0F A4` |
| Symphony mit Mikrofon `0xA2`–`0xA9`, `0xAA`–`0xAD` | `73 EN MM EE R1 G1 B1 R2 G2 B2 SE BR chk` (MM `27` Gerätemikrofon, `26` App-Mikrofon; EE Effekt, `FF` = alle Farben; 1 = Vordergrund, 2 = Hintergrund) |
| `0xA1` | kein Mikrofon |

Quelle: lednetwf `18_sound_reactive_music_mode.md`.

### Weitere Funktionen in der Quelle

Nicht ausgewertet, nur verwiesen: LED-Anzahl, Chip-Typ und Farbreihenfolge (`17_device_configuration.md`, `16_query_formats_0x63_vs_0x44.md`), Kerzenmodus, Segmente, Vorhang-Matrix (`10_device_specific.md`).

## Umsetzung im Gateway

- Erkennung: Name `LEDnetWF…`/`IOTWF…` als Hinweis, entscheidend ist das Schreibziel `FFFF`/`FF01` (`BandFam::matchGatt`). Produkt-ID und BLE-Version aus den Herstellerdaten (`BandFam::parseZenggeMfg`), ersatzweise aus dem langen Namen (`parseZenggeName`). Ohne beides: Farbe wie Symphony (`0x3B A1`), wie die Integration bei unbekannten Produkten.
- Pakete: `BandDriver` mit Transportschicht (Version 0, cmdId `0B`, Folgenummer je Paket).
  - Ein/Aus: `3B 23`/`3B 24` mit 0,5 s Überblendung wie die Integration.
  - Farbe: `0x31`, `0x3B A1` oder `0x3B A1` v2 nach Produkt-ID (`BandDriver::zenggeColorFor`). Die Helligkeit steckt im Farbpaket (RGB eingerechnet bzw. Helligkeitsbyte = gewählte Helligkeit × Hellwert der kalibrierten Farbe). Eine Helligkeitsänderung sendet die Farbe neu, kein `3B 01`.
  - Nicht unterstützte Produkte (IOTBT, nur Weiß, Dimmer, Schalter): Das Gateway trennt nach dem Erkennen und meldet das im Log.
- **Offen:** Die Integration aktiviert nach dem Verbinden die Benachrichtigung an `FF02`. Das Gateway tut das nicht, weil es keinen Zustand liest. Ob ein Gerät Befehle ohne aktivierte Benachrichtigung annimmt, ist ungeprüft.
- Musikmodus und Mikrofon-EQ gibt es für diese Familie im Gateway nicht.
