# Host-Tests fuer LeviLuma Next (laufen ohne Hardware und ohne ESP-IDF).
# Aufruf: tests/run_tests.sh   oder   cmake -S tests -B tests/build && cmake --build tests/build && tests/build/levi_tests
cmake_minimum_required(VERSION 3.16)
project(leviluma_next_tests C CXX)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)

set(NEXT ${CMAKE_CURRENT_LIST_DIR}/..)
# Pruefverfahren: ASan + UBSan; UBSan-Funde brechen den Lauf ab (kein stilles Weiterlaufen). Der Wert landet im Ergebnis/Bericht.
set(LEVI_SANITIZERS "AddressSanitizer,UndefinedBehaviorSanitizer (no-recover)")
add_compile_options(-Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
add_link_options(-fsanitize=address,undefined -fno-sanitize-recover=all)

add_executable(levi_tests
  test_main.cpp test_protocol.cpp test_gateway.cpp test_controller.cpp test_round.cpp test_debug.cpp
  ${NEXT}/shared/protocol/src/leviluma_proto.c
  ${NEXT}/shared/protocol/src/leviluma_link.c
  ${NEXT}/shared/common/src/kv_store.cpp
  ${NEXT}/shared/common/src/levi_key.c
  ${NEXT}/gateway/components/light_core/src/light_model.cpp
  ${NEXT}/gateway/components/light_core/src/ble_scheduler.cpp
  ${NEXT}/gateway/components/light_core/src/calib_store.cpp
  ${NEXT}/gateway/components/light_core/src/pairing_table.cpp
  ${NEXT}/gateway/components/light_core/src/gateway_handler.cpp
  ${NEXT}/gateway/components/light_core/src/band_memory.cpp
  ${NEXT}/controller/components/controller_core/src/gateway_registry.cpp
  ${NEXT}/controller/components/controller_core/src/ctrl_link.cpp
  ${NEXT}/controller/components/controller_core/src/zb_mapping.cpp
  ${NEXT}/controller/components/controller_core/src/touch_cal.cpp
  ${NEXT}/controller-round/components/round_core/src/round_core.cpp
  ${NEXT}/debug/components/debug_core/src/dbg_text.cpp
  ${NEXT}/debug/components/debug_core/src/dbg_frame.cpp
  ${NEXT}/debug/components/debug_core/src/dbg_levi.cpp
  ${NEXT}/debug/components/debug_core/src/dbg_state.cpp
  ${NEXT}/debug/components/debug_core/src/dbg_format.cpp
  ${NEXT}/debug/components/debug_core/src/dbg_cmd.cpp)
target_include_directories(levi_tests PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${NEXT}/shared/protocol/include
  ${NEXT}/shared/common/include
  ${NEXT}/gateway/components/light_core/include
  ${NEXT}/controller/components/controller_core/include
  ${NEXT}/controller-round/components/round_core/include
  ${NEXT}/debug/components/debug_core/include)
target_compile_definitions(levi_tests PRIVATE LEVI_SANITIZERS="${LEVI_SANITIZERS}")
target_link_libraries(levi_tests m)
