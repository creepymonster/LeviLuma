# LeviLuma Next – Gateway (Seeed XIAO ESP32S3): BLE zum Band, ESP-NOW zum Controller.
# Bauen: idf.py set-target esp32s3 && idf.py build   (oder ESP-IDF-Erweiterung in VS Code)
cmake_minimum_required(VERSION 3.16)

# Gemeinsames Protokoll und Hilfen aus next/shared
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../shared/protocol"
    "${CMAKE_CURRENT_LIST_DIR}/../shared/common")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(leviluma_gateway)
