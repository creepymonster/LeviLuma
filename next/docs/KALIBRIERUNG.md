# Farbkalibrierung

Übernommen aus Legacy ([Branch `legacy`](https://github.com/creepymonster/LeviLuma/tree/legacy): `include/CalibCore.h`, `src/state.cpp`, Assistent aus `include/WebUi.h`). Die Datei `CalibCore.h` liegt unverändert (byte-gleich) in `next/gateway/components/light_core/include/`.

## Modell (am Code geprüft)

- **10 Zielfarben**, feste Reihenfolge (`LeviCal::TARGET`): Neutralweiß D65 (255,255,255), Warmweiß 3000K (255,197,143), Rot (255,0,0), Koralle (255,64,64), Orange (255,128,64), Gelb (255,255,0), Grün (0,255,0), Cyan (0,255,255), Blau (0,0,255), Magenta (255,0,255).
- Je Zielfarbe wird die **Ansteuerung R,G,B (0…255)** gespeichert, mit der die LED dieser Farbe entspricht → **30 Werte**, Reihenfolge Ziel 1 R,G,B, Ziel 2 R,G,B, …
- Interpolation: RBF mit Thin-Plate-Spline (`φ(r) = r² ln r`) plus linearem Anteil, ein Gleichungssystem 14×14 (Gauß mit Teilpivotisierung) je Ausgangskanal.
- Anwendung (`apply`): Farbe auf volle Helligkeit normalisieren und zusätzlich direkt auswerten; weiche Umschaltung zwischen beiden bei 30…70 % Helligkeit (Smoothstep); Ergebnis auf 0…255 begrenzt. Die Helligkeit regelt das Band selbst (natives Helligkeitspaket).
- Standard ohne gespeicherte Werte: lineare Kanal-Gains 1,000 / 0,690 / 0,940 (FastLED TypicalSMD5050) oder 30 feste Werte aus Kconfig (`CONFIG_LEVILUMA_CALIB_POINTS`, entspricht Legacy `LEVI_CALIB_POINTS`).

## Speicherort

Jedes Gateway speichert seine eigene Kalibrierung im NVS (Namensraum `levigw`, Schlüssel `cal_pts`, 30 Byte, und `cal_rev`). Die Revision steigt bei jedem Speichern/Zurücksetzen, damit der Controller Änderungen erkennt (`STATE.calib_revision`). Der Controller hält nur eine Kopie zur Anzeige.

## Ablauf am Controller

Gateway-Seite → **Kalibrierung**:

- Anzeige: „Eigene Kalibrierung aktiv (10 Punkte)“ oder „Standard (FastLED TypicalSMD5050)“ und die 30 Werte (zum Übernehmen in Kconfig).
- **Kalibrierung starten** (Gateway online, Band verbunden, eingeschaltet): 10 Schritte. Je Schritt zeigt der Bildschirm die Zielfarbe; die Regler R/G/B starten auf den Zielwerten. Die Regler senden die Farbe **ohne Kalibrierung** an das Band (`SET_STATE` mit `RAW`, höchstens alle 400 ms ans Band, wie Legacy). Die LED so einstellen, dass sie der Vorschau entspricht, dann **Übernehmen**.
- Nach Schritt 10 werden die 30 Werte mit `SET_CALIB` gesendet; das Gateway speichert sie, berechnet das Modell neu und antwortet mit der gespeicherten Kalibrierung. Danach wird die vorherige Farbe kalibriert wiederhergestellt.
- **Abbrechen** stellt die vorherige Farbe wieder her, ohne zu speichern.
- **Zurücksetzen** löscht die gespeicherte Kalibrierung (`RESET_CALIB`), es gilt wieder der Standard.

Hinweis: Die Farbvorschau auf dem C5-Display ist nicht farbkalibriert; maßgeblich ist der Vergleich mit einer Referenz (wie in Legacy mit dem Browser-Bildschirm).
