// Host-Vorschau der Controller-Oberflaeche: kompiliert die echten ui_*.cpp gegen LVGL (Framebuffer)
// mit nachgebildetem Controller-Dienst und schreibt PPM-Bilder. Nicht das Geraet: Touch, Farben und
// Timing koennen abweichen.
#include <stdio.h>
#include <string.h>
#include <string>
#include "lvgl.h"
#include "ui.h"
#include "board.h"
#include "lvgl_port.h"

// ---- Nachbildung des Controller-Dienstes (app.h) ----
static GatewayView gws[16];
static uint32_t version = 1;
uint32_t appMillis() { return 100000; }
uint32_t appVersion() { return version; }
int appGatewayCount() { return 16; }
bool appView(int i, GatewayView &v) { v = gws[i]; return v.used; }
static uint8_t own[6] = {0x60, 0x55, 0xF9, 0x12, 0x34, 0x56};
const uint8_t *appOwnId() { return own; }
CtrlLink::Stats appStats() { CtrlLink::Stats s; s.sent = 120; s.resent = 3; s.rx = 340; return s; }
void appDiscover() {}
lvp_err_t appPair(int, const uint8_t *) { return LVP_OK; }
bool appUnpair(int) { return true; }
bool appSetAlias(int, const char *) { return true; }
lvp_err_t appSetPower(int, bool) { return LVP_OK; }
lvp_err_t appSetBrightness(int, uint8_t) { return LVP_OK; }
lvp_err_t appSetColor(int, uint8_t, uint8_t, uint8_t) { return LVP_OK; }
lvp_err_t appSetRaw(int, uint8_t, uint8_t, uint8_t) { return LVP_OK; }
lvp_err_t appSetMusic(int, bool) { return LVP_OK; }
lvp_err_t appSetState(int, const lvp_set_state_t &) { return LVP_OK; }
bool appRequestState(int) { return true; }
bool appRequestCalib(int) { return true; }
bool appSendCalib(int, const uint8_t *) { return true; }
bool appResetCalib(int) { return true; }
const char *appErrorText(uint8_t, bool) { return "Keine Antwort vom Gateway"; }
bool boardTouchCalLoad(TouchCal &) { return true; }
bool boardTouchCalSave(const TouchCal &) { return true; }
uint32_t lvglIdleMs() { return 0; }
void lvglSetTouchCal(const TouchCal &) {}
bool lvglTouchRawPressed(int32_t *x, int32_t *y) { *x = *y = 0; return false; }

static void gw(int i, const char *name, bool online, bool ble, int8_t power, uint8_t br, uint8_t r, uint8_t g, uint8_t b, bool paired, uint8_t caps = 0) {
  GatewayView &v = gws[i];
  memset(&v, 0, sizeof(v));
  v.used = true; v.paired = paired; v.online = online; v.stateValid = paired && online;
  v.id[0] = 0x34; v.id[5] = (uint8_t)(0x10 + i);
  strcpy(v.name, name); strcpy(v.announcedName, name);
  v.caps = caps | (ble ? LVP_GW_BLE_CONNECTED : 0); v.fwVersion = 0x0100; v.lastSeenAgoMs = 1200;
  v.state.power = power; v.state.brightness = br; v.state.r = r; v.state.g = g; v.state.b = b;
  v.state.flags = ble ? LVP_STATE_BLE : 0; v.state.revision = 12; v.state.sent_revision = 12;
  v.calibValid = paired; v.calib.calib_revision = 3;
  for (int k = 0; k < 30; k++) v.calib.points[k] = (uint8_t)(255 - k * 3);
}

static uint8_t *fb;
static void flushCb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  const uint16_t *s = (const uint16_t *)px;
  int w = lv_area_get_width(a), h = lv_area_get_height(a);
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    uint16_t v = s[y * w + x];
    int i = ((a->y1 + y) * LCD_W + a->x1 + x) * 3;
    fb[i] = ((v >> 11) & 31) * 255 / 31; fb[i + 1] = ((v >> 5) & 63) * 255 / 63; fb[i + 2] = (v & 31) * 255 / 31;
  }
  lv_display_flush_ready(d);
}
static std::string out;
static void shot(const char *name) {
  for (int i = 0; i < 5; i++) { lv_timer_handler(); }
  lv_refr_now(nullptr);
  FILE *f = fopen((out + "/" + name + ".ppm").c_str(), "wb");
  fprintf(f, "P6\n%d %d\n255\n", LCD_W, LCD_H); fwrite(fb, 1, LCD_W * LCD_H * 3, f); fclose(f);
}

int main(int argc, char **argv) {
  out = argc > 1 ? argv[1] : ".";
  lv_init();
  static uint32_t t = 0;
  lv_tick_set_cb([]() { return t += 50; });
  fb = new uint8_t[LCD_W * LCD_H * 3]();
  static uint8_t buf[LCD_W * LCD_H * 2];
  lv_display_t *d = lv_display_create(LCD_W, LCD_H);
  lv_display_set_buffers(d, buf, nullptr, sizeof(buf), LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(d, flushCb);

  gw(0, "Wohnzimmer", true, true, 1, 75, 255, 107, 107, true);
  gw(1, "Kueche", true, false, 0, 100, 255, 255, 255, true);
  gw(2, "Balkon", false, false, -1, 100, 0, 0, 0, true);
  gw(3, "LeviLuma-3A7F", true, true, -1, 100, 0, 0, 0, false, LVP_GW_KEY_REQUIRED);
  gw(4, "LeviLuma-91C2", true, true, -1, 100, 0, 0, 0, false, LVP_GW_PAIRING_OPEN);
  gws[0].hasKey = true;

  uiBuild();                 shot("01_home");
  uiShowDetail(0);           shot("02_detail");
  uiShowSettings();          shot("03_settings");
  uiShowGateways();          shot("04_gateways");
  uiShowPair(3);             shot("05_pair_key");
  uiShowPair(4);             shot("06_pair_open");
  uiShowGatewaySettings(0);  shot("07_gateway_settings");
  uiShowCalibration(0);      shot("08_calibration");
  uiShowInfo();              shot("09_info");
  for (int i = 0; i < 16; i++) gws[i].used = false;
  version++;
  uiShowHome();              shot("10_home_empty");
  uiShowTouchCal(false);     shot("11_touch_cal");
  return 0;
}
