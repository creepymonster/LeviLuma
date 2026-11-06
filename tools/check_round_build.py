#!/usr/bin/env python3
"""Prueft einen Build von controller-round auf unerwuenschte Funkdienste.

Aufruf: tools/check_round_build.py <build-ordner>
Geprueft werden sdkconfig (BLE, Zigbee/802.15.4, OpenThread), die Liste der Komponenten
(kein bt, esp-zigbee-lib, esp_http_server, mqtt) und die Quelltexte von main/ (keine Aufrufe
zum Verbinden, Scannen, Access-Point-Betrieb oder TCP/IP-Initialisierung).
"""
import json, os, re, sys

build = sys.argv[1] if len(sys.argv) > 1 else "build"
desc = json.load(open(os.path.join(build, "project_description.json")))
proj = desc["project_path"]
cfg = open(os.path.join(build, "config", "sdkconfig.h")).read()
errors = []

for sym in ["CONFIG_BT_ENABLED", "CONFIG_ZB_ENABLED", "CONFIG_IEEE802154_ENABLED", "CONFIG_OPENTHREAD_ENABLED",
            "CONFIG_LEVILUMA_ZIGBEE_ENABLED"]:
    if re.search(r"#define %s 1\b" % sym, cfg):
        errors.append("sdkconfig: %s ist aktiv" % sym)

for comp in ["bt", "esp-zigbee-lib", "esp_http_server", "mqtt", "esp_https_server"]:
    if comp in desc["build_components"]:
        errors.append("Komponente im Build: %s" % comp)

forbidden = ["esp_wifi_connect", "esp_wifi_scan_start", "WIFI_MODE_AP", "WIFI_MODE_APSTA", "esp_netif_init",
             "esp_netif_create_default", "httpd_start", "esp_mqtt", "esp_bt_", "nimble_port_init", "esp_zigbee"]
main = os.path.join(proj, "main")
for f in sorted(os.listdir(main)):
    if f.endswith((".c", ".cpp", ".h")):
        text = open(os.path.join(main, f)).read()
        for word in forbidden:
            if re.search(r"^[^/]*\b%s" % re.escape(word), text, re.M):
                errors.append("%s: %s" % (f, word))

if errors:
    print("FEHLER:\n  " + "\n  ".join(errors))
    sys.exit(1)
print("OK: kein BLE, kein Zigbee/802.15.4, kein HTTP/MQTT, keine WLAN-Verbindung/-Suche/AP, kein esp_netif.")
print("    Komponenten: %d, esp_wifi nur fuer ESP-NOW." % len(desc["build_components"]))
