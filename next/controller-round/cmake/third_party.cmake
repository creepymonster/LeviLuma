# Externe Komponenten des runden Controllers, per Git auf feste Versionen gepinnt und beim ersten
# CMake-Lauf nach next/controller-round/third_party/ geholt (nicht im Repository, siehe .gitignore).
# Der ESP Component Manager ist abgeschaltet (wie beim RockBase-Controller), damit keine Registry noetig ist.
#  - LVGL und lvgl_theme_shadcn: dieselben Versionen wie Legacy und RockBase-Controller
#  - LovyanGFX: Display-/Touchtreiber aus Legacy (Panel_GC9A01, Touch_CST816S), hier ohne Arduino
set(LEVI_TP_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party")

set(LEVI_LVGL_URL  "https://github.com/lvgl/lvgl.git")
set(LEVI_LVGL_REF  "v9.6.0")
set(LEVI_THEME_URL "https://github.com/AlixANNERAUD/lvgl_theme_shadcn.git")
set(LEVI_THEME_REF "v0.3.1")
# Legacy: lovyan03/LovyanGFX@^1.2.7, der Legacy-Build in diesem Auftrag lief mit 1.2.36
set(LEVI_LGFX_URL  "https://github.com/lovyan03/LovyanGFX.git")
set(LEVI_LGFX_REF  "1.2.36")

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
