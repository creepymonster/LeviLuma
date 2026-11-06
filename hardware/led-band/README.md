# LED-Band (ELK / Lotus Lantern)

Alles, was das Projekt über die BLE-LED-Leiste weiß, die Legacy und das Next-Gateway steuern. Jede Angabe nennt ihre Herkunft. Legacy liegt im Branch [`Legacy`](https://github.com/creepymonster/LeviLuma/tree/Legacy); Legacy-Pfade unten sind relativ zu dessen Root.

- **„am Gerät getestet (Legacy)“**: im Legacy-Projekt am Band ausprobiert (laut `README.md` und `include/LotusProtocol.h` im Branch `Legacy`).
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
| Schreibart | Legacy und Next schreiben standardmäßig **ohne Antwort**; „Write“ mit Bestätigung funktioniert ebenfalls (Bestätigung nach ca. 60–270 ms) | am Gerät beobachtet (Next-Konsole `ble rsp on`) |
| Wartezeit | etwa 1 s nach dem Verbinden, bevor die Dienste gelesen werden | Legacy `ble.cpp`, in Next übernommen |

## Befehle (Hex)

9-Byte-Pakete, Start `7E`, Ende `EF`. Quelle: Legacy `include/LotusProtocol.h`; die Next-Kopie liegt in `next/gateway/components/light_core/include/LotusProtocol.h`.

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

Die Firmware rechnet Farben vor dem Senden mit der Kalibrierung um (`CalibCore.h`). Das Gateway-Log (`BLE: Farbe … Paket …`) zeigt deshalb das tatsächlich gesendete Paket, nicht die gewählte Farbe.

**Mit LightBlue senden:** Gateway bzw. Legacy-Gerät vom Strom nehmen (sonst ist das Band belegt), mit dem Band verbinden, Service `FFF0` → Characteristic `FFF3` → Hex ohne Leerzeichen, „Write without response“.

## Ansteuerung durch die Firmware

Gilt für Legacy (`src/ble.cpp`) und Next (`BleScheduler`, `next/gateway/components/light_core/`).

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

Details zur Kalibrierung: `next/docs/KALIBRIERUNG.md`.

## Verbindungsverhalten

- **Parameterwunsch:** Etwa 1,5 s nach dem Verbinden verlangt das Band per Link-Layer-Verfahren Intervall 22,5–47,5 ms, Latenz 0, Supervision-Timeout 1 s. Legacy (NimBLE-Arduino) und Next antworten mit eigenen Werten (30–50 ms, Timeout 2,56 s). Herkunft: Next-Log, NimBLE-Arduino 1.4.3 `NimBLEClient.cpp`.
- **MTU:** Legacy handelt 255 aus, Next 256. Das Band stellt nach dem Verbinden **selbst** eine MTU-Anfrage (im Legacy-Log zweites `mtu update event`, im Next-Log mit GATT-Server zweite Zeile `BLE: MTU 256`).
- **Anfragen des Bands müssen beantwortet werden.** Bekommt das Band auf seine Anfrage keine Antwort, nimmt es nach dem ATT-Timeout von **30 s** keine Befehle mehr an. Die Funkverbindung bleibt dabei bestehen (RSSI lesbar, Band weiter unsichtbar).
  - Am Gerät beobachtet mit dem Next-Gateway, solange dessen NimBLE ohne GATT-Server gebaut war (`CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=n`).
  - Ohne GATT-Server verwirft NimBLE solche Anfragen stumm (`ble_att.c`).
  - Abhilfe im Gateway: Peripheral-Rolle und GATT-Server an (`next/gateway/sdkconfig.defaults`). **Am Gerät bestätigt** (Next-Gateway, Stand `f1df579`): Das Gateway beantwortet die MTU-Anfrage des Bands, alle Schreibvorgänge mit Bestätigung wurden über mehr als zwei Minuten Verbindung bestätigt (je etwa 70–350 ms), kein Hängen nach 30 s, keine Trennung.
  - Legacy hat den GATT-Server eingebaut und hielt die Verbindung am Gerät über 40 s ohne Ausfall.
- **Pairing:** Das Band verlangt keine Kopplung (kein Pairing-Ereignis im Next-Log).
- **Verbindungsaufbau bei schwachem Signal:** Bei −97 dBm schlug ein Verbindungsversuch mit Timeout fehl (`Status 13`), der nächste Versuch nach 4 s bei −87 dBm klappte (am Gerät beobachtet).
- **Wiederverbinden:** Legacy und Next versuchen es alle 4 s neu. Das Next-Gateway merkt sich nach der ersten erfolgreichen Verbindung die Adresse des Bands und verbindet danach nur noch dieses (`BandMemory`, Konsole `band forget`).

## Funk

- Der XIAO ESP32S3 (Next-Gateway) braucht seine externe Antenne. Ohne sie empfing er am Gerät rund 25 dB schwächer als ein MacBook am selben Ort, und das Band wurde nicht gefunden.
- Empfangspegel am Gerät beobachtet: im selben Raum mit Antenne etwa −70 dBm, durch zwei Türen und rund 6 m etwa −87 dBm (Verbindung klappte noch).

## Codestellen

| Was | Legacy (Branch `Legacy`) | Next-Gateway |
|---|---|---|
| Pakete | `include/LotusProtocol.h` | `next/gateway/components/light_core/include/LotusProtocol.h` |
| Senden, Reihenfolge, Pausen | `src/ble.cpp` (`bleTask`) | `light_core/src/ble_scheduler.cpp`, `next/gateway/main/ble_link.cpp` |
| Bandauswahl | `bleNameMatches()` | `light_core/src/band_memory.cpp` |
| Diagnose | – | Gateway-Konsole: `status`, `band`, `band forget`, `ble rsp on/off`, `ble off/on`; Log mit Hex-Paketen |
