#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_USE_LOG 0
#define LV_USE_ARC 1

#define LV_USE_LABEL 1

#define LV_USE_DRAW_SW 1

#define LV_MEM_SIZE (96U * 1024U)
/* Wischgesten: Mindestweg in Pixeln, ab dem LVGL eine Wischgeste erkennt (LVGL-Standard 50). */
#define LV_INDEV_DEF_GESTURE_LIMIT 30
/* An/Aus: Mitte 500 ms halten. */
#define LV_INDEV_DEF_LONG_PRESS_TIME 500
#endif
