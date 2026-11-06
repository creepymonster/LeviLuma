#pragma once
// Zigbee-Hilfen ohne Zigbee-Stack (auf dem Host getestet):
//  - stabile Zuordnung Gateway-ID <-> Zigbee-Endpoint-ID (persistent im Gateway-Register)
//  - Umrechnung zwischen Zigbee-Attributen (Level, Hue/Saturation, CIE xy, Farbtemperatur) und RGB/Helligkeit.
#include <stdint.h>
#include "gateway_registry.h"

namespace ZbMap {

static constexpr uint8_t FIRST_ENDPOINT = 10; // Endpoints 10..17 (einer je gekoppeltem Gateway)
static constexpr uint8_t MAX_ENDPOINTS = GatewayRegistry::MAX_PAIRED;
static constexpr uint8_t OPT_OUT = 0xFF; // bewusst ohne Zigbee-Endpoint (wird nicht automatisch zugeordnet)

// Endpoint zuordnen (bestehende Zuordnung bleibt unveraendert, auch nach OPT_OUT moeglich).
// 0 bei Fehler (nicht gekoppelt, voll, NVS).
uint8_t assign(GatewayRegistry &reg, int idx);
// Zuordnung loesen und als OPT_OUT merken (wirkt nach einem Neustart des Zigbee-Stacks).
bool release(GatewayRegistry &reg, int idx);
// Hat das Gateway einen Endpoint (10..17)?
inline bool hasEndpoint(const GatewayEntry &g) {
  return g.zbEndpoint != 0 && g.zbEndpoint != OPT_OUT;
}
// Gateway-Index zu einem Endpoint, -1 wenn keiner
int findByEndpoint(const GatewayRegistry &reg, uint8_t ep);
// Alle gekoppelten Gateways ohne Endpoint (und ohne OPT_OUT) zuordnen. Rueckgabe: true, wenn sich etwas geaendert hat.
bool assignAll(GatewayRegistry &reg);
// Kennwert der aktuellen Endpoint-Liste: aendert er sich, muss der Zigbee-Stack neu starten.
uint32_t layoutSignature(const GatewayRegistry &reg);

// Zigbee Level (1..254) <-> Helligkeit 1..100 %
uint8_t levelToBrightness(uint8_t level);
uint8_t brightnessToLevel(uint8_t brightness);

// Hue/Saturation (Zigbee 0..254) <-> RGB (volle Helligkeit)
void hueSatToRgb(uint8_t hue, uint8_t sat, uint8_t rgb[3]);
void rgbToHueSat(const uint8_t rgb[3], uint8_t *hue, uint8_t *sat);

// CIE xy (Zigbee 0..65279, Wert/65536) <-> RGB (sRGB, D65, volle Helligkeit)
void xyToRgb(uint16_t x, uint16_t y, uint8_t rgb[3]);
void rgbToXy(const uint8_t rgb[3], uint16_t *x, uint16_t *y);

// Farbtemperatur in Mired (153..500) -> RGB (Naeherung nach Tanner Helland)
void miredToRgb(uint16_t mired, uint8_t rgb[3]);

} // namespace ZbMap
