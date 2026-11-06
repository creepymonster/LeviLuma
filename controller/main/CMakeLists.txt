set(srcs "main.cpp" "app.cpp" "board.cpp" "lvgl_port.cpp"
         "ui_common.cpp" "ui_home.cpp" "ui_detail.cpp" "ui_settings.cpp" "ui_calib.cpp" "ui_touchcal.cpp")
set(reqs controller_core protocol common lvgl lvgl_theme_shadcn)

# Zigbee nur mit aktiviertem Build-Flag: keine Zigbee-Quellen, kein Stack, keine Endpoints sonst.
# LEVI_WITH_ZIGBEE setzt das Projekt-CMakeLists aus sdkconfig/sdkconfig.defaults*.
idf_build_get_property(levi_with_zigbee LEVI_WITH_ZIGBEE)
if(levi_with_zigbee)
  list(APPEND srcs "zigbee_bridge.cpp" "ui_zigbee.cpp")
  list(APPEND reqs esp-zigbee-lib)
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       REQUIRES ${reqs}
                       PRIV_REQUIRES nvs_flash esp_wifi esp_netif esp_event esp_timer esp_lcd esp_driver_spi esp_driver_ledc esp_driver_gpio)
