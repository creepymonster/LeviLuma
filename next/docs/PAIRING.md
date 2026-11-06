# Gateway koppeln

## Ohne individuellen Schlüssel

Die Verbindung ist dann **unverschlüsselt**. Neue Controller nimmt das Gateway nur im Kopplungsfenster an.

1. Gateway einschalten (das Kopplungsfenster ist nach dem Start `CONFIG_LEVILUMA_PAIRING_WINDOW_S` = 300 s offen) oder am XIAO die **BOOT-Taste** kurz drücken (öffnet es erneut für 300 s). Die User-LED blinkt, solange es offen ist.
2. Am Controller: **Einstellungen → Gateways**. Erkannte Gateways erscheinen automatisch unter „Gefunden, nicht gekoppelt“ (Hinweis „Kopplung offen“). Bei Bedarf **Suchen** tippen.
3. Gateway antippen → **Koppeln**.
4. Nach Erfolg öffnet sich die Gateway-Seite; das Gateway erscheint in der Übersicht. Bei „Kopplung am Gateway nicht freigegeben“ die BOOT-Taste drücken und erneut koppeln.

## Mit individuellem Schlüssel (empfohlen)

Der Schlüssel ist der ESP-NOW-LMK (16 Byte, 32 Hex-Zeichen). Er wird **nie per Funk übertragen**.

1. Schlüssel erzeugen, z. B. `openssl rand -hex 16`.
2. Am Gateway hinterlegen – entweder beim Bauen (`menuconfig` → LeviLuma Gateway → Individueller Schlüssel) oder über die serielle Konsole (115200 Baud, USB des XIAO):
   ```
   key 00112233445566778899AABBCCDDEEFF
   ```
   Das Gateway speichert ihn im NVS und startet neu. `key off` löscht ihn wieder.
3. Am Controller: **Einstellungen → Gateways** → Gateway (Hinweis „Schlüssel nötig“) antippen → Schlüssel über die Hex-Tastatur eingeben → ✓.
4. Passt der Schlüssel nicht, kann das Gateway die Anfrage nicht entschlüsseln: „Keine Antwort: Schlüssel prüfen“. Der eingegebene Schlüssel wird dann verworfen.

Mit Schlüssel ist kein Kopplungsfenster nötig – der Schlüssel ist der Nachweis.

## Verwalten

- **Anzeigename:** Gateway-Seite → *Name*. Der Name wird im Controller gespeichert (leer = Name des Gateways). Den Namen im Gateway selbst ändert die Konsole: `name Wohnzimmer`.
- **Status:** Die Gateway-Seite zeigt ID, Firmware, online/offline (zuletzt gesehen), Band verbunden, Lampenzustand und Schlüssel ja/nein.
- **Abgleich:** *Abgleich* fragt Zustand und Kalibrierung neu ab (passiert sonst automatisch).
- **Entkoppeln:** *Entkoppeln* sendet `UNPAIR` an das Gateway und löscht Kopplung, Schlüssel und Zigbee-Zuordnung im Controller sofort (auch wenn das Gateway nicht erreichbar ist). Am Gateway löscht `unpair all` alle Kopplungen.
- **Konsole des Gateways:** `status` zeigt ID, Name, Schlüssel ja/nein, gekoppelte Controller, Kalibrierung, gemerktes Band und Bandzustand.
- **Band des Gateways:** Jedes Gateway steuert genau ein Band. Ohne gemerktes Band verbindet es sich mit dem stärksten passenden Band (ELK/MELK/LEDBLE) und merkt sich dessen BLE-Adresse dauerhaft, sobald die Verbindung steht (Log: „Band … dauerhaft gemerkt“). Danach verbindet es nur noch dieses Band, auch wenn andere passende Bänder stärker empfangen werden (Log: „gemerktes Band … gefunden, verbinde erneut“ bzw. „… nicht gefunden“). Bei mehreren Bändern das Gateway beim ersten Start dicht an sein Band legen. `band` zeigt das gemerkte Band, `band forget` vergisst es (mit Neustart), danach wird neu gewählt.
- **Funkdiagnose am Gateway:** Das Log zeigt alle 30 s, wie viele ESP-NOW-Rahmen seit dem Start empfangen wurden und wie viele Antworten ohne MAC-Bestätigung blieben (auch in `status`). `ble off` schaltet Bluetooth für einen Test ganz ab (bleibt über Neustarts, Band wird nicht verbunden), `ble on` wieder ein. Damit lässt sich prüfen, ob die gemeinsame Nutzung des Funkteils durch BLE und ESP-NOW den Empfang stört.

## Grenzen

- Bis zu 8 gekoppelte Gateways je Controller, bis zu 4 Controller je Gateway.
- Kanal und PMK müssen auf allen Geräten gleich sein (`menuconfig` → LeviLuma ESP-NOW).
