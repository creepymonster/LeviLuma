#pragma once
// Steuerleiste des Simulators (rechts im Fenster, eigenes LVGL-Display, nicht Teil der Firmware-UI).
// Zeigt den simulierten Gateway-Zustand und stellt ihn um: online/offline, Band verbunden/getrennt,
// Musikmodus, Kopplungsfenster (BOOT-Taste), Paketverlust. Aenderungen gehen nur als Befehle an die
// simulierten Gateways (MockGateway::post); die Controller-Oberflaeche erfaehrt sie wie bei echter
// Hardware ueber ESP-NOW-Rahmen (ANNOUNCE/STATE) und CtrlLink.
#include <vector>
#include "lvgl.h"
#include "mock_gateway.h"
#include "sim_air.h"

void controlPanelBuild(lv_display_t *panel, const char *deviceName, std::vector<MockGateway *> gateways, SimAir &air);
