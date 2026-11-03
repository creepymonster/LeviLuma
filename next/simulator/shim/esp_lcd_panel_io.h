/* Desktop-Simulator: Platzhalter fuer ESP-IDF esp_lcd (kein ESP-IDF-Code).
 * next/controller/main/board.h bindet esp_lcd ein, weil die Firmware dort Panel-Typen deklariert.
 * Die Oberflaeche nutzt aus board.h nur LCD_W/LCD_H und die Touch-Kalibrierung; die Typen hier sind
 * nur Namen, damit board.h unveraendert uebersetzt. Implementiert wird nichts davon. */
#pragma once
typedef void *esp_lcd_panel_io_handle_t;
typedef struct {
  int unused;
} esp_lcd_panel_io_event_data_t;
typedef bool (*esp_lcd_panel_io_color_trans_done_cb_t)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *);
