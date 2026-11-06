# Zigbee (optional, FRITZ!Box 5690 Pro)

**Standard: aus.** Ohne `CONFIG_LEVILUMA_ZIGBEE_ENABLED` enthält der Controller keinen Zigbee-Code: keine Komponente `esp-zigbee-lib`, keine Initialisierung, keine Tasks, keine Endpoints, kein Menüeintrag, keine Einstellungsseite (`zigbee_bridge.cpp` und `ui_zigbee.cpp` werden nicht übersetzt).

## Aktivieren

```sh
cd controller
idf.py -B build-zigbee -D SDKCONFIG=build-zigbee/sdkconfig \
       -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.zigbee" set-target esp32c5 build
```

`sdkconfig.defaults.zigbee` setzt `CONFIG_LEVILUMA_ZIGBEE_ENABLED`, `CONFIG_ZB_ENABLED`, Endgerät (`CONFIG_ZB_ZED`), nativen 802.15.4-Funk und Software-Koexistenz. Bibliothek: esp-zigbee-lib 2.0.5 (Espressif-eigener Stack, Commit `e2fdf4a`), nur die ESP32-C5-Bibliotheken werden geholt.

## Aufbau

- **Ein Zigbee-Light-Endpoint je gekoppeltem Gateway**: HA *Color Dimmable Light* mit On/Off, Level Control und Color Control (Fähigkeiten: Hue/Saturation, CIE xy, Farbtemperatur). Hersteller/Modell im Basic Cluster: `LeviLuma` / `LeviLuma-C5` (Kconfig).
- **Zuordnung Gateway-ID ↔ Endpoint-ID** wird im Gateway-Register des Controllers (NVS) gespeichert. Endpoints 10…17; eine bestehende Zuordnung wird nie umnummeriert. Entfernte Zuordnungen bleiben als „abgewählt“ gemerkt und werden nicht automatisch neu vergeben.
- **Neustart bei Änderungen:** Endpoints werden beim Start des Stacks registriert. Neu gekoppelte Gateways erhalten sofort eine dauerhafte Zuordnung; wirksam wird sie nach einem Neustart (Hinweis und Knopf auf der Zigbee-Seite). Ohne gekoppeltes Gateway startet der Stack nicht.

## Zustandsführung

```
Befehl:      FRITZ!Box → Zigbee → C5 → ESP-NOW → XIAO → BLE → Leiste
Rückmeldung: XIAO → ESP-NOW (STATE) → C5 → LVGL und Zigbee-Attribute
```

- Zigbee-Schreibzugriffe (OnOff, CurrentLevel, CurrentX/Y, Hue/Saturation, Farbtemperatur) werden in ESP-NOW-Befehle umgesetzt (Level 1…254 → 1…100 %, Farbe → RGB bei voller Helligkeit).
- Die Zigbee-Attribute werden **nur aus dem bestätigten Gateway-Zustand** gesetzt (nach jeder STATE-Meldung). Lehnt das Gateway ab oder antwortet es nicht, fallen die Attribute auf den bestätigten Zustand zurück. Es gibt keine eigene Zigbee-Zustandsverwaltung.
- Musikmodus und Kalibrierung sind über Zigbee nicht steuerbar.

## FRITZ!Box koppeln

1. FRITZ!Box: **Smart Home → Geräte und Gruppen → Gerät anmelden** (Zigbee) starten.
2. Controller: **Einstellungen → Zigbee (FRITZ!Box) → Mit FRITZ!Box koppeln** (Network Steering).
3. Status „Netzwerk: verbunden“ mit PAN-ID, Kanal und Kurzadresse. Die Lichter erscheinen in der FRITZ!Box.
4. **Verlassen** führt einen lokalen Zigbee-Reset aus (Netzwerk verlassen, Zigbee-Daten löschen).

Diagnose auf derselben Seite: letztes Stack-Ereignis, Anzahl Zigbee-Befehle, Attribut-Updates, Fehler.

## Einschränkungen (nicht am Gerät geprüft)

- **Koexistenz:** Der ESP32-C5 hat ein gemeinsames 2,4-GHz-Funkteil für WLAN (ESP-NOW) und IEEE 802.15.4. Beide laufen im Zeitmultiplex (Software-Koexistenz). Espressif empfiehlt für gleichzeitigen WLAN-Betrieb die Rolle *Endgerät*; deshalb ist der C5 als nicht schlafendes Endgerät (`rx_on_when_idle`) konfiguriert. Eine Router-Rolle ist über `CONFIG_ZB_ZCZR` möglich, aber mit WLAN-Koexistenz nicht empfohlen. Latenz und Paketverluste unter Last sind nicht gemessen.
- **FRITZ!Box und mehrere Endpoints:** Ob die FRITZ!Box 5690 Pro jeden Endpoint eines Geräts als eigenes Licht darstellt, ist nicht bestätigt. Falls nur der erste Endpoint erscheint, wäre ein Gerät je Gateway nur mit mehreren Zigbee-Knoten möglich (nicht umgesetzt).
- Welche Farbbefehle die FRITZ!Box sendet (Hue/Saturation, xy oder Farbtemperatur), ist nicht bestätigt; alle drei werden unterstützt.
- Reporting: Die Attribute werden lokal gesetzt; ob die FRITZ!Box Reporting konfiguriert oder abfragt, ist am Gerät zu prüfen.
