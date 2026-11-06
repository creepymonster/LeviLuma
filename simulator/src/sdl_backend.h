#pragma once
// SDL-Anbindung des Simulators (Ersatz fuer Displaytreiber, Touchcontroller und Hintergrundbeleuchtung).
//
// Ein Fenster mit zwei LVGL-Displays:
//  - links das Geraetedisplay in der Originalaufloesung (vergroessert um "zoom"); beim runden
//    Controller wird alles ausserhalb des Displaykreises maskiert,
//  - rechts die Steuerleiste des Simulators (simuliertes Gateway), ein eigenes LVGL-Display.
// Das Geraetedisplay ist das LVGL-Standarddisplay; die Oberflaeche der Firmware arbeitet nur darauf.
// Die Maus ersetzt den Touch: Linke Taste = Beruehrung, Ziehen = Wischen. Eine Beruehrung beginnt nur
// innerhalb der sichtbaren Displayflaeche (rund: innerhalb des Kreises).
// Alle Funktionen laufen im Hauptthread (SDL-Ereignisse und LVGL).
#include <stdint.h>
#include <string>
#include "lvgl.h"

struct SimDeviceSpec {
  const char *name; // fuer Fenstertitel und Steuerleiste
  int width, height;
  bool round;
};

struct SimPointer {
  bool pressed;
  int32_t x, y;
};

namespace SimBackend {
bool init(const SimDeviceSpec &spec, int zoom, int panelWidth);
void shutdown();
lv_display_t *deviceDisplay();
lv_display_t *panelDisplay();
const SimDeviceSpec &spec();

// Touch-Ersatz fuer den Geraete-Port (Mauszustand in Displaykoordinaten)
SimPointer devicePointer();
// Hintergrundbeleuchtung 0..100 %
void setBacklight(int percent);
int backlight();

// SDL-Ereignisse verarbeiten; false, wenn das Fenster geschlossen wurde
bool pump();
// Texturen aus den LVGL-Puffern aktualisieren und zeichnen
void present();
void setTitleSuffix(const std::string &s);

// Fuer Testskripte: Displaykoordinaten -> Fensterkoordinaten, Ereignisse wie von der Maus
void injectMouse(bool panel, int type, int32_t x, int32_t y); // type: 0 Bewegung, 1 Druecken, 2 Loslassen
bool saveWindow(const char *path);  // BMP
bool saveDevice(const char *path);  // BMP, nur das Display in Originalaufloesung (mit Maske)
} // namespace SimBackend
