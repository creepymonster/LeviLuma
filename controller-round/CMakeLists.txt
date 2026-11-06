# LeviLuma Next – temporaerer runder Controller (Waveshare ESP32-S3-Touch-LCD-1.28).
# Steuert genau ein Next-Gateway per ESP-NOW. Kein BLE, keine WLAN-Verbindung, kein Zigbee, kein Webserver.
#
# Bauen: idf.py -C controller-round set-target esp32s3 && idf.py -C controller-round build
cmake_minimum_required(VERSION 3.16)

set(ENV{IDF_COMPONENT_MANAGER} 0)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/third_party.cmake)
levi_git_tag(lvgl ${LEVI_LVGL_URL} ${LEVI_LVGL_REF})
levi_git_tag(lvgl_theme_shadcn ${LEVI_THEME_URL} ${LEVI_THEME_REF})
levi_git_tag(LovyanGFX ${LEVI_LGFX_URL} ${LEVI_LGFX_REF})

# Gemeinsamer Code: Protokoll und Speicher (shared) sowie die Controller-Logik
# (GatewayRegistry, CtrlLink) des RockBase-Controllers – eingebunden, nicht kopiert.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../shared/protocol"
    "${CMAKE_CURRENT_LIST_DIR}/../shared/common"
    "${CMAKE_CURRENT_LIST_DIR}/../controller/components/controller_core"
    "${LEVI_TP_DIR}/lvgl"
    "${LEVI_TP_DIR}/lvgl_theme_shadcn"
    "${LEVI_TP_DIR}/LovyanGFX")

# Nur main und seine Abhaengigkeiten bauen: keine ungenutzten Komponenten wie bt, HTTP-Server oder MQTT
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(leviluma_controller_round)
