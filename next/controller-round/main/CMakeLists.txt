idf_component_register(SRCS "main.cpp" "app.cpp" "board.cpp" "lvgl_port.cpp" "ui.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES round_core controller_core protocol common lvgl lvgl_theme_shadcn LovyanGFX
                       PRIV_REQUIRES nvs_flash esp_wifi esp_event esp_timer esp_driver_ledc)
