# LeviLuma ESP-NOW-Protokoll (Version 1)

Gemeinsame Implementierung: `next/shared/protocol/` (`leviluma_proto.h/.c`, `leviluma_link.h/.c`).
Controller und Gateway übersetzen exakt diese Dateien; Host-Tests in `next/tests/test_protocol.cpp`.

## Grundsätze

- **Autoritativ ist das Gateway.** Es hält den LightState seiner Leiste. Der Controller sendet Befehle und zeigt nur den Zustand an, den das Gateway zurückmeldet. Ein gesendeter Befehl gilt nicht als ausgeführt.
- **Transport:** ESP-NOW v1, höchstens 250 Byte je Rahmen, fester 2,4-GHz-Kanal (`CONFIG_LEVILUMA_ESPNOW_CHANNEL`, Standard 1) auf beiden Seiten.
- **Byte-Reihenfolge:** Little Endian. Serialisierung byteweise mit Grenzprüfung, keine Struct-Casts.
- **Identität:** Geräte-ID = WLAN-STA-MAC (aus der eFuse-Basis-MAC abgeleitet, stabil und eindeutig). Sie ist zugleich die ESP-NOW-Adresse. Keine MAC-Adresse ist einkompiliert; Gateways werden zur Laufzeit erkannt.

## Rahmenaufbau

| Offset | Größe | Feld | Bedeutung |
|---|---|---|---|
| 0 | 2 | `magic` | `'L' 'V'` (0x4C 0x56) |
| 2 | 1 | `version` | 1; andere Versionen werden abgelehnt (`NACK VERSION` bei Unicast) |
| 3 | 1 | `type` | Nachrichtentyp (unten) |
| 4 | 1 | `flags` | Bit 0 `RESPONSE` (Antwort auf `ref_seq`), Bit 1 `NOTIFY` (unaufgefordert) |
| 5 | 1 | `len` | Länge der Nutzdaten (0…234) |
| 6 | 2 | `seq` | Sequenznummer des Absenders, je Sitzung fortlaufend, 0 wird übersprungen |
| 8 | 2 | `ref_seq` | bei Antworten: `seq` der Anfrage, sonst 0 |
| 10 | 4 | `session` | Zufallswert je Start des Absenders (erkennt Neustarts) |
| 14 | 2 | `crc` | CRC-16/CCITT-FALSE über Kopf (mit `crc` = 0) und Nutzdaten |
| 16 | `len` | Nutzdaten | je Typ, exakt aufgebraucht (Restbytes = Fehler) |

Namen: 1 Byte Länge + höchstens 24 Byte (UTF-8, ohne Nullterminator). Steuerzeichen werden beim Lesen ersetzt.

## Nachrichtentypen

| Typ | Wert | Richtung | Nutzdaten | Antwort |
|---|---|---|---|---|
| `DISCOVER` | 0x01 | C → Broadcast | Controller-ID[6], Name | `ANNOUNCE` (Broadcast) |
| `ANNOUNCE` | 0x02 | G → Broadcast | Gateway-ID[6], FW u16, `caps` u8, Zustandsrevision u32, Name | – |
| `PAIR_REQ` | 0x10 | C → G | Controller-ID[6], Name | `PAIR_RSP` oder `NACK` |
| `PAIR_RSP` | 0x11 | G → C | Ergebnis u8, Gateway-ID[6], Name | – |
| `UNPAIR` | 0x12 | C → G | Controller-ID[6] | `ACK` |
| `GET_STATE` | 0x20 | C → G | – | `STATE` |
| `SET_STATE` | 0x21 | C → G | `mask`, power, brightness, r, g, b, music (je u8) | `STATE` (bestätigter Zustand) oder `NACK` |
| `STATE` | 0x22 | G → C | revision u32, sent_revision u32, power i8, brightness, r, g, b, flags, calib_revision u16 | – |
| `GET_CALIB` | 0x30 | C → G | – | `CALIB` |
| `SET_CALIB` | 0x31 | C → G | 30 Werte | `CALIB` oder `NACK STORAGE` |
| `RESET_CALIB` | 0x32 | C → G | – | `CALIB` oder `NACK STORAGE` |
| `CALIB` | 0x33 | G → C | custom u8, calib_revision u16, 30 Werte | – |
| `ACK` | 0x7E | G → C | – | – |
| `NACK` | 0x7F | G → C | Fehlercode u8 | – |

`caps` im ANNOUNCE: Bit 0 Schlüssel nötig, Bit 1 Kopplungsfenster offen, Bit 2 Band (BLE) verbunden, Bit 3 mit mindestens einem Controller gekoppelt.

`SET_STATE.mask`: Bit 0 Ein/Aus, Bit 1 Helligkeit (1…100), Bit 2 Farbe **mit** Kalibrierung (wie Legacy `setRGB`, beendet Musik), Bit 3 Farbe **ohne** Kalibrierung (wie Legacy `setRaw`, für die Kalibriervorschau), Bit 4 Musik. Bit 2 und 3 schließen sich aus. Eine angenommene Anfrage erhöht die Revision genau einmal.

`STATE.flags`: Bit 0 Musik, Bit 1 rohe Farbe, Bit 2 Band verbunden. `power` −1 = unbekannt (nach Gateway-Start, wie Legacy). `sent_revision` = bis zu dieser Revision sind alle BLE-Befehle gesendet (Sendebestätigung, keine Ausführungsbestätigung des Bandes).

Fehlercodes: 1 nicht gekoppelt, 2 ungültige Anfrage, 3 nicht unterstützt, 4 beschäftigt, 5 Speicherfehler, 6 Kopplung nicht freigegeben, 7 Version, 8 Wert außerhalb, 9 voll.

## Zuverlässigkeit

- **Wiederholung (Controller):** Jede Anfrage wartet `CONFIG_LEVILUMA_REQUEST_TIMEOUT_MS` (250 ms) und wird dann wiederholt, mit linear wachsender Wartezeit (1×, 2×, 3× …), insgesamt `CONFIG_LEVILUMA_REQUEST_TRIES` (4) Sendungen. Danach gilt sie als gescheitert; die Anzeige meldet „Keine Antwort“ und der Zustand wird beim nächsten Kontakt neu abgefragt.
- **Duplikate (Gateway):** Gleiche Sitzung und gleiche Sequenznummer → die gespeicherte Antwort wird erneut gesendet, der Befehl **nicht** erneut ausgeführt. Ältere Sequenzen werden verworfen. Neue Sitzung (Controller-Neustart) beginnt neu.
- **Senderegel je Gateway (Controller, beide Varianten):** Höchstens **eine** Anfrage ist unterwegs (alle Typen). Wiederholungen nutzen dieselbe Sitzung und Sequenznummer und erzeugen keine zusätzlichen Einträge; nach dem endgültigen Timeout wird ein Befehl nie erneut eingereiht. Zusätzlich wartet höchstens **ein** Steuerbefehl: Ein neuer Lichtbefehl (`SET_STATE`) ersetzt den wartenden **vollständig** – kein Zusammenführen von Feldern, keine FIFO-Schlange (A unterwegs, dann B, C, D → nach A folgt ausschließlich D). Jedes Gateway wird unabhängig behandelt. Dadurch haben nacheinander gesendete Befehle dieselbe Wirkung wie am Gateway angewendet: Ein älteres wartendes „Musik an“ kann einen späteren kalibrierten Farbbefehl nicht mehr überstimmen. Ein einzelner `SET_STATE` darf mehrere Felder enthalten; das Gateway wendet sie in fester Reihenfolge an (Ein/Aus, Helligkeit, Farbe – kalibrierte Farbe beendet die Musik –, danach ein ausdrückliches Musikfeld).
- **Abfragen überholen nichts:** `GET_STATE`/`GET_CALIB` werden nur als zusammengefasster Bedarf (ein Merker je Art) vorgemerkt und erst gesendet, wenn nichts mehr offen ist und kein Befehl wartet (Reihenfolge: Befehl, Zustand, Kalibrierung). Andernfalls würde das Gateway die Wiederholung eines verlorenen `SET_STATE` wegen der neueren Sequenznummer als veraltet verwerfen. Eine `STATE`-Antwort erledigt den Zustandsbedarf.
- **Explizite Vorgänge:** `SET_CALIB`/`RESET_CALIB` sind keine ersetzbaren Lichtbefehle. Sie werden nie von einem Lichtbefehl verdrängt (der Lichtbefehl wird mit `LVP_ERR_BUSY` abgewiesen), ein zweiter Kalibriervorgang wird abgewiesen, solange einer wartet. Sie ersetzen höchstens einen wartenden Lichtbefehl. Bleibt die Antwort aus, meldet der Controller „Keine Antwort“ (nicht „Gespeichert“) und liest die Kalibrierung neu.
- **Ablehnung:** Auf `NACK` zu `SET_STATE` fragt der Controller den Zustand neu ab und zeigt ihn an.

## Erkennung, Online-Status, Abgleich

- Gateways senden alle `CONFIG_LEVILUMA_BEACON_MS` (2 s) ein `ANNOUNCE` per Broadcast; der Controller sendet beim Start, alle 15 s und auf Knopfdruck `DISCOVER`.
- Offline nach drei verpassten Beacons (`3 × Beacon + 500 ms`). Jeder gültige Rahmen gilt als Lebenszeichen.
- Der Controller fragt `GET_STATE` an, wenn: Gateway neu online, andere Gateway-Sitzung (Neustart), Revision im Beacon ≠ eigene Kopie, BLE-Status im Beacon ≠ Kopie, oder ein Befehl ohne Antwort blieb.
- Das Gateway meldet Zustandsänderungen (Befehle anderer Controller, BLE-Verbindung, `sent_revision`) unaufgefordert mit `STATE`+`NOTIFY` an alle gekoppelten Controller (höchstens alle 100 ms). Veraltete Meldungen (gleiche Sitzung, kleinere Revision) verwirft der Controller.

## Sicherheit

- Verschlüsselung ausschließlich mit dem dokumentierten ESP-NOW-Mechanismus: CCMP mit einem Local Master Key (LMK, 16 Byte) je Peer; der Primary Master Key (PMK, `CONFIG_LEVILUMA_ESPNOW_PMK`) verschlüsselt die LMK im Stack. Keine eigene Kryptografie.
- **Individueller Gateway-Schlüssel (optional) = LMK.** Er wird am Gateway (Kconfig `CONFIG_LEVILUMA_GW_KEY` oder Konsole `key …`) und am Controller (Eingabe beim Koppeln) getrennt hinterlegt und **nie übertragen**. Nachweis beim Koppeln: Ein `PAIR_REQ` mit falschem Schlüssel kann das Gateway nicht entschlüsseln; der Controller erhält keine Antwort und verwirft den vorläufigen Schlüssel.
- Broadcasts (`DISCOVER`, `ANNOUNCE`) sind nie verschlüsselt (ESP-NOW unterstützt keine verschlüsselten Broadcasts) und enthalten keine Geheimnisse.
- Ohne Schlüssel ist die Verbindung unverschlüsselt; neue Controller werden dann nur im Kopplungsfenster angenommen (nach dem Start und nach Druck auf die BOOT-Taste, Standard 300 s).
- Steuerbefehle per Broadcast werden ignoriert; Befehle nicht gekoppelter Absender werden mit `NACK NOT_PAIRED` abgelehnt.
- **Offen (Hardwaretest):** Ob ESP-NOW unverschlüsselte Unicasts von einem verschlüsselt eingetragenen Peer verwirft, ist in der ESP-IDF-Dokumentation nicht ausdrücklich beschrieben. Davon hängt ab, ob ein Angreifer ohne Schlüssel ein Gateway mit Schlüssel per Klartext-`PAIR_REQ` koppeln könnte. Siehe `next/README.md`, offene Tests.
- Schlüssel liegen im NVS im Klartext. Für Produktivgeräte NVS-Verschlüsselung/Flash-Verschlüsselung erwägen.

## Abläufe

```
Kopplung:   C --DISCOVER(bc)--> G        G trägt C als Peer ein (mit LMK, falls Schlüssel)
            C <-ANNOUNCE(bc)--- G
            C --PAIR_REQ------> G        (verschlüsselt, falls Schlüssel)
            C <-PAIR_RSP------- G        G speichert C im NVS, C speichert G (und Schlüssel) im NVS
            C --GET_STATE/GET_CALIB--> G ...

Befehl:     C --SET_STATE(seq n)--> G    G prüft, wendet an, Revision +1
            C <-STATE(ref n)------- G    Anzeige übernimmt diesen Zustand
            G --BLE--> Leiste            G meldet sent_revision später per STATE+NOTIFY
```
