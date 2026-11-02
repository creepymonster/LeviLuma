idf_component_register(SRCS "main.cpp" "espnow_link.cpp" "ble_link.cpp" "console.cpp" "status_io.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES light_core protocol common
                       PRIV_REQUIRES nvs_flash esp_wifi esp_netif esp_event esp_timer bt esp_driver_gpio)
