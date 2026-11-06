# LeviLuma Next – nativer LVGL-Desktop-Simulator (SDL2) fuer beide Next-Controller.
#
#   cmake -S simulator -B simulator/build
#   cmake --build simulator/build
#   simulator/build/bin/leviluma-sim-controller     (RockBase NM-CYD-C5, 320x240)
#   simulator/build/bin/leviluma-sim-round          (Waveshare Round, 240x240)
#
# Zwei Programme statt einem: Beide Firmware-Oberflaechen definieren dieselben Symbole (uiBuild,
# appView, GatewayView mit unterschiedlichem Aufbau ...). In einem Programm waeren das
# ODR-Verletzungen; getrennt bleibt der Firmware-Code unveraendert.
#
# Kein ESP-IDF, kein FreeRTOS, kein PlatformIO. Die Oberflaechen-Dateien der Firmware werden direkt
# uebersetzt (keine Kopien), ersetzt sind nur Display, Touch, Zeit, Funk, Gateway und NVS.
cmake_minimum_required(VERSION 3.20)
project(leviluma_simulator C CXX)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build-Typ" FORCE)
endif()
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20) # Firmware-UI nutzt Designated Initializers (lv_theme_shadcn_fonts_t)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_filename_component(LEVI_NEXT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(LEVI_SIM_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(LEVI_CTRL "${LEVI_NEXT}/controller")
set(LEVI_ROUND "${LEVI_NEXT}/controller-round")

if(DEFINED ENV{IDF_PATH})
  message(STATUS "LeviLuma-Simulator: IDF_PATH ist gesetzt, wird aber nicht benutzt.")
endif()

include(cmake/deps.cmake)
include(cmake/kconfig.cmake)

# ---- LVGL-Konfiguration mit der Firmware abgleichen (Fonts) ----
file(READ "${LEVI_SIM_DIR}/config/lv_conf.h" LEVI_LVCONF)
foreach(df "${LEVI_CTRL}/sdkconfig.defaults" "${LEVI_ROUND}/sdkconfig.defaults")
  file(STRINGS "${df}" fonts REGEX "^CONFIG_LV_FONT_MONTSERRAT_[0-9]+=y")
  foreach(f IN LISTS fonts)
    string(REGEX REPLACE "^CONFIG_(LV_FONT_MONTSERRAT_[0-9]+)=y" "\\1" n "${f}")
    if(NOT LEVI_LVCONF MATCHES "#define ${n} 1")
      message(FATAL_ERROR "LeviLuma-Simulator: ${df} aktiviert ${n}, config/lv_conf.h nicht. Bitte nachziehen.")
    endif()
  endforeach()
endforeach()

# ---- sdkconfig.h je Variante aus den Kconfig-Dateien der Firmware ----
set(LEVI_GEN "${CMAKE_BINARY_DIR}/generated")
levi_sim_sdkconfig("${LEVI_GEN}/controller/sdkconfig.h"
  KCONFIGS "${LEVI_NEXT}/shared/protocol/Kconfig" "${LEVI_CTRL}/main/Kconfig.projbuild"
  DEFAULTS "${LEVI_CTRL}/sdkconfig.defaults")
levi_sim_sdkconfig("${LEVI_GEN}/round/sdkconfig.h"
  KCONFIGS "${LEVI_NEXT}/shared/protocol/Kconfig" "${LEVI_ROUND}/main/Kconfig.projbuild"
  DEFAULTS "${LEVI_ROUND}/sdkconfig.defaults")

# ---- Gemeinsamer Teil: Protokoll, Controller- und Gateway-Logik, Simulator-Kern ----
if(MSVC)
  set(LEVI_WARN /W3)
else()
  set(LEVI_WARN -Wall -Wno-unused-function -Wno-missing-field-initializers)
endif()

add_library(levi_sim_core STATIC
  # gemeinsamer Next-Code (unveraendert)
  "${LEVI_NEXT}/shared/protocol/src/leviluma_proto.c"
  "${LEVI_NEXT}/shared/protocol/src/leviluma_link.c"
  "${LEVI_NEXT}/shared/common/src/kv_store.cpp"
  "${LEVI_NEXT}/shared/common/src/levi_key.c"
  "${LEVI_CTRL}/components/controller_core/src/gateway_registry.cpp"
  "${LEVI_CTRL}/components/controller_core/src/ctrl_link.cpp"
  "${LEVI_CTRL}/components/controller_core/src/touch_cal.cpp"
  "${LEVI_NEXT}/gateway/components/light_core/src/light_model.cpp"
  "${LEVI_NEXT}/gateway/components/light_core/src/calib_store.cpp"
  "${LEVI_NEXT}/gateway/components/light_core/src/pairing_table.cpp"
  "${LEVI_NEXT}/gateway/components/light_core/src/gateway_handler.cpp"
  # Simulator
  src/sim_clock.cpp
  src/sim_air.cpp
  src/sim_controller.cpp
  src/sdl_backend.cpp
  src/control_panel.cpp
  src/sim_script.cpp
  src/simulator_app.cpp
  mocks/mock_gateway.cpp)
target_include_directories(levi_sim_core PUBLIC
  "${LEVI_SIM_DIR}/src"
  "${LEVI_SIM_DIR}/mocks"
  "${LEVI_NEXT}/shared/protocol/include"
  "${LEVI_NEXT}/shared/common/include"
  "${LEVI_CTRL}/components/controller_core/include"
  "${LEVI_NEXT}/gateway/components/light_core/include")
target_link_libraries(levi_sim_core PUBLIC levi_lvgl_theme levi_lvgl ${LEVI_SDL_TARGET})
target_compile_options(levi_sim_core PRIVATE ${LEVI_WARN})
if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(levi_sim_core PUBLIC Threads::Threads m)
endif()

# Ausgabe immer nach build/bin (auch bei Multi-Config-Generatoren wie Visual Studio)
function(levi_sim_exe name)
  set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<0:>")
  target_compile_options(${name} PRIVATE ${LEVI_WARN})
  target_link_libraries(${name} PRIVATE levi_sim_core)
endfunction()

# ---- RockBase-Controller: Original-Oberflaeche aus controller/main ----
# Ohne Zigbee (wie der Standard-Build): ui_zigbee.cpp und zigbee_bridge.cpp gehoeren nicht dazu.
set(LEVI_CTRL_UI
  "${LEVI_CTRL}/main/ui_common.cpp"
  "${LEVI_CTRL}/main/ui_home.cpp"
  "${LEVI_CTRL}/main/ui_detail.cpp"
  "${LEVI_CTRL}/main/ui_settings.cpp"
  "${LEVI_CTRL}/main/ui_calib.cpp"
  "${LEVI_CTRL}/main/ui_touchcal.cpp")
add_executable(leviluma-sim-controller
  ${LEVI_CTRL_UI}
  targets/controller/main.cpp
  targets/controller/app_adapter.cpp
  targets/controller/port_adapter.cpp)
# Reihenfolge: erzeugte sdkconfig.h und Platzhalter vor allem anderen, dann main/ der Firmware
target_include_directories(leviluma-sim-controller PRIVATE "${LEVI_GEN}/controller" "${LEVI_SIM_DIR}/shim" "${LEVI_CTRL}/main")
levi_sim_exe(leviluma-sim-controller)

# ---- Round Controller: Original-Oberflaeche aus controller-round/main ----
add_executable(leviluma-sim-round
  "${LEVI_ROUND}/main/ui.cpp"
  "${LEVI_ROUND}/components/round_core/src/round_core.cpp"
  targets/round/main.cpp
  targets/round/app_adapter.cpp
  targets/round/port_adapter.cpp)
target_include_directories(leviluma-sim-round PRIVATE "${LEVI_GEN}/round" "${LEVI_ROUND}/main" "${LEVI_ROUND}/components/round_core/include")
levi_sim_exe(leviluma-sim-round)

# ---- Tests (CTest) ----
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
