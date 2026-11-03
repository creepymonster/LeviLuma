# Testauswertung LeviLuma Next

> **Automatisch erzeugt – nicht von Hand ändern.** Erzeugt von `tests/tools/gen_report.py` aus den Ergebnissen des abschließenden CI-Laufs (`.github/workflows/tests.yml`, Job *report*). Lokale Testläufe und Entwicklungsschritte ändern diese Datei nicht.

## Gesamtergebnis (Host-Tests)

**NICHT AUSGEFÜHRT** – Keine Testergebnisse vorhanden (Testprogramm nicht gestartet oder Ergebnisdatei fehlt).

| Kennzahl | Wert |
|---|---|
| Tests ausgeführt | 0 |
| – bestanden | 0 |
| – fehlgeschlagen | 0 |
| – übersprungen (nicht ausgeführt) | 0 |
| Assertions (Prüfungen) ausgeführt | 0 |
| – davon fehlgeschlagen | 0 |

## Getesteter Stand

| | |
|---|---|
| Getesteter Commit | `unbekannt (noch keine Auswertung erzeugt)` |
| Ref | `-` (Auslöser: -) |
| Testlauf gestartet | unbekannt (Tests nicht gestartet) |

Der Bericht beschreibt den oben genannten Commit. Seine Übernahme in `next/tests/README.md` erfolgt in einem **separaten Folge-Commit** (Bot, `[skip ci]`), der nur diese Datei ändert; der Quellstand ist deshalb der genannte Commit, nicht der Berichts-Commit.

## Testumgebung und Prüfverfahren

| | |
|---|---|
| Betriebssystem / CMake | unbekannt (Tests nicht gestartet) |
| Compiler (g++) | unbekannt |
| Sanitizer (im Testprogramm aktiv) | unbekannt (Tests nicht gestartet) |
| Compiler-Warnungen | `-Wall -Wextra -Werror` (Warnung = Build-Fehler) |
| Schritt *Build* | nicht gelaufen |
| Schritt *Testlauf* | nicht gelaufen |
| Schritt *Legacy-Kopien byte-gleich* (`cmp`) | nicht gelaufen |

Die Host-Tests laufen ohne Funk-, BLE- und Display-Hardware gegen simulierte Gegenstellen.

## Host-Tests nach Bereichen

_Keine Testergebnisse vorhanden – nichts wurde ausgeführt._

## Fehlgeschlagene, übersprungene und nicht ausgeführte Prüfungen

- **Alle Host-Tests: nicht ausgeführt.** Keine Testergebnisse vorhanden (Testprogramm nicht gestartet oder Ergebnisdatei fehlt).
- Prüfung der Legacy-Kopien: nicht gelaufen.

## Firmware-Builds (getrennt von den Host-Tests)

Gebaut von `.github/workflows/next.yml` (ESP-IDF in Container). Der Status wird für denselben Commit abgefragt; ein Build ist kein Test.

**Nicht ermittelt:** Für diesen Commit liegt kein Firmware-Lauf vor (Workflow nicht ausgelöst, noch nicht gestartet oder Abfrage nicht möglich). Kein Build gilt als erfolgreich.

## Hardwaretests

**Nicht ausgeführt.** Weder Gateway noch Controller werden in der CI auf Hardware geflasht oder getestet; kein Punkt hier gilt als bestanden. Offene Hardwaretests laut `next/README.md` (Abschnitt „Offene Hardwaretests“, automatisch übernommen):

1. Display: Ausrichtung/Spiegelung (`LEVILUMA_LCD_ROT_*`), Farben (RGB/BGR, Inversion), SPI-Takt.
2. Touch: Rohbereiche, Drei-Punkt-Kalibrierung, gemeinsamer SPI-Bus mit dem Display unter Last.
3. ESP-NOW zwischen C5 und S3 auf Kanal 1 (C5 Dualband), Reichweite, Wiederholungen.
4. Schlüssel: Kopplung mit richtigem/falschem Schlüssel; ob unverschlüsselte Unicasts eines verschlüsselt eingetragenen Peers verworfen werden.
5. Gateway-BLE (NimBLE-Port): Scan, Verbindung, Schreiben ohne Antwort an 0xFFF3, Wiederverbindung, Koexistenz mit ESP-NOW.
6. Musikmodus und Mikrofon-EQ (schon in Legacy ungetestet; in Next ohne Bedienelement, nur im Protokoll).
7. Zigbee: Beitritt zur FRITZ!Box 5690 Pro, Darstellung mehrerer Endpoints, Farbbefehle (xy/Hue/Farbtemperatur), Koexistenz Zigbee + ESP-NOW auf dem C5.
8. Kalibrierassistent am Band (Vorschau-Drosselung 400 ms).
9. XIAO: LED-Polarität, BOOT-Taste als Kopplungstaste im Betrieb.
