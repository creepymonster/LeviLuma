#pragma once
// Optionale Zigbee-Anbindung (nur mit CONFIG_LEVILUMA_ZIGBEE_ENABLED):
// Je gekoppeltem Gateway ein Zigbee-Light-Endpoint (On/Off, Level, Color Control).
// Befehlsweg:  FRITZ!Box -> Zigbee -> C5 -> ESP-NOW -> Gateway -> BLE -> LED-Leiste
// Rueckweg:    Gateway -> ESP-NOW -> C5 -> Zigbee-Attribute (und LVGL)
// Der LightState bleibt auf dem Gateway autoritativ; die Zigbee-Attribute spiegeln nur den
// bestaetigten Zustand und werden nach jeder Gateway-Rueckmeldung ueberschrieben.
#include <stdint.h>

struct ZbStatus {
  bool started = false;    // Stack laeuft
  bool joined = false;     // Netzwerk beigetreten
  bool steering = false;   // Kopplung mit der FRITZ!Box laeuft
  bool restartRequired = false; // Endpoint-Zuordnung geaendert: Neustart noetig
  bool noEndpoints = false;     // kein gekoppeltes Gateway: Stack nicht gestartet
  uint16_t panId = 0, shortAddr = 0;
  uint8_t channel = 0;
  int endpoints = 0;
  const char *deviceType = "";
  char lastSignal[40] = "";
  uint32_t commandsRx = 0, attrUpdates = 0, errors = 0;
};

void zbStart();
ZbStatus zbStatus();
void zbStartSteering(); // FRITZ!Box: vorher "Smart Home > Geraet anmelden" starten
void zbLeave();         // Netzwerk verlassen (lokaler Reset der Zigbee-Daten)
// Zuordnung eines gekoppelten Gateways setzen/loesen (wirkt nach Neustart)
bool zbAssign(int gwIdx, bool assign);
