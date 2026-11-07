# Bandfamilie BJ_LED (App „MohuanLED“)

Sehr günstige USB-LED-Leisten (AliExpress), gesteuert über die App **MohuanLED**.

**Stand: nicht am Gerät getestet.** Einzige Quelle ist ein Home-Assistant-Projekt, dessen Autor nach eigener Aussage nur **ein** Band getestet hat. Das Next-Gateway nutzt davon Ein/Aus und Farbe, die Helligkeit wird in die Farbe eingerechnet.

## Quellen

| Kürzel | Quelle | Lizenz | Inhalt |
|---|---|---|---|
| bj_led | [8none1/bj_led](https://github.com/8none1/bj_led) (`README.md`, `custom_components/bj_led/bjled.py`, HCI-Mitschnitte in `bt_snoops/`) | MIT | Befehle, Effektliste, UUID |

Aus der Quelle sind nur Fakten (Bytefolgen, UUIDs) übernommen, kein Code.

## Erkennung und GATT

| Merkmal | Wert | Quelle |
|---|---|---|
| Gerätename | `BJ_LED` | bj_led README und `manifest.json` (`local_name`) |
| Adresse | beginnt laut Quelle oft mit `FF:FF` | bj_led README |
| Schreiben | Characteristic `EE01` (Service in der Quelle nicht genannt; das Gateway sucht sie in allen Services) | bj_led `WRITE_CHARACTERISTIC_UUIDS` |
| Benachrichtigung | angeboten, laut Quelle sendet das Band aber nichts; die App aktiviert sie nicht | bj_led README |
| Schreibart | ohne Antwort | bj_led `write_gatt_char(..., False)` |

## Befehle

Kopf `69 96`, danach Länge/Befehl, keine Prüfsumme. Hex.

| Befehl | Paket | Quelle | Im Gateway |
|---|---|---|---|
| Ein | `69 96 02 01 01` | bj_led Code (`TURN_ON_CMD`) | ja |
| Ein (abweichend) | `69 96 06 01 01` | bj_led README; widerspricht dem Code, vermutlich Schreibfehler | nein |
| Aus | `69 96 02 01 00` | bj_led Code und README | ja |
| Farbe R, G, B | `69 96 05 02 RR GG BB` | bj_led Code | ja |
| Farbe mit Weiß | `69 96 05 02 RR GG BB WW` | bj_led README (Beispiele: Rot `7F 00 00 7F`, Weiß `FF FF FF FF`); das Band hat laut Quelle keine weiße LED, das letzte Byte kann entfallen | nein |
| Effekt | `69 96 03 03 EE SS` (EE = Effekt `00`–`15`, SS = Geschwindigkeit `01` schnell bis `0A` langsam) | bj_led README und Code | nein |

- **Helligkeit:** Es gibt **kein Helligkeitspaket**. bj_led rechnet die Helligkeit in RGB ein.
- **Status:** nicht lesbar (siehe Benachrichtigung oben). bj_led arbeitet deshalb „optimistisch“.
- **Uhr/Timer:** laut Quelle aus den HCI-Mitschnitten ableitbar, aber nicht ausgewertet und nicht dokumentiert.
- **Mikrofon:** von der Quelle nicht unterstützt.
- **Lange Verbindungen:** Nach vielen Stunden Verbindung ignorierte das Band laut Quelle Befehle stillschweigend; bj_led trennt seit Version 0.0.2 nach 120 s ohne Befehl. Das Gateway hält die Verbindung dauerhaft. Am Gerät beobachten.

### Effekte (`69 96 03 03 EE SS`)

| EE | Effekt | EE | Effekt |
|---|---|---|---|
| `00` | Farbschleife | `0B` | Regenbogen überblenden |
| `01` | Rot pulsieren | `0C` | Farben blitzen |
| `02` | Grün pulsieren | `0D` | Rot blitzen |
| `03` | Blau pulsieren | `0E` | Grün blitzen |
| `04` | Gelb pulsieren | `0F` | Blau blitzen |
| `05` | Cyan pulsieren | `10` | Gelb blitzen |
| `06` | Magenta pulsieren | `11` | Cyan blitzen |
| `07` | Weiß pulsieren | `12` | Magenta blitzen |
| `08` | Rot/Grün überblenden | `13` | Weiß blitzen |
| `09` | Rot/Blau überblenden | `14` | Farben springen |
| `0A` | Grün/Blau überblenden | `15` | RGB springen |

## Umsetzung im Gateway

- Erkennung: Name `BJ_LED` als Hinweis, entscheidend ist eine beschreibbare Characteristic `EE01` (`BandFam::matchGatt`).
- Pakete: `BandDriver` (Ein/Aus, Farbe mit eingerechneter Helligkeit). Eine Helligkeitsänderung sendet die zuletzt gezeigte Farbe neu.
- Musikmodus und Mikrofon-EQ gibt es für diese Familie im Gateway nicht.
