# Host-Tests fuer LeviLuma Next (laufen ohne Hardware und ohne ESP-IDF).
# Aufruf: next/tests/run_tests.sh   oder   cmake -S next/tests -B next/tests/build && cmake --build next/tests/build && next/tests/build/levi_tests
cmake_minimum_required(VERSION 3.16)
project(leviluma_next_tests C CXX)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)

set(NEXT ${CMAKE_CURRENT_LIST_DIR}/..)
add_compile_options(-Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -fno-omit-frame-pointer)
add_link_options(-fsanitize=address,undefined)

add_executable(levi_tests
  test_main.cpp test_protocol.cpp test_gateway.cpp test_controller.cpp
  ${NEXT}/shared/protocol/src/leviluma_proto.c
  ${NEXT}/shared/protocol/src/leviluma_link.c
  ${NEXT}/shared/common/src/kv_store.cpp
  ${NEXT}/shared/common/src/levi_key.c
  ${NEXT}/gateway/components/light_core/src/light_model.cpp
  ${NEXT}/gateway/components/light_core/src/ble_scheduler.cpp
  ${NEXT}/gateway/components/light_core/src/calib_store.cpp
  ${NEXT}/gateway/components/light_core/src/pairing_table.cpp
  ${NEXT}/gateway/components/light_core/src/gateway_handler.cpp
  ${NEXT}/controller/components/controller_core/src/gateway_registry.cpp
  ${NEXT}/controller/components/controller_core/src/ctrl_link.cpp
  ${NEXT}/controller/components/controller_core/src/zb_mapping.cpp
  ${NEXT}/controller/components/controller_core/src/touch_cal.cpp)
target_include_directories(levi_tests PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${NEXT}/shared/protocol/include
  ${NEXT}/shared/common/include
  ${NEXT}/gateway/components/light_core/include
  ${NEXT}/controller/components/controller_core/include)
target_link_libraries(levi_tests m)
