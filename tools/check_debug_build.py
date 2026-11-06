#!/usr/bin/env python3
"""Prueft einen Build von debug (ESP-NOW-Diagnosegeraet) auf die zugesicherte passive Arbeitsweise.

Aufruf: tools/check_debug_build.py <build-ordner>

Geprueft werden
  - sdkconfig: kein BLE, kein Zigbee/802.15.4, kein OpenThread;
  - Komponenten: keine Oberflaeche (lvgl, esp_lcd), kein HTTP/MQTT, kein Zigbee;
  - Quelltexte von main/ und components/debug_core: keine Sendeaufrufe (esp_now_send, esp_now_add_peer, Rahmen-Injektion),
    keine Verbindung/Suche/AP, kein NVS-Zugriff;
  - Objektdateien dieses Projekts: keine ungebundenen Verweise auf diese Funktionen (nm, falls vorhanden).
Das beweist nicht, dass das Funkteil des Treibers nichts sendet (z. B. MAC-ACK fuer an die eigene MAC gerichtete Rahmen);
es sichert nur, dass die Anwendung selbst nichts sendet, koppelt oder speichert.
"""
import glob, json, os, re, shutil, subprocess, sys

build = sys.argv[1] if len(sys.argv) > 1 else "build"
desc = json.load(open(os.path.join(build, "project_description.json")))
proj = desc["project_path"]
cfg = open(os.path.join(build, "config", "sdkconfig.h")).read()
errors = []

for sym in ["CONFIG_BT_ENABLED", "CONFIG_ZB_ENABLED", "CONFIG_IEEE802154_ENABLED", "CONFIG_OPENTHREAD_ENABLED",
            "CONFIG_LEVILUMA_ZIGBEE_ENABLED"]:
    if re.search(r"#define %s 1\b" % sym, cfg):
        errors.append("sdkconfig: %s ist aktiv" % sym)

for comp in ["esp-zigbee-lib", "lvgl", "lvgl__lvgl", "esp_lcd_touch", "mqtt", "esp_http_server", "esp_https_server"]:
    if any(c == comp or c.endswith("__" + comp) for c in desc["build_components"]):
        errors.append("Komponente im Build: %s" % comp)

# Aufrufe, die senden, koppeln, verbinden oder speichern
forbidden = ["esp_now_send", "esp_now_add_peer", "esp_now_mod_peer", "esp_now_set_pmk", "esp_wifi_80211_tx", "esp_wifi_connect",
             "esp_wifi_scan_start", "WIFI_MODE_AP", "WIFI_MODE_APSTA", "esp_netif_create_default", "httpd_start", "esp_mqtt",
             "esp_bt_", "nimble_port_init", "esp_zigbee", "lv_init", "nvs_flash_init", "nvs_open", "nvs_flash_erase",
             "esp_wifi_set_config", "esp_wifi_start_ftm", "esp_now_set_wake_window", "esp_wifi_set_csi"]
dirs = [os.path.join(proj, "main"), os.path.join(proj, "components", "debug_core")]
for d in dirs:
    for root, _, files in os.walk(d):
        for f in sorted(files):
            if f.endswith((".c", ".cpp", ".h")):
                path = os.path.join(root, f)
                for n, line in enumerate(open(path, encoding="utf-8"), 1):
                    code = line.split("//")[0]
                    for word in forbidden:
                        if re.search(r"\b%s" % re.escape(word), code):
                            errors.append("%s:%d: %s" % (os.path.relpath(path, proj), n, word))

# Verweise in den eigenen Objektdateien
nm = shutil.which("xtensa-esp32s3-elf-nm") or shutil.which("nm")
objs = glob.glob(os.path.join(build, "esp-idf", "main", "CMakeFiles", "*", "*.obj")) + \
       glob.glob(os.path.join(build, "esp-idf", "debug_core", "CMakeFiles", "*", "src", "*.obj"))
checked = 0
if nm and objs:
    syms = [w for w in forbidden if re.fullmatch(r"[a-z_0-9]+", w) and not w.endswith("_")]
    for o in objs:
        out = subprocess.run([nm, "-u", o], capture_output=True, text=True).stdout
        checked += 1
        for s in syms:
            if re.search(r"\b%s\b" % re.escape(s), out):
                errors.append("%s: ungebundener Verweis auf %s" % (os.path.relpath(o, build), s))
else:
    print("HINWEIS: nm oder Objektdateien nicht gefunden - Symbolpruefung uebersprungen.")

if errors:
    print("FEHLER:\n  " + "\n  ".join(errors))
    sys.exit(1)
print("OK: Quelltexte ohne Sende-, Kopplungs-, Verbindungs- und NVS-Aufrufe; kein BLE, Zigbee, LVGL, HTTP/MQTT.")
print("    Objektdateien geprueft: %d; Komponenten: %d." % (checked, len(desc["build_components"])))
