# LeviLuma Next – Controller (RockBase NM-CYD-C5, ESP32-C5): LVGL-Oberflaeche, ESP-NOW zu den
# Gateways, optional Zigbee (CONFIG_LEVILUMA_ZIGBEE_ENABLED, Standard aus).
#
# Bauen ohne Zigbee:  idf.py set-target esp32c5 && idf.py build
# Bauen mit Zigbee:   idf.py -B build-zigbee -D SDKCONFIG=build-zigbee/sdkconfig \
#                       -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.zigbee" set-target esp32c5 build
cmake_minimum_required(VERSION 3.16)

# Abhaengigkeiten werden ueber feste Git-Versionen geholt (siehe cmake/third_party.cmake)
set(ENV{IDF_COMPONENT_MANAGER} 0)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/third_party.cmake)
levi_git_tag(lvgl ${LEVI_LVGL_URL} ${LEVI_LVGL_REF})
levi_git_tag(lvgl_theme_shadcn ${LEVI_THEME_URL} ${LEVI_THEME_REF})

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../shared/protocol"
    "${CMAKE_CURRENT_LIST_DIR}/../shared/common"
    "${LEVI_TP_DIR}/lvgl"
    "${LEVI_TP_DIR}/lvgl_theme_shadcn")

levi_zigbee_requested(LEVI_WITH_ZIGBEE)
if(LEVI_WITH_ZIGBEE)
  levi_git_zigbee()
  list(APPEND EXTRA_COMPONENT_DIRS "${LEVI_TP_DIR}/esp-zigbee-sdk/components/esp-zigbee-lib")
  message(STATUS "LeviLuma: Zigbee aktiv (esp-zigbee-lib eingebunden)")
else()
  message(STATUS "LeviLuma: Zigbee aus")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Fuer main/CMakeLists.txt: CONFIG-Werte sind bei der Aufloesung der Abhaengigkeiten noch nicht bekannt
idf_build_set_property(LEVI_WITH_ZIGBEE ${LEVI_WITH_ZIGBEE})
project(leviluma_controller)
