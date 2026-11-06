# LED-Band (ELK / Lotus Lantern)

Gesammelte Erkenntnisse zur BLE-LED-Leiste, die Legacy und das Next-Gateway steuern. Jede Angabe nennt ihre Herkunft. „Am Gerät beobachtet“ heißt: in Logs des Entwicklers mit dem Band `ELK-BLEDDM 96` gesehen. Das ist kein systematischer Test aller Bänder der Familie.

## Erkennung

| Merkmal | Wert | Herkunft |
|---|---|---|
| Gerätenamen | beginnen mit `ELK-BLEDDM`, `ELK-BLEDOM`, `ELK-BLEDOB`, `MELK` oder `LEDBLE` | Legacy `bleNameMatches()`, Next `BandMemory::nameMatches()` |
| Name in der Werbung | oft erst in der Scan-Antwort; aktiver Scan nötig | am Gerät beobachtet (ohne Antenne fehlten die Namen) |
| Sichtbarkeit | Das Band wirbt nicht mehr, solange ein Gerät verbunden ist (verschwindet z. B. aus LightBlue) | am Gerät beobachtet |
| Verbindungen | nur eine gleichzeitig | am Gerät beobachtet (iPhone sieht das Band nicht, solange das Gateway verbunden ist) |

## GATT

| Merkmal | Wert | Herkunft |
|---|---|---|
| Service | `0xFFF0` | Legacy, Next |
| Characteristic | `0xFFF3`, Handle 3 | am Gerät beobachtet (Next-Log) |
| Eigenschaften von `0xFFF3` | `0x0E`: Read, Write, Write without Response | am Gerät beobachtet (Next-Log) |
| Schreibart | Legacy und Next schreiben ohne Antwort; „Write“ (mit Bestätigung) funktioniert ebenfalls | am Gerät beobachtet (`ble rsp on`) |

## Befehle

9-Byte-Pakete, Start `7E`, Ende `EF`. Quelle: `legacy/include/LotusProtocol.h` (Next-Kopie: `next/gateway/components/light_core/include/LotusProtocol.h`). Zum Senden mit LightBlue: Service `FFF0` → Characteristic `FFF3` → Hex ohne Leerzeichen, „Write without response“.

| Befehl | Paket | Stand |
|---|---|---|
| Ein | `7E0404F00001FF00EF` | am Gerät getestet (Legacy) |
| Aus | `7E0404000000FF00EF` | am Gerät getestet (Legacy) |
| Farbe R,G,B | `7E070503` RR GG BB `10EF` (`10` wie in der Original-App) | am Gerät getestet (Legacy) |
| Helligkeit 0–100 | `7E0401` BB `FFFFFF00EF` (BB hex, z. B. `64` = 100 %) | am Gerät getestet (Legacy) |
| Musikmodus an/aus | `7E04070[1/0]FFFFFF00EF` | **ungetestet** |
| Mikrofon-EQ | `7E0503` (Modus\|0x80) `04FFFF00EF` | **ungetestet** |

Das Band bestätigt Befehle nicht auf Anwendungsebene. Die Firmware merkt nur, dass gesendet wurde (`sentRevision`), nicht, dass das Band sie ausgeführt hat.

## Verbindungsverhalten

- **Parameterwunsch:** Etwa 1,5 s nach dem Verbinden verlangt das Band per Link-Layer-Verfahren Intervall 22,5–47,5 ms, Latenz 0, Supervision-Timeout 1 s. Legacy (NimBLE-Arduino) und Next antworten mit eigenen Werten (30–50 ms, Timeout 2,56 s). Herkunft: Next-Log, NimBLE-Arduino 1.4.3 `NimBLEClient.cpp`.
- **MTU:** Legacy handelt 255 aus, Next 256. Das Band stellt nach dem Verbinden **selbst** eine MTU-Anfrage (im Legacy-Log zweites `mtu update event`).
- **Anfragen des Bands müssen beantwortet werden.** Bekommt das Band auf seine Anfrage keine Antwort, nimmt es nach dem ATT-Timeout von **30 s** keine Befehle mehr an. Die Funkverbindung bleibt dabei bestehen (RSSI lesbar, Band weiter unsichtbar). Am Gerät beobachtet mit dem Next-Gateway, solange dessen NimBLE ohne GATT-Server gebaut war (`CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=n`). Ohne GATT-Server verwirft NimBLE solche Anfragen stumm (`ble_att.c`). Abhilfe im Gateway: Peripheral-Rolle und GATT-Server an (`next/gateway/sdkconfig.defaults`). **Ob das das Haengen am Gerät behebt, ist noch nicht getestet.**
- **Pairing:** Das Band verlangt keine Kopplung (kein Pairing-Ereignis im Next-Log).

## Funk

- Der XIAO ESP32S3 (Next-Gateway) braucht seine externe Antenne. Ohne sie empfing er am Gerät rund 25 dB schwächer als ein MacBook am selben Ort; das Band wurde nicht gefunden.
