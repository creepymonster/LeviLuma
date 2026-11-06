# Gemeinsames LeviLuma-ESP-NOW-Protokoll.
# In ESP-IDF als Komponente "protocol" (ueber EXTRA_COMPONENT_DIRS in Controller und Gateway),
# auf dem Host wird es von tests direkt uebersetzt.
idf_component_register(SRCS "src/leviluma_proto.c" "src/leviluma_link.c"
                       INCLUDE_DIRS "include")
