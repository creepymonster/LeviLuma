# Ende-zu-Ende-Tests des Simulators: starten die echten Programme, bedienen die Firmware-Oberflaeche ueber
# Mausereignisse (SDL-Ereignisschlange) und pruefen Anzeige und simulierten Gateway-Zustand.
# Ohne Bildschirm laufen sie mit dem SDL-Treiber "offscreen" (Standard); mit
# -DLEVI_SIM_TEST_VIDEO=x11 (oder leer = SDL waehlt) laufen sie in einem sichtbaren Fenster.
# Diese Tests gehoeren zum Simulator, nicht zur Host-Testsuite next/tests (eigene Auswertung dort).
set(LEVI_SIM_TEST_VIDEO "offscreen" CACHE STRING "SDL_VIDEODRIVER fuer die Simulator-Tests (leer = SDL waehlt)")

function(levi_sim_test name exe script)
  add_test(NAME ${name}
           COMMAND $<TARGET_FILE:${exe}> --no-dim --script "${CMAKE_CURRENT_SOURCE_DIR}/${script}" ${ARGN}
           WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
  set(env "")
  if(LEVI_SIM_TEST_VIDEO)
    list(APPEND env "SDL_VIDEODRIVER=${LEVI_SIM_TEST_VIDEO}")
  endif()
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "${env}" TIMEOUT 180 LABELS simulator)
endfunction()

levi_sim_test(sim_controller_basic leviluma-sim-controller controller_basic.sim)
levi_sim_test(sim_controller_gateway_states leviluma-sim-controller controller_gateway_states.sim)
levi_sim_test(sim_controller_pairing leviluma-sim-controller controller_pairing.sim --gateways 3)
levi_sim_test(sim_controller_calibration leviluma-sim-controller controller_calibration.sim)
levi_sim_test(sim_controller_first_boot leviluma-sim-controller controller_first_boot.sim --first-boot)
levi_sim_test(sim_round_basic leviluma-sim-round round_basic.sim)
levi_sim_test(sim_round_gateway_states leviluma-sim-round round_gateway_states.sim)
levi_sim_test(sim_round_pairing leviluma-sim-round round_pairing.sim)

# Keine ESP-IDF-/FreeRTOS-Abhaengigkeit: alle uebersetzten Quellen und ihre Include-Pfade pruefen
set(LEVI_CHECK_LIST "${CMAKE_CURRENT_BINARY_DIR}/check_no_idf_input.txt")
file(GENERATE OUTPUT "${LEVI_CHECK_LIST}" CONTENT
"$<JOIN:$<TARGET_PROPERTY:leviluma-sim-controller,SOURCES>;$<TARGET_PROPERTY:leviluma-sim-round,SOURCES>;$<TARGET_PROPERTY:levi_sim_core,SOURCES>,\n>
INCLUDES
$<JOIN:$<TARGET_PROPERTY:leviluma-sim-controller,INCLUDE_DIRECTORIES>;$<TARGET_PROPERTY:leviluma-sim-round,INCLUDE_DIRECTORIES>;$<TARGET_PROPERTY:levi_sim_core,INCLUDE_DIRECTORIES>,\n>
")
add_test(NAME sim_no_idf
         COMMAND ${CMAKE_COMMAND} -DLIST=${LEVI_CHECK_LIST} -DSIM_DIR=${LEVI_SIM_DIR} -DBUILD_DIR=${CMAKE_BINARY_DIR} -P "${LEVI_SIM_DIR}/cmake/check_no_idf.cmake")
set_tests_properties(sim_no_idf PROPERTIES LABELS simulator)
