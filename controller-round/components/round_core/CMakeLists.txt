# Hosttestbare Logik des runden Controllers (Ein-Gateway-Regel, Sendedrossel)
idf_component_register(SRCS "src/round_core.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES controller_core)
