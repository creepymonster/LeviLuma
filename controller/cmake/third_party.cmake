# Externe Komponenten des Controllers, per Git auf feste Versionen gepinnt und beim ersten
# CMake-Lauf nach controller/third_party/ geholt (nicht im Repository, siehe .gitignore).
#
# Warum nicht der ESP Component Manager? Die Abhaengigkeiten des shadcn-Themes und von LVGL
# verweisen auf die ESP Registry. Die feste Git-Version ist reproduzierbar und funktioniert
# auch ohne Registry-Zugang. Der Component Manager ist fuer dieses Projekt abgeschaltet.
#
# Manuell: tools/fetch_deps.sh

set(LEVI_TP_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party")

# Versionen (bei Aenderung den Ordner third_party/<name> loeschen, dann neu konfigurieren)
set(LEVI_LVGL_URL  "https://github.com/lvgl/lvgl.git")
set(LEVI_LVGL_REF  "v9.6.0")
set(LEVI_THEME_URL "https://github.com/AlixANNERAUD/lvgl_theme_shadcn.git")
set(LEVI_THEME_REF "v0.3.1")
# esp-zigbee-sdk hat keine Release-Tags im Repository: fester Commit (esp-zigbee-lib 2.0.5)
set(LEVI_ZIGBEE_URL "https://github.com/espressif/esp-zigbee-sdk.git")
set(LEVI_ZIGBEE_REF "e2fdf4ad8558e5beb306b0d7040072d76244c413")

function(levi_git_tag name url ref)
  set(dst "${LEVI_TP_DIR}/${name}")
  if(EXISTS "${dst}/CMakeLists.txt")
    return()
  endif()
  message(STATUS "LeviLuma: hole ${name} ${ref}")
  execute_process(COMMAND git clone --quiet --depth 1 --branch ${ref} -c advice.detachedHead=false ${url} "${dst}"
                  RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    file(REMOVE_RECURSE "${dst}")
    message(FATAL_ERROR "LeviLuma: ${name} konnte nicht geladen werden (${url} ${ref}). Netzwerk/Git pruefen.")
  endif()
endfunction()

# Nur esp-zigbee-lib mit den Bibliotheken fuer den ESP32-C5 (Sparse Checkout, sonst ueber 170 MB)
function(levi_git_zigbee)
  set(dst "${LEVI_TP_DIR}/esp-zigbee-sdk")
  if(EXISTS "${dst}/components/esp-zigbee-lib/CMakeLists.txt")
    return()
  endif()
  message(STATUS "LeviLuma: hole esp-zigbee-lib (${LEVI_ZIGBEE_REF})")
  file(MAKE_DIRECTORY "${dst}")
  set(steps
    "git init --quiet"
    "git remote add origin ${LEVI_ZIGBEE_URL}"
    "git sparse-checkout set --no-cone /components/esp-zigbee-lib/ !/components/esp-zigbee-lib/lib/ /components/esp-zigbee-lib/lib/esp32c5/"
    "git fetch --quiet --depth 1 --filter=blob:none origin ${LEVI_ZIGBEE_REF}"
    "git -c advice.detachedHead=false checkout --quiet FETCH_HEAD")
  foreach(step IN LISTS steps)
    separate_arguments(cmd UNIX_COMMAND "${step}")
    execute_process(COMMAND ${cmd} WORKING_DIRECTORY "${dst}" RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
      file(REMOVE_RECURSE "${dst}")
      message(FATAL_ERROR "LeviLuma: esp-zigbee-lib konnte nicht geladen werden (Schritt: ${step}).")
    endif()
  endforeach()
endfunction()

# Zigbee aktiv? In sdkconfig (falls schon vorhanden) und in den sdkconfig-Defaults nachsehen.
function(levi_zigbee_requested out)
  # Reihenfolge: Defaults zuerst, die vorhandene sdkconfig zuletzt (sie ist massgeblich)
  set(files "")
  foreach(f IN LISTS SDKCONFIG_DEFAULTS)
    if(NOT IS_ABSOLUTE "${f}")
      set(f "${CMAKE_SOURCE_DIR}/${f}")
    endif()
    list(APPEND files "${f}")
  endforeach()
  if(NOT SDKCONFIG_DEFAULTS)
    list(APPEND files "${CMAKE_SOURCE_DIR}/sdkconfig.defaults")
  endif()
  if(SDKCONFIG)
    list(APPEND files "${SDKCONFIG}")
  else()
    list(APPEND files "${CMAKE_SOURCE_DIR}/sdkconfig")
  endif()
  set(found OFF)
  foreach(f IN LISTS files)
    if(EXISTS "${f}")
      file(STRINGS "${f}" lines REGEX "CONFIG_LEVILUMA_ZIGBEE_ENABLED[= ]")
      foreach(l IN LISTS lines)
        if(l STREQUAL "CONFIG_LEVILUMA_ZIGBEE_ENABLED=y")
          set(found ON)
        else()
          set(found OFF)
        endif()
      endforeach()
    endif()
  endforeach()
  set(${out} ${found} PARENT_SCOPE)
endfunction()
