# LeviLuma Next - ESP-NOW-Diagnosegeraet (Seeed XIAO ESP32S3): passiver Empfang, Ausgabe im seriellen Monitor.
# Eigenstaendig bau- und flashbar. Bauen: idf.py set-target esp32s3 && idf.py build
cmake_minimum_required(VERSION 3.16)

# Gemeinsames Protokoll aus next/shared (keine eigene Kopie). Es liefert auch die Kconfig LEVILUMA_ESPNOW_CHANNEL.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../shared/protocol")

# Nur die benoetigten Komponenten bauen (main und deren Abhaengigkeiten): kein BLE, Zigbee, LVGL, HTTP/MQTT im Build.
# next/tools/check_debug_build.py prueft das.
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(leviluma_debug)
