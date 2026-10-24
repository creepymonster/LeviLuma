# LeviLuma V3 – Waveshare ESP32-S3 Touch LCD 1.28

PlatformIO / Arduino / LovyanGFX / LVGL 9 / NimBLE-Arduino.

## Start
1. `include/Config.example.h` nach `include/Config.h` kopieren und WLAN eintragen.
2. Projektordner in VS Code/PlatformIO öffnen.
3. Build, Upload, Serial Monitor (115200).
4. Weboberfläche: `http://leviluma.local/` oder IP des ESP32.

## Änderungen in V3
- Helles Webdesign mit Glühbirne, weißer Karte auf Grau, Statuschip und schwarzem Power-Button.
- LCD: 216px-Ring (16px), Ringöffnung rechts, weißer Knopf, 108px-Mitte mit Halo,
  32px-Prozentzahl, Statuspunkt bei y=34 und Farbmarkierungen bei y=200.
- Einheitliche Palette: E24F62, E7A23B, 5FC36B, 52B4D1, 8B5CF6.
- LVGL-Schriften aktiviert, RGB565-Puffer 4-Byte-ausgerichtet.
- BLE-Übertragung hält Revision offen, wenn 33-ms-Limit noch nicht erreicht ist.
- LCD aktualisiert nur bei Zustandsänderung; Touch wird einmal je Schleife gelesen.
- Weckberührung wird bis zum Loslassen unterdrückt.
- WLAN Auto-Reconnect und mDNS-Neustart; reduzierte WLAN-Sendeleistung.
- Hintergrundbeleuchtung wird erst nach dem ersten LVGL-Frame aktiviert.

## Wichtige Grenzen
- Auf realer Hardware und mit PlatformIO hier **nicht kompiliert/getestet**.
- LED-Status ist optimistisch; FFF4 lieferte keine brauchbare Bestätigung.
- `default_16MB.csv` setzt 16 MB Flash voraus; Board-Angabe vor Upload prüfen.
- DMA/Double-Buffer und Animationen sind noch nicht implementiert. Zuerst stabilen
  Display-/Touch-/BLE-Betrieb prüfen.
- Web-API ist im lokalen Netzwerk nicht authentifiziert. Nur in vertrauenswürdigem WLAN verwenden.
- Für 0 % Helligkeit ist der Power-Button vorgesehen; Regler 1–100 %.

## GPIO
Display: SCLK10 MOSI11 CS9 DC8 RESET14 BACKLIGHT2.
Touch: SDA6 SCL7 IRQ5 RESET13.

## BLE
ELK-BLEDDM 96, Service FFF0, Write FFF3.
Ein/Aus, RGB und Helligkeit (natives Paket, `Lotus::brightness`) entsprechen den am Gerät getesteten Paketen.

## Warnungsbereinigung
`LV_COLOR_DEPTH` wurde durch `LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565` ersetzt, um die im bereitgestellten Build-Log vielfach wiederholte Deprecation-Warnung zu beseitigen. Noch kein erneuter PlatformIO-Build in dieser Umgebung ausgeführt.
