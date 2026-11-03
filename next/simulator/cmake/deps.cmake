# Abhaengigkeiten des Desktop-Simulators.
#
# LVGL und lvgl_theme_shadcn: dieselben Quellen und Versionen wie die Firmware. Die Versionen werden
# nicht hier gepflegt, sondern aus next/controller/cmake/third_party.cmake gelesen (und mit
# next/controller-round/cmake/third_party.cmake verglichen). Geholt wird per FetchContent in den
# Build-Ordner des Simulators; die third_party-Ordner der Firmware bleiben unberuehrt.
#
# SDL2: zuerst eine installierte SDL2 (find_package), sonst SDL2 als Quelltext in fester Version
# (LEVI_SIM_SDL_REF) und statisch gebaut. Mit -DLEVI_SIM_FETCH_SDL=ON wird immer die feste Version genutzt.
#
# Offline: Quellordner vorgeben, z. B.
#   -DFETCHCONTENT_SOURCE_DIR_LVGL=<pfad>/lvgl -DFETCHCONTENT_SOURCE_DIR_LVGL_THEME_SHADCN=<pfad>/lvgl_theme_shadcn
#   -DFETCHCONTENT_SOURCE_DIR_SDL2=<pfad>/SDL
include(FetchContent)

# ---- Versionen aus den Firmware-Projekten ----
function(levi_sim_firmware_versions project_dir prefix)
  # third_party.cmake setzt LEVI_*-Variablen und definiert Funktionen; in diesem Funktionsscope
  # bleiben die Variablen lokal, nur die Versionen werden nach aussen gegeben.
  include("${project_dir}/cmake/third_party.cmake")
  set(${prefix}_LVGL_URL "${LEVI_LVGL_URL}" PARENT_SCOPE)
  set(${prefix}_LVGL_REF "${LEVI_LVGL_REF}" PARENT_SCOPE)
  set(${prefix}_THEME_URL "${LEVI_THEME_URL}" PARENT_SCOPE)
  set(${prefix}_THEME_REF "${LEVI_THEME_REF}" PARENT_SCOPE)
endfunction()

levi_sim_firmware_versions("${LEVI_NEXT}/controller" LEVI_CTRL)
levi_sim_firmware_versions("${LEVI_NEXT}/controller-round" LEVI_ROUND)
if(NOT LEVI_CTRL_LVGL_REF STREQUAL LEVI_ROUND_LVGL_REF OR NOT LEVI_CTRL_THEME_REF STREQUAL LEVI_ROUND_THEME_REF)
  message(FATAL_ERROR "LeviLuma-Simulator: RockBase (${LEVI_CTRL_LVGL_REF}, Theme ${LEVI_CTRL_THEME_REF}) und Round "
                      "(${LEVI_ROUND_LVGL_REF}, Theme ${LEVI_ROUND_THEME_REF}) nutzen unterschiedliche Versionen. "
                      "Der Simulator baut beide gegen dieselbe LVGL; bitte angleichen oder den Simulator erweitern.")
endif()
message(STATUS "LeviLuma-Simulator: LVGL ${LEVI_CTRL_LVGL_REF}, lvgl_theme_shadcn ${LEVI_CTRL_THEME_REF} (wie Firmware)")

# Nur Quellen holen, nicht deren eigenes CMake einbinden (SOURCE_SUBDIR ohne CMakeLists.txt)
FetchContent_Declare(lvgl
  GIT_REPOSITORY "${LEVI_CTRL_LVGL_URL}"
  GIT_TAG "${LEVI_CTRL_LVGL_REF}"
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR levi-no-cmake)
FetchContent_Declare(lvgl_theme_shadcn
  GIT_REPOSITORY "${LEVI_CTRL_THEME_URL}"
  GIT_TAG "${LEVI_CTRL_THEME_REF}"
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR levi-no-cmake)
FetchContent_MakeAvailable(lvgl lvgl_theme_shadcn)

# ---- LVGL als statische Bibliothek mit der Simulator-Konfiguration (config/lv_conf.h) ----
file(GLOB_RECURSE LEVI_LVGL_SRCS CONFIGURE_DEPENDS "${lvgl_SOURCE_DIR}/src/*.c")
add_library(levi_lvgl STATIC ${LEVI_LVGL_SRCS})
target_include_directories(levi_lvgl SYSTEM PUBLIC "${lvgl_SOURCE_DIR}" "${LEVI_SIM_DIR}/config")
target_compile_definitions(levi_lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
if(MSVC)
  target_compile_options(levi_lvgl PRIVATE /w)
else()
  target_compile_options(levi_lvgl PRIVATE -w)
endif()

file(GLOB_RECURSE LEVI_THEME_SRCS CONFIGURE_DEPENDS "${lvgl_theme_shadcn_SOURCE_DIR}/src/*.c")
add_library(levi_lvgl_theme STATIC ${LEVI_THEME_SRCS})
target_include_directories(levi_lvgl_theme SYSTEM PUBLIC "${lvgl_theme_shadcn_SOURCE_DIR}/include")
target_link_libraries(levi_lvgl_theme PUBLIC levi_lvgl)
if(MSVC)
  target_compile_options(levi_lvgl_theme PRIVATE /w)
else()
  target_compile_options(levi_lvgl_theme PRIVATE -w)
endif()

# ---- SDL2 ----
set(LEVI_SIM_SDL_URL "https://github.com/libsdl-org/SDL.git")
set(LEVI_SIM_SDL_REF "release-2.30.9")
option(LEVI_SIM_FETCH_SDL "SDL2 immer als Quelltext in fester Version bauen (statt installierter SDL2)" OFF)

if(NOT LEVI_SIM_FETCH_SDL)
  find_package(SDL2 CONFIG QUIET)
endif()
if(SDL2_FOUND AND NOT LEVI_SIM_FETCH_SDL)
  message(STATUS "LeviLuma-Simulator: installierte SDL2 ${SDL2_VERSION}")
  if(TARGET SDL2::SDL2)
    set(LEVI_SDL_TARGET SDL2::SDL2)
  else()
    set(LEVI_SDL_TARGET SDL2::SDL2-static)
  endif()
else()
  message(STATUS "LeviLuma-Simulator: SDL2 ${LEVI_SIM_SDL_REF} aus dem Quelltext (statisch)")
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL2_DISABLE_SDL2MAIN ON CACHE BOOL "" FORCE)
  set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL2
    GIT_REPOSITORY "${LEVI_SIM_SDL_URL}"
    GIT_TAG "${LEVI_SIM_SDL_REF}"
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL2)
  set(LEVI_SDL_TARGET SDL2::SDL2-static)
endif()
