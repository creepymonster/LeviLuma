/* LVGL-Konfiguration des Desktop-Simulators (nicht fuer die Firmware).
 *
 * Gleiche Einstellungen wie die Firmware beider Controller, die LVGL ueber Kconfig konfiguriert
 * (controller/sdkconfig.defaults, controller-round/sdkconfig.defaults, Abschnitt "LVGL 9.6").
 * Alles, was dort nicht gesetzt ist, bleibt auf dem LVGL-Standard (wie bei der Firmware).
 * Die Fontliste prueft simulator/CMakeLists.txt gegen beide sdkconfig.defaults.
 *
 * Einzige Abweichung: kein Betriebssystem-Layer (LV_USE_OS NONE). LVGL laeuft im Simulator wie in der
 * Firmware nur in einem Thread (hier der Hauptthread mit der SDL-Ereignisschleife).
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/* CONFIG_LV_COLOR_DEPTH_16=y (in LVGL 9.6 gleichbedeutend, LV_COLOR_DEPTH ist dort abgekuendigt) */
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565

/* CONFIG_LV_USE_CLIB_MALLOC / _STRING / _SPRINTF=y */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

/* CONFIG_LV_DEF_REFR_PERIOD=20 */
#define LV_DEF_REFR_PERIOD 20

#define LV_USE_OS LV_OS_NONE

/* CONFIG_LV_FONT_MONTSERRAT_*=y, CONFIG_LV_FONT_DEFAULT_MONTSERRAT_16=y */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16

/* CONFIG_LV_USE_LOG=n */
#define LV_USE_LOG 0

/* CONFIG_LV_USE_THEME_DEFAULT=y (Simulator: Theme der Steuerleiste) */
#define LV_USE_THEME_DEFAULT 1

#define LV_USE_DRAW_SW 1
#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0

#endif /* LV_CONF_H */
