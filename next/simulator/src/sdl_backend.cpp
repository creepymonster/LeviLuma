#include "sdl_backend.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#define SDL_MAIN_HANDLED
#include <SDL.h>

namespace {

constexpr int MARGIN = 24;
constexpr uint32_t BG = 0x18181B;    // Fensterhintergrund (zinc-900)
constexpr uint32_t BEZEL = 0x3F3F46; // Gehaeuserand um das Display (zinc-700)
constexpr int BEZEL_PX = 8;          // Breite des Rands in Fensterpixeln

struct Surface {
  lv_display_t *disp = nullptr;
  int w = 0, h = 0;
  std::vector<uint16_t> fb; // RGB565 wie die Firmware
  std::vector<uint8_t> buf; // LVGL-Zeichenpuffer
  SDL_Texture *tex = nullptr;
  bool dirty = true;
  SDL_Rect rect{}; // Lage im Fenster
};

SimDeviceSpec gSpec;
int gZoom = 2;
SDL_Window *gWin = nullptr;
SDL_Renderer *gRen = nullptr;
SDL_Texture *gBezel = nullptr; // rund: Kreisrand hinter dem Display
Surface gDev, gPanel;
std::vector<uint8_t> gMask; // Abdeckung 0..255 je Pixel (rund), sonst 255
int gBacklight = 100;
std::string gTitle;

// Maus: wer hat die laufende Beruehrung?
enum class Capture { None, Device, Panel };
Capture gCapture = Capture::None;
SimPointer gDevPtr{false, 0, 0}, gPanelPtr{false, 0, 0};
lv_indev_t *gPanelIndev = nullptr;

void flushCb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  Surface &s = d == gDev.disp ? gDev : gPanel;
  int w = lv_area_get_width(a);
  const uint16_t *src = (const uint16_t *)px;
  for (int y = a->y1; y <= a->y2; y++) {
    if (y < 0 || y >= s.h) {
      src += w;
      continue;
    }
    for (int x = 0; x < w; x++) {
      int xx = a->x1 + x;
      if (xx >= 0 && xx < s.w) {
        s.fb[y * s.w + xx] = src[x];
      }
    }
    src += w;
  }
  s.dirty = true;
  lv_display_flush_ready(d);
}

bool createSurface(Surface &s, int w, int h) {
  s.w = w;
  s.h = h;
  s.fb.assign((size_t)w * h, 0);
  s.buf.assign((size_t)w * h * 2, 0); // ein Vollbildpuffer, Teilaktualisierung
  s.disp = lv_display_create(w, h);
  if (!s.disp) {
    return false;
  }
  lv_display_set_color_format(s.disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(s.disp, s.buf.data(), nullptr, (uint32_t)s.buf.size(), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(s.disp, flushCb);
  s.tex = SDL_CreateTexture(gRen, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
  if (s.tex) {
    SDL_SetTextureBlendMode(s.tex, SDL_BLENDMODE_BLEND);
  }
  return s.tex != nullptr;
}

inline void rgb565(uint16_t v, int &r, int &g, int &b) {
  r = ((v >> 11) & 31) * 255 / 31;
  g = ((v >> 5) & 63) * 255 / 63;
  b = (v & 31) * 255 / 31;
}

// RGB565 -> ARGB8888. Geraet: Hintergrundbeleuchtung und Maske (ausserhalb des Kreises durchsichtig)
void convert(const Surface &s, bool device, uint32_t *out, int pitchPx) {
  int br = device ? gBacklight : 100;
  for (int y = 0; y < s.h; y++) {
    for (int x = 0; x < s.w; x++) {
      int r, g, b;
      rgb565(s.fb[y * s.w + x], r, g, b);
      uint32_t alpha = 255;
      if (device) {
        r = r * br / 100;
        g = g * br / 100;
        b = b * br / 100;
        alpha = gMask[y * s.w + x];
      }
      out[y * pitchPx + x] = alpha << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
    }
  }
}

// Runder Gehaeuserand als Textur (geglaettet), Durchmesser = Display + 2 * BEZEL_PX
SDL_Texture *makeRoundBezel(int diameter) {
  SDL_Texture *t = SDL_CreateTexture(gRen, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, diameter, diameter);
  if (!t) {
    return nullptr;
  }
  std::vector<uint32_t> px((size_t)diameter * diameter);
  const float c = diameter / 2.0f, r = diameter / 2.0f;
  for (int y = 0; y < diameter; y++) {
    for (int x = 0; x < diameter; x++) {
      int in = 0;
      for (int sy = 0; sy < 4; sy++) {
        for (int sx = 0; sx < 4; sx++) {
          float dx = x + (sx + 0.5f) / 4.0f - c, dy = y + (sy + 0.5f) / 4.0f - c;
          in += dx * dx + dy * dy <= r * r;
        }
      }
      px[(size_t)y * diameter + x] = (uint32_t)(in * 255 / 16) << 24 | BEZEL;
    }
  }
  SDL_UpdateTexture(t, nullptr, px.data(), diameter * 4);
  SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  return t;
}

void upload(Surface &s, bool device) {
  void *pixels;
  int pitch;
  if (SDL_LockTexture(s.tex, nullptr, &pixels, &pitch) == 0) {
    convert(s, device, (uint32_t *)pixels, pitch / 4);
    SDL_UnlockTexture(s.tex);
  }
  s.dirty = false;
}

void buildMask() {
  gMask.assign((size_t)gSpec.width * gSpec.height, 255);
  if (!gSpec.round) {
    return;
  }
  // Abdeckung des Kreises (Durchmesser = Displaybreite) mit 4x4-Ueberabtastung fuer einen glatten Rand
  const float cx = gSpec.width / 2.0f, cy = gSpec.height / 2.0f, r = gSpec.width / 2.0f;
  for (int y = 0; y < gSpec.height; y++) {
    for (int x = 0; x < gSpec.width; x++) {
      int in = 0;
      for (int sy = 0; sy < 4; sy++) {
        for (int sx = 0; sx < 4; sx++) {
          float px = x + (sx + 0.5f) / 4.0f - cx, py = y + (sy + 0.5f) / 4.0f - cy;
          in += px * px + py * py <= r * r;
        }
      }
      gMask[y * gSpec.width + x] = (uint8_t)(in * 255 / 16);
    }
  }
}

bool insideDisplay(int32_t x, int32_t y) {
  if (x < 0 || y < 0 || x >= gSpec.width || y >= gSpec.height) {
    return false;
  }
  return gMask[y * gSpec.width + x] >= 128;
}

int32_t clampi(int32_t v, int32_t lo, int32_t hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

void toDevice(int wx, int wy, int32_t &x, int32_t &y) {
  x = (wx - gDev.rect.x) / gZoom;
  y = (wy - gDev.rect.y) / gZoom;
}

void toPanel(int wx, int wy, int32_t &x, int32_t &y) {
  x = wx - gPanel.rect.x;
  y = wy - gPanel.rect.y;
}

void mouseEvent(int type, int wx, int wy) {
  int32_t dx, dy, px, py;
  toDevice(wx, wy, dx, dy);
  toPanel(wx, wy, px, py);
  if (type == 1) {
    if (insideDisplay(dx, dy)) {
      gCapture = Capture::Device;
      gDevPtr = {true, dx, dy};
    } else if (px >= 0 && py >= 0 && px < gPanel.w && py < gPanel.h) {
      gCapture = Capture::Panel;
      gPanelPtr = {true, px, py};
    }
  } else if (type == 0) {
    if (gCapture == Capture::Device) {
      gDevPtr.x = clampi(dx, 0, gSpec.width - 1);
      gDevPtr.y = clampi(dy, 0, gSpec.height - 1);
    } else if (gCapture == Capture::Panel) {
      gPanelPtr.x = clampi(px, 0, gPanel.w - 1);
      gPanelPtr.y = clampi(py, 0, gPanel.h - 1);
    }
  } else {
    gDevPtr.pressed = false;
    gPanelPtr.pressed = false;
    gCapture = Capture::None;
  }
}

void panelRead(lv_indev_t *, lv_indev_data_t *d) {
  d->point.x = gPanelPtr.x;
  d->point.y = gPanelPtr.y;
  d->state = gPanelPtr.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void updateTitle() {
  char t[256];
  snprintf(t, sizeof(t), "LeviLuma Simulator - %s (%dx%d) - SIMULATION%s", gSpec.name, gSpec.width, gSpec.height, gTitle.c_str());
  SDL_SetWindowTitle(gWin, t);
}

bool saveBmp(const char *path, int w, int h, const uint32_t *argb, int pitchPx) {
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!s) {
    return false;
  }
  for (int y = 0; y < h; y++) {
    memcpy((uint8_t *)s->pixels + y * s->pitch, argb + y * pitchPx, (size_t)w * 4);
  }
  bool ok = SDL_SaveBMP(s, path) == 0;
  SDL_FreeSurface(s);
  return ok;
}

} // namespace

namespace SimBackend {

bool init(const SimDeviceSpec &spec, int zoom, int panelWidth) {
  gSpec = spec;
  gZoom = zoom < 1 ? 1 : zoom > 4 ? 4 : zoom;
  SDL_SetMainReady();
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return false;
  }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // Pixel scharf vergroessern
  int devW = spec.width * gZoom, devH = spec.height * gZoom;
  int panelH = devH + 2 * MARGIN < 640 ? 640 : devH + 2 * MARGIN;
  int winW = MARGIN * 2 + devW + panelWidth, winH = panelH;
  gWin = SDL_CreateWindow("LeviLuma Simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH, SDL_WINDOW_SHOWN);
  if (!gWin) {
    fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
    return false;
  }
  gRen = SDL_CreateRenderer(gWin, -1, SDL_RENDERER_ACCELERATED);
  if (!gRen) {
    gRen = SDL_CreateRenderer(gWin, -1, SDL_RENDERER_SOFTWARE);
  }
  if (!gRen) {
    fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
    return false;
  }
  buildMask();
  // Reihenfolge: Geraetedisplay zuerst, es bleibt das LVGL-Standarddisplay
  if (!createSurface(gDev, spec.width, spec.height) || !createSurface(gPanel, panelWidth, panelH)) {
    fprintf(stderr, "Display anlegen fehlgeschlagen: %s\n", SDL_GetError());
    return false;
  }
  lv_display_set_default(gDev.disp);
  gDev.rect = {MARGIN, (panelH - devH) / 2, devW, devH};
  gPanel.rect = {MARGIN * 2 + devW, 0, panelWidth, panelH};
  if (spec.round) {
    gBezel = makeRoundBezel(devW + 2 * BEZEL_PX);
  }
  gPanelIndev = lv_indev_create();
  lv_indev_set_type(gPanelIndev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(gPanelIndev, panelRead);
  lv_indev_set_display(gPanelIndev, gPanel.disp);
  updateTitle();
  return true;
}

void shutdown() {
  if (gBezel) {
    SDL_DestroyTexture(gBezel);
  }
  if (gRen) {
    SDL_DestroyRenderer(gRen);
  }
  if (gWin) {
    SDL_DestroyWindow(gWin);
  }
  SDL_Quit();
}

lv_display_t *deviceDisplay() {
  return gDev.disp;
}

lv_display_t *panelDisplay() {
  return gPanel.disp;
}

const SimDeviceSpec &spec() {
  return gSpec;
}

SimPointer devicePointer() {
  return gDevPtr;
}

void setBacklight(int percent) {
  percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
  if (percent != gBacklight) {
    gBacklight = percent;
    gDev.dirty = true;
  }
}

int backlight() {
  return gBacklight;
}

bool pump() {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    switch (e.type) {
    case SDL_QUIT:
      return false;
    case SDL_MOUSEBUTTONDOWN:
      if (e.button.button == SDL_BUTTON_LEFT) {
        mouseEvent(1, e.button.x, e.button.y);
      }
      break;
    case SDL_MOUSEBUTTONUP:
      if (e.button.button == SDL_BUTTON_LEFT) {
        mouseEvent(2, e.button.x, e.button.y);
      }
      break;
    case SDL_MOUSEMOTION:
      mouseEvent(0, e.motion.x, e.motion.y);
      break;
    case SDL_MOUSEWHEEL: { // Steuerleiste scrollen
      int mx, my;
      SDL_GetMouseState(&mx, &my);
      if (mx >= gPanel.rect.x) {
        lv_obj_scroll_by_bounded(lv_display_get_screen_active(gPanel.disp), 0, e.wheel.y * 40, LV_ANIM_OFF);
      }
      break;
    }
    case SDL_WINDOWEVENT:
      if (e.window.event == SDL_WINDOWEVENT_EXPOSED) {
        gDev.dirty = gPanel.dirty = true;
      }
      break;
    case SDL_KEYDOWN:
      if (e.key.keysym.sym == SDLK_ESCAPE && (e.key.keysym.mod & KMOD_SHIFT)) {
        return false;
      }
      break;
    default:
      break;
    }
  }
  return true;
}

static void draw() {
  if (gDev.dirty) {
    upload(gDev, true);
  }
  if (gPanel.dirty) {
    upload(gPanel, false);
  }
  SDL_SetRenderDrawColor(gRen, (BG >> 16) & 255, (BG >> 8) & 255, BG & 255, 255);
  SDL_RenderClear(gRen);
  // Gehaeuse: rund als Ring (Maske im Bild), eckig als Rahmen
  SDL_Rect frame = {gDev.rect.x - BEZEL_PX, gDev.rect.y - BEZEL_PX, gDev.rect.w + 2 * BEZEL_PX, gDev.rect.h + 2 * BEZEL_PX};
  if (gBezel) {
    SDL_RenderCopy(gRen, gBezel, nullptr, &frame);
  } else {
    SDL_SetRenderDrawColor(gRen, (BEZEL >> 16) & 255, (BEZEL >> 8) & 255, BEZEL & 255, 255);
    SDL_RenderFillRect(gRen, &frame);
  }
  SDL_RenderCopy(gRen, gDev.tex, nullptr, &gDev.rect);
  SDL_RenderCopy(gRen, gPanel.tex, nullptr, &gPanel.rect);
}

void present() {
  draw();
  SDL_RenderPresent(gRen);
}

void setTitleSuffix(const std::string &s) {
  if (s != gTitle) {
    gTitle = s;
    updateTitle();
  }
}

void injectMouse(bool panel, int type, int32_t x, int32_t y) {
  int wx, wy;
  if (panel) {
    wx = gPanel.rect.x + x;
    wy = gPanel.rect.y + y;
  } else {
    wx = gDev.rect.x + x * gZoom + gZoom / 2;
    wy = gDev.rect.y + y * gZoom + gZoom / 2;
  }
  // Ueber die SDL-Ereignisschlange, damit derselbe Weg wie bei einer echten Maus genommen wird
  SDL_Event e{};
  if (type == 0) {
    e.type = SDL_MOUSEMOTION;
    e.motion.windowID = SDL_GetWindowID(gWin);
    e.motion.x = wx;
    e.motion.y = wy;
  } else {
    e.type = type == 1 ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    e.button.windowID = SDL_GetWindowID(gWin);
    e.button.button = SDL_BUTTON_LEFT;
    e.button.state = type == 1 ? SDL_PRESSED : SDL_RELEASED;
    e.button.clicks = 1;
    e.button.x = wx;
    e.button.y = wy;
  }
  SDL_PushEvent(&e);
}

bool saveWindow(const char *path) {
  int w, h;
  SDL_GetRendererOutputSize(gRen, &w, &h);
  std::vector<uint32_t> px((size_t)w * h);
  draw(); // Lesen vor dem Anzeigen: der Inhalt des hinteren Puffers ist danach undefiniert
  bool ok = SDL_RenderReadPixels(gRen, nullptr, SDL_PIXELFORMAT_ARGB8888, px.data(), w * 4) == 0;
  SDL_RenderPresent(gRen);
  if (!ok) {
    fprintf(stderr, "Bildschirmfoto: %s\n", SDL_GetError());
    return false;
  }
  return saveBmp(path, w, h, px.data(), w);
}

bool saveDevice(const char *path) {
  std::vector<uint32_t> px((size_t)gDev.w * gDev.h);
  convert(gDev, true, px.data(), gDev.w); // Alphakanal bleibt erhalten (rund: ausserhalb durchsichtig)
  return saveBmp(path, gDev.w, gDev.h, px.data(), gDev.w);
}

} // namespace SimBackend
