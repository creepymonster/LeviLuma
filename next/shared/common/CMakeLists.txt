# Gemeinsame Hilfen fuer Controller und Gateway: Schluessel-Wert-Speicher (NVS) und Schluessel-Format.
idf_component_register(SRCS "src/kv_store.cpp" "src/levi_key.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES nvs_flash log)
