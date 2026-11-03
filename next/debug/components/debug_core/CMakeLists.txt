# Reine Auswertungs- und Formatierlogik ohne Funk (auch auf dem Host getestet, siehe next/tests):
# Rahmenauswertung, LeviLuma-Dekodierung, Ausgabe, Filter, Befehle.
idf_component_register(SRCS "src/dbg_text.cpp" "src/dbg_frame.cpp" "src/dbg_levi.cpp" "src/dbg_state.cpp" "src/dbg_format.cpp" "src/dbg_cmd.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES protocol)
