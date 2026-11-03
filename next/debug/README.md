# LeviLuma Diagnose (ESP-NOW-Mitschnitt für den Seeed XIAO ESP32-S3)

Kleine, eigenständige ESP-IDF-Firmware (v5.5.6), die ESP-NOW-Verkehr **passiv** beobachtet und jede empfangene Nachricht mit allen auswertbaren Angaben im seriellen Monitor ausgibt. Gedacht für die Kommunikation zwischen LeviLuma-Controllern und -Gateways.

> **Flashen ersetzt die bisherige Firmware auf diesem XIAO** (zum Beispiel ein Gateway). Zum Zurückwechseln muss die gewünschte Firmware erneut geflasht werden. Die Diagnosefirmware liest und schreibt den NVS nicht und nutzt dieselbe Flash-/Partitionseinstellung wie das Gateway; gespeicherte Kopplungen und Schlüssel eines früheren Gateways sollten deshalb erhalten bleiben. **Das wurde nicht am Gerät geprüft.** `erase-flash` löscht sie in jedem Fall.

## Was das Gerät tut und was nicht

- **Passiv:** keine Kopplung, keine Steuerbefehle, keine Antworten, keine Veränderung anderer Geräte. Im Code gibt es kein `esp_now_send`, kein `esp_now_add_peer`, kein `esp_wifi_connect`, keinen WLAN-Scan und keinen NVS-Zugriff. `next/tools/check_debug_build.py` prüft das im Build (Quelltexte und ungebundene Symbole der eigenen Objektdateien).
- **Einschränkung dieser Zusicherung:** Sie gilt für die Anwendung. Was die WLAN-Hardware selbst tut, ist hier nicht belegt (z. B. ein MAC-Quittungsrahmen auf Unicasts, die an die MAC dieses Geräts gerichtet sind; im normalen Betrieb schickt niemand etwas an diese MAC).
- Es gibt **keine** BLE-, Display-, LVGL- oder Zigbee-Funktion (`set(COMPONENTS main)` hält diese Komponenten aus dem Build).

## Technische Empfangsgrenzen

Grundlage: ESP-IDF v5.5.6, `api-reference/network/esp_now` und `api-guides/wifi` (Abschnitt „Wi-Fi Sniffer Mode“).

| | Normaler ESP-NOW-Empfang (`esp_now_register_recv_cb`) | WLAN-Promiscuous-Modus (verwendet) |
|---|---|---|
| Rahmen | nur **Broadcast** und **an die eigene MAC** gerichtete Rahmen. Ohne eingetragenen Peer nur Broadcasts und *unverschlüsselte* Unicasts (Doku). | alle Management-Rahmen **auf dem eingestellten Kanal**, also auch Unicasts zwischen anderen Geräten |
| Angaben | Absender, Ziel, `rx_ctrl` (RSSI, Kanal), Nutzdaten | kompletter 802.11-Rahmen: Adressen, Frame Control (Retry, Protected), Rahmenzähler, ESP-NOW-Kopf, Versionsfeld, Zufallswerte, Länge, RSSI, Kanal, Hardware-Zeitstempel |
| Verschlüsselte Unicasts | nur mit eingetragenem Peer und gleichem LMK | sichtbar, aber **Inhalt nicht lesbar** (kein Schlüssel) |

Daraus folgt:

- Das Gerät ist **kein vollständiger Mitschnitt**. Es hört nur **einen Kanal** zur Zeit, hört nur in Reichweite, und Rahmen mit Empfangsfehlern oder bei Überlast gehen verloren. Verworfene Nachrichten (volle Queue) werden gezählt (`stats`), Verluste im Funk nicht.
- **Verschlüsselte Nutzdaten** (ESP-NOW nutzt laut Doku CCMP mit einem LMK je Peer) werden **nie dekodiert**: Ist das Protected-Bit gesetzt, zeigt die Ausgabe nur Metadaten (Adressen, Länge, RSSI, Kanal, Rahmenzähler) und die rohen Bytes, die mit „nicht dekodiert, verschlüsselt“ gekennzeichnet sind. Es gibt keine eigene Kryptografie und keine Schlüsseleingabe.
- Verschlüsselte Rahmen lassen sich nur **heuristisch** als ESP-NOW erkennen (Action-Rahmen mit Protected-Bit und Broadcast im Adressfeld 3, das die Doku für ESP-NOW nennt), weil Kategorie und OUI mit verschlüsselt sind. Das Gerät kennzeichnet sie entsprechend.
- **Broadcasts** (`DISCOVER`, `ANNOUNCE`) sind laut ESP-NOW-Doku nie verschlüsselt und deshalb immer dekodierbar. Ist ein Gateway mit Schlüssel eingerichtet, sind seine Unicast-Antworten und die Controller-Befehle verschlüsselt, also hier nicht lesbar.
- Das ESP-NOW-**Versionsfeld** wird als rohes Byte ausgegeben. Die Doku nennt die Bitfelder (v1: Version in Bit 3..0; v2 zusätzlich „More data“), aber keine Zuordnung der Werte zu „v1“ und „v2“; das Gerät deutet sie deshalb nicht. ESP-NOW-v2-Rahmen mit mehreren Elementen werden erkannt (Anzahl), ausgewertet wird nur das erste.
- `sig_len` des Treibers enthält laut Header-Kommentar die 4 Byte FCS; die Auswertung geht davon aus. **Am Gerät nicht überprüft.**
- Erfasst werden höchstens 320 Byte je Rahmen (ESP-NOW v1: höchstens 293 mit FCS). Längere Rahmen werden als „abgeschnitten“ gekennzeichnet, Prüfsummen dann nicht bewertet.

## Bauen, Flashen, Monitor

Voraussetzung: ESP-IDF **v5.5.6** (`. $IDF_PATH/export.sh`).

```sh
cd next/debug
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor        # Linux; macOS /dev/cu.usbmodem*, Windows COMx
```

- **Schnittstelle:** USB-C des XIAO, Konsole über **USB-Serial-JTAG** (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`), wie beim Gateway. Der Port erscheint meist als `/dev/ttyACM0`. Erscheint er nicht, BOOT-Taste gedrückt halten, USB einstecken und erneut flashen. Befehle mit Enter abschließen.
- **Konfiguration:** `idf.py menuconfig` → „LeviLuma Diagnose (ESP-NOW-Empfänger)“ und „LeviLuma ESP-NOW“ (Kanal, gemeinsam mit Controller und Gateway).
- **Alle Optionen** (CI-Testvariante mit Suchlauf, zweitem Empfangsweg und vollem Rohrahmen):

```sh
idf.py -B build-full -D SDKCONFIG=build-full/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.full" set-target esp32s3 build
```

| Option (Kconfig) | Standard | Wirkung |
|---|---|---|
| `LEVILUMA_DEBUG_CHANNEL_FOLLOW` | an | Standardkanal = `LEVILUMA_ESPNOW_CHANNEL` (Menü „LeviLuma ESP-NOW“, Standard 1) |
| `LEVILUMA_DEBUG_CHANNEL` | 1 | eigener Kanal, wenn der LeviLuma-Kanal nicht übernommen wird |
| `LEVILUMA_DEBUG_COUNTRY` | leer | optionaler Ländercode; bestimmt die zulässigen Kanäle. Verantwortung für die Vorschriften liegt beim Anwender |
| `LEVILUMA_DEBUG_SCAN_ENABLE` | **aus** | erlaubt den Kanalsuchlauf (siehe unten) |
| `LEVILUMA_DEBUG_SCAN_AT_BOOT`, `_DWELL_MS`, `_STAY_ON_HIT` | aus / 500 / an | Suchlauf beim Start, Verweilzeit je Kanal, Halt beim ersten gültigen LeviLuma-Rahmen |
| `LEVILUMA_DEBUG_QUEUE_LEN` | 16 | Einträge der Empfangs-Queue (je etwa 350 Byte) |
| `LEVILUMA_DEBUG_OUT_BUF` | 6144 | Ausgabepuffer je Nachricht; Überlänge wird gekürzt und gezählt |
| `LEVILUMA_DEBUG_ESPNOW_CB`, `_DEDUP_MS` | aus / 50 | zusätzlicher Empfangsweg über den normalen ESP-NOW-Callback; doppelte Rahmen werden erkannt |
| `LEVILUMA_DEBUG_SHOW_ENCRYPTED`, `_ASCII`, `_FULL_FRAME` | an / an / aus | Startwerte der Anzeige |

## Kanalwahl

- Standard ist der konfigurierte LeviLuma-Kanal. Beim Start wird er gegen die Länderregeln des Funktreibers (`esp_wifi_get_country`) geprüft; ist er dort nicht zulässig, weicht das Gerät mit deutlicher Warnung auf den ersten zulässigen Kanal aus.
- Zur Laufzeit: `channel` zeigt den Kanal und den zulässigen Bereich, `channel <n>` wechselt (nicht gespeichert). Unzulässige Kanäle werden abgelehnt.
- **Optionaler Suchlauf** (`LEVILUMA_DEBUG_SCAN_ENABLE`, standardmäßig aus): `scan on [ms]` wechselt zyklisch durch die zulässigen Kanäle. **Während des Suchlaufs werden Nachrichten auf allen anderen Kanälen verpasst.** Das gilt besonders für kurze Verweilzeiten, denn ein Gateway meldet sich standardmäßig nur alle 2 s (ANNOUNCE). Bei `STAY_ON_HIT` bleibt das Gerät auf dem Kanal des ersten gültigen LeviLuma-Rahmens. `scan off` oder `channel <n>` beendet den Suchlauf.

## Monitorbefehle

```
help                         Hilfe
stats [reset]                Zähler (Verluste, Doppelte, Filter, Fehler) anzeigen bzw. zurücksetzen
channel [1..14]              Kanal anzeigen bzw. ändern
scan [on [ms]|off]           Kanalsuchlauf (nur wenn per Kconfig erlaubt)
filter                       Filter anzeigen
filter clear                 alle Filter löschen
filter mac add|del <MAC>     Absender oder Ziel (bis zu 4); filter mac clear
filter type add|del <Typ>    LeviLuma-Typ als Name oder Zahl (STATE, set_state, 0x22); filter type clear
filter levi on|off           nur Rahmen mit LeviLuma-Kennung
filter enc on|off            verschlüsselte Rahmen anzeigen
raw on|off                   zusätzlich den ganzen erfassten Rahmen als Hexdump
ascii on|off                 ASCII-Spalte im Hexdump
```

Der Typfilter zeigt nur Rahmen mit **bestätigtem** Kopf (CRC ok); Rahmen mit falscher CRC oder verschlüsselte Rahmen blendet er aus. Ausgeblendete Rahmen zählt `stats` unter „Durch Filter ausgeblendet“.

## Ausgabe

Pro Nachricht: Nummer, Laufzeit, Kanal, RSSI, Empfangsweg; Absender, Ziel (Broadcast/Multicast/Unicast); 802.11-Angaben (Typ/Subtyp, Retry, Protected, Rahmenzähler, Adressfeld 3); **Längen getrennt** (Rahmen laut Treiber, mit/ohne FCS, erfasst, MAC-Kopf, ESP-NOW-Element, Nutzdaten); ESP-NOW-Kopf (Kategorie, OUI, Zufallswerte, Element-ID, Typ, Versionsfeld); Bewertung; bei LeviLuma-Nachrichten Version, Typ mit Klartextnamen, Flags (RESPONSE/NOTIFY), Sequenz, Referenzsequenz, Sitzung, `len`-Feld, CRC-Ergebnis und **alle Nutzdatenfelder** mit Namen; Hexdump der Nutzdaten (ASCII-Spalte optional). Nicht verfügbare Angaben stehen als „nicht verfuegbar“ (zum Beispiel 802.11-Kopf und Versionsfeld beim normalen ESP-NOW-Empfang). Die Ausgabe nutzt ASCII ohne Umlaute, damit jeder Monitor sie darstellt.

Dekodiert werden alle Typen des gemeinsamen Protokolls (`next/shared/protocol`): DISCOVER, ANNOUNCE, PAIR_REQ, PAIR_RSP, UNPAIR, GET_STATE, SET_STATE, STATE, GET_CALIB, SET_CALIB, RESET_CALIB, CALIB, ACK, NACK. Unbekannte Versionen, unbekannte Typen, falsche CRC, falsche Länge, ungültige Nutzdaten, abgeschnittene oder verschlüsselte Rahmen werden mit **Fehlergrund und Rohdaten** ausgegeben; Felder werden dann nicht erraten. Bei unvollständiger Erfassung steht die CRC als „nicht geprüft“, die Kopffelder als „UNGEPRUEFT“.

### Beispielausgaben

> **Alle Beispiele sind synthetisch.** Sie stammen nicht von einem Gerät: Die Rahmen wurden im Host-Test (`next/tests/test_debug.cpp`) von Hand aus der ESP-IDF-Beschreibung des Rahmenaufbaus und mit dem gemeinsamen Protokoll-Kodierer gebaut und vom echten Formatierer der Firmware (`dbg::format_record`) ausgegeben. Adressen, RSSI, Zeiten und die „verschlüsselten“ Bytes (Pseudozufall) sind erfunden. Wie echte Rahmen am Gerät aussehen, ist **nicht** geprüft.

ANNOUNCE-Broadcast eines Gateways:

```
#1  t=12.345s  Kanal 1  RSSI -52 dBm  Weg: Promiscuous  (HW-Zeit 777000 us)
  Absender   : 24:0A:C4:00:00:01
  Ziel       : FF:FF:FF:FF:FF:FF  (Broadcast)
  802.11     : Typ 0 Subtyp 13 (Action), Retry nein, Protected nein, ToDS/FromDS 0/0, Rahmenzaehler 3 Fragment 0
  Adressfeld3: FF:FF:FF:FF:FF:FF
  Laengen    : Rahmen laut Treiber 83 Byte (inkl. 4 Byte FCS, ohne FCS 79), erfasst 83, MAC-Kopf 24, ESP-NOW-Element 45 (Nutzdaten laut Element 40)
  ESP-NOW    : Kategorie 127, OUI 18:FE:34, Zufallswerte DEADBEEF, Element-ID 221, Typ 4, Versionsfeld 0x01 (untere 4 Bit: 1)
  Bewertung  : OK - ESP-NOW-Element vollstaendig
  Nutzdaten  : 40 Byte vorhanden
  LeviLuma   : GUELTIG
    Version  : 1
    Typ      : ANNOUNCE (0x02)
    Flags    : 0x02 [NOTIFY]
    Sequenz  : 5   Referenzsequenz: 0   Sitzung: 0xCAFE0001
    Laenge   : len-Feld 24, vorhanden 24
    CRC      : OK (Rahmen 0x5D12, berechnet 0x5D12)
    Felder   :
      gateway_id       = 24:0A:C4:00:00:01
      fw_version       = 1.2 (0x0102)
      caps             = 0x05 [KEY_REQUIRED|BLE_CONNECTED]
      state_revision   = 42
      name             = "Wohnzimmer"
  Hexdump    : Nutzdaten (ESP-NOW-Body), 40 Byte
    0000  4C 56 01 02 02 18 05 00 00 00 01 00 FE CA 12 5D  |LV.............]|
    0010  24 0A C4 00 00 01 02 01 05 2A 00 00 00 0A 57 6F  |$........*....Wo|
    0020  68 6E 7A 69 6D 6D 65 72                          |hnzimmer|
```

Verschlüsselter Unicast (nur Metadaten und Rohbytes, keine Felder):

```
#1  t=15.033s  Kanal 1  RSSI -49 dBm  Weg: Promiscuous  (HW-Zeit 777000 us)
  Absender   : 24:0A:C4:00:00:02
  Ziel       : 24:0A:C4:00:00:01  (Unicast)
  802.11     : Typ 0 Subtyp 13 (Action), Retry nein, Protected ja, ToDS/FromDS 0/0, Rahmenzaehler 3 Fragment 0
  Adressfeld3: FF:FF:FF:FF:FF:FF
  Laengen    : Rahmen laut Treiber 67 Byte (inkl. 4 Byte FCS, ohne FCS 63), erfasst 67, MAC-Kopf 24
  ESP-NOW    : Kopf nicht lesbar (verschluesselt)
  Bewertung  : VERSCHLUESSELT - Protected-Bit gesetzt, Inhalt verschluesselt und ohne Schluessel nicht lesbar (Zuordnung zu ESP-NOW ist nur eine Heuristik: Action-Rahmen mit Broadcast im Adressfeld 3)
  LeviLuma   : NICHT DEKODIERT - Inhalt verschluesselt (Schluessel liegt dem Diagnosegeraet nicht vor)
  Rohdaten   : 39 Byte nach dem MAC-Kopf, verschluesselt (nur Chiffretext, keine Klartextfelder):
    0000  1F 24 0F 90 7F 33 79 13 F3 1C 17 3F BA DD 4F 76  |.$...3y....?..Ov|
    0010  1A 27 AF 0E 5C 91 02 E1 F0 8E F8 89 D6 0B 94 EF  |.'..\...........|
    0020  97 10 C1 06 61 D9 E3                             |....a..|
```

Beschädigter Rahmen (ein Bit gekippt, CRC falsch, Felder nicht angezeigt):

```
#1  t=16.000s  Kanal 1  RSSI -52 dBm  Weg: Promiscuous  (HW-Zeit 777000 us)
  ...
  LeviLuma   : FEHLER (Kopffelder UNGEPRUEFT)
    Version  : 1
    Typ      : STATE (0x22)
    Flags    : 0x03 [RESPONSE|NOTIFY]
    Sequenz  : 12   Referenzsequenz: 11   Sitzung: 0xCAFE0001
    Laenge   : len-Feld 16, vorhanden 16
    CRC      : FALSCH (Rahmen 0x8B99, berechnet 0x2565)
    Fehler   : CRC-Pruefsumme stimmt nicht: Rahmen beschaedigt, Feldwerte unzuverlaessig [CRC]
  Hexdump    : Nutzdaten (ESP-NOW-Body), 32 Byte
  ...
```

Befehle (ebenfalls synthetisch, mit erfundenen Zählerständen):

```
> channel 13
Kanal 13 nicht zulaessig (Laenderregeln des Funktreibers: 1..11).
> filter type add STATE
Filter:
  MAC (Absender oder Ziel): alle
  LeviLuma-Typ (nur Rahmen mit gueltiger CRC): STATE (0x22)
  nur LeviLuma-Kennung: nein, verschluesselte Rahmen anzeigen: ja
> stats
Statistik (Laufzeit 65.432 s)
  Management-Rahmen gesehen : 1523 (davon kein ESP-NOW-Kandidat, ignoriert: 1498)
  ...
  VERWORFEN (Queue voll)    : 0
  ...
```

## Aufbau und Nebenläufigkeit

| Datei | Aufgabe |
|---|---|
| `components/debug_core/src/dbg_frame.cpp` | WLAN-/ESP-NOW-Rahmenauswertung (MAC-Kopf, Protected-Bit, Elemente), Vorfilter |
| `components/debug_core/src/dbg_levi.cpp` | LeviLuma-Dekodierung mit `lvp_decode`/`lvp_crc16` aus `next/shared/protocol` (keine Protokollkopie), Feldlisten je Typ |
| `components/debug_core/src/dbg_format.cpp`, `dbg_text.cpp` | Textausgabe in einen begrenzten Puffer, Hexdump |
| `components/debug_core/src/dbg_state.cpp`, `dbg_cmd.cpp` | Zähler, Filter, Doppelerkennung, Kanalplan, Monitorbefehle |
| `main/sniffer.cpp` | Funk: Promiscuous-Callback (optional ESP-NOW-Callback), Kanalwechsel, Suchlauf |
| `main/report.cpp` | Auswertetask: Queue → Auswertung → Filter → Ausgabe |
| `main/console.cpp` | serielle Eingabe |

Die Empfangs-Callbacks laufen im WLAN-Task und tun nur: Länge prüfen, Vorfilter, höchstens 320 Byte kopieren, in die Queue stellen (`xQueueSend` ohne Wartezeit). Formatierung und Ausgabe passieren im eigenen Task. Ist die Queue voll, wird die Nachricht verworfen und in `queue_drops` gezählt; Kürzungen und Ausgabekürzungen werden ebenfalls gezählt. Jeder Zähler hat einen schreibenden Kontext. Der Rücksetz-Befehl `stats reset` kann deshalb einzelne gleichzeitige Zählungen verlieren.

Doppelausgaben: Wird der zweite Empfangsweg aktiviert, gilt ein Rahmen als doppelt, wenn Absender, Ziel und Nutzdaten übereinstimmen, er über den **anderen** Weg kam und innerhalb von `LEVILUMA_DEBUG_DEDUP_MS` liegt. Zwei Rahmen desselben Wegs (zum Beispiel MAC-Wiederholungen mit Retry-Bit oder LeviLuma-Wiederholungen derselben Anfrage) bleiben beide sichtbar. Wenn der Rahmen zuerst über den Callback eintrifft, wird die ärmere Ansicht (ohne 802.11-Kopf) ausgegeben und die reichere unterdrückt.

## Neue Nachrichtentypen im Protokoll

Siehe `AGENTS.md`, Abschnitt „Diagnosegerät“: `levi_type_supported` und `fill_fields` in `dbg_levi.cpp` erweitern, ein Beispiel in `sample_for` (`next/tests/test_debug.cpp`) ergänzen. Der Test `test_dbg_levi_all_protocol_types` schlägt fehl, bis beides vorhanden ist.

## Prüfungen: was tatsächlich lief

Ausgeführt (lokale Sitzung, Linux, ESP-IDF v5.5.6, gcc/g++):

- **Host-Tests** (`next/tests/run_tests.sh`): alle 85 Tests, 10509 Prüfungen, 0 Fehler, mit AddressSanitizer und UndefinedBehaviorSanitizer. Davon 22 Tests (9750 Prüfungen) in der Gruppe „Diagnose“: gültige, unbekannte, verschlüsselte, beschädigte und abgeschnittene Rahmen, alle Protokolltypen, Ausgabeformat, Filter, Doppelerkennung, Befehle, Fuzzing (20000 Fälle). **Alle Rahmen sind synthetisch.**
- **Firmware-Builds** mit `idf.py` (ESP-IDF v5.5.6, Ziel esp32s3), beide ohne Fehler und Warnungen: Standardkonfiguration (Binärgröße 0xA3150 Byte, 57 % der kleinsten App-Partition frei) und die Variante `sdkconfig.defaults.full`.
- **Passivitätsprüfung** `next/tools/check_debug_build.py` auf beiden Builds: bestanden. Sie wurde auch mit einem absichtlich eingefügten `esp_now_send` und `nvs_flash_init` auf Fehlschlag geprüft.

**Nicht ausgeführt:**

- **Kein Hardwaretest.** Das Gerät wurde weder geflasht noch gestartet. Ungeprüft sind insbesondere: Empfang echter Rahmen im Promiscuous-Modus, das tatsächliche Aussehen echter (auch verschlüsselter) ESP-NOW-Rahmen, ob die FCS im Puffer liegt (Annahme laut Header-Kommentar), das Verhalten bei Last, Ländergrenzen auf dem Gerät, die serielle Eingabe über USB-Serial-JTAG, der Suchlauf und der zweite Empfangsweg.
- Der CI-Lauf für den neuen Build-Eintrag in `.github/workflows/next.yml` ist noch nicht erfolgt.
