# Reine Controller-Logik ohne Hardware (auch auf dem Host getestet, siehe tests):
# Gateway-Register, ESP-NOW-Link mit Wiederholung und Abgleich, Zigbee-Zuordnung, Touch-Kalibrierung.
idf_component_register(SRCS "src/gateway_registry.cpp" "src/ctrl_link.cpp" "src/zb_mapping.cpp" "src/touch_cal.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES protocol common)
