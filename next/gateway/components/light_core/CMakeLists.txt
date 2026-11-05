# Reine Gateway-Logik ohne Funk (auch auf dem Host getestet, siehe next/tests):
# LightState, BLE-Befehlsauswahl, Kalibrierung, Kopplungen, Protokollbehandlung, gemerktes Band.
idf_component_register(SRCS "src/light_model.cpp" "src/ble_scheduler.cpp" "src/calib_store.cpp"
                            "src/pairing_table.cpp" "src/gateway_handler.cpp"
                            "src/band_memory.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES protocol common)
