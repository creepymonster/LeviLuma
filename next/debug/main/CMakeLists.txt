idf_component_register(SRCS "main.cpp" "sniffer.cpp" "report.cpp" "console.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES debug_core protocol
                       PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer)
