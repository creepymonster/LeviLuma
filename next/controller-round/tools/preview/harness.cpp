// Host-Vorschau der runden Oberflaeche: kompiliert main/ui.cpp gegen LVGL mit nachgebildetem
// Controller-Dienst und schreibt PPM-Bilder (ausserhalb des Kreises abgedunkelt wie am Geraet).
// Nicht das Geraet: Touch, Farben und Timing koennen abweichen.
#include <stdio.h>
#include <string.h>
#include <string>
#include "app.h"
#include "lvgl.h"
#include "ui.h"

static GatewayView gws[16];
static int active = -1;
static uint32_t version = 1;
uint32_t appMillis() { static uint32_t t = 100000; return t += 10; }
uint32_t appVersion() { return version; }
int appGatewayCount() { return 16; }
bool appView(int i, GatewayView &v) { v = gws[i]; return v.used; }
int appActive() { return active; }
bool appMayPair() { return active < 0; }
void appDiscover() {}
lvp_err_t appPair(int, const uint8_t *) { return LVP_OK; }
bool appUnpair() { return true; }
bool appResync() { return true; }
lvp_err_t appSetPower(bool) { return LVP_OK; }
lvp_err_t appSetBrightness(uint8_t) { return LVP_OK; }
lvp_err_t appSetColor(uint8_t, uint8_t, uint8_t) { return LVP_OK; }
const char *appErrorText(uint8_t, bool) { return "Keine Antwort"; }
uint32_t lvglIdleMs() { return 0; }

static void gw(int i, const char *name, bool paired, bool online, int8_t power, uint8_t br, uint8_t r, uint8_t g, uint8_t b, uint8_t caps = 0) {
  GatewayView &v = gws[i];
  memset(&v, 0, sizeof(v));
  v.used = true; v.paired = paired; v.online = online; v.stateValid = paired;
  v.id[0] = 0x34; v.id[3] = 0xA1; v.id[4] = 0xB2; v.id[5] = (uint8_t)(0x10 + i);
  strcpy(v.name, name); v.caps = caps; v.lastSeenAgoMs = 1500; v.hasKey = paired;
  v.state.power = power; v.state.brightness = br; v.state.r = r; v.state.g = g; v.state.b = b;
  v.state.flags = LVP_STATE_BLE;
}

static uint8_t *fb;
static void flushCb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  const uint16_t *s = (const uint16_t *)px;
  int w = lv_area_get_width(a), h = lv_area_get_height(a);
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    uint16_t v = s[y * w + x];
    int i = ((a->y1 + y) * 240 + a->x1 + x) * 3;
    fb[i] = ((v >> 11) & 31) * 255 / 31; fb[i + 1] = ((v >> 5) & 63) * 255 / 63; fb[i + 2] = (v & 31) * 255 / 31;
  }
  lv_display_flush_ready(d);
}
static std::string out;
static void shot(const char *name) {
  for (int i = 0; i < 30; i++) lv_timer_handler();
  lv_refr_now(nullptr);
  FILE *f = fopen((out + "/" + name + ".ppm").c_str(), "wb");
  fprintf(f, "P6\n240 240\n255\n");
  for (int y = 0; y < 240; y++) for (int x = 0; x < 240; x++) {
    int i = (y * 240 + x) * 3; float dx = x - 119.5f, dy = y - 119.5f;
    bool in = dx * dx + dy * dy <= 120.f * 120.f; // rundes Panel
    unsigned char p[3] = {(unsigned char)(in ? fb[i] : 40), (unsigned char)(in ? fb[i + 1] : 40), (unsigned char)(in ? fb[i + 2] : 40)};
    fwrite(p, 1, 3, f);
  }
  fclose(f);
}

int main(int argc, char **argv) {
  out = argc > 1 ? argv[1] : ".";
  lv_init();
  static uint32_t t = 0;
  lv_tick_set_cb([]() { return t += 20; });
  fb = new uint8_t[240 * 240 * 3]();
  static uint8_t buf[240 * 240 * 2];
  lv_display_t *d = lv_display_create(240, 240);
  lv_display_set_buffers(d, buf, nullptr, sizeof(buf), LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(d, flushCb);

  gw(0, "LeviLuma-3A7F", false, true, -1, 100, 0, 0, 0, LVP_GW_KEY_REQUIRED);
  gw(1, "Wohnzimmer", false, true, -1, 100, 0, 0, 0, LVP_GW_PAIRING_OPEN);
  uiBuild();                 shot("01_unpaired");
  uiShowTile(3);             shot("02_gateway_unpaired");
  uiShowSearchForPreview();  shot("03_search");
  uiShowKeyForPreview(0);    shot("04_key");

  // gekoppelt, an, Koralle 75 %
  gw(1, "Wohnzimmer", true, true, 1, 75, 255, 107, 107);
  active = 1; version++;
  lv_screen_load(lv_obj_create(nullptr)); // frischer Bildschirm fuer den zweiten Aufbau
  uiBuild();                 shot("05_power_on");
  uiShowTile(1);             shot("06_brightness");
  uiShowTile(2);             shot("07_color");
  uiShowTile(3);             shot("08_gateway");
  gws[1].state.power = 0; version++;
  uiShowTile(0);             shot("09_power_off");
  gws[1].online = false; version++;
  uiShowTile(0);             shot("10_power_offline");
  return 0;
}
