#include "sim_script.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <fstream>
#include "mock_gateway.h"
#include "src/widgets/buttonmatrix/lv_buttonmatrix_private.h" // nur fuer click-button (Tastenflaechen)
#include "sdl_backend.h"
#include "sim_air.h"
#include "sim_clock.h"
#include "simulator_app.h"

namespace {

constexpr uint32_t DEFAULT_WAIT_MS = 4000;

// Symbole der LVGL-Schrift in Skripten als @NAME, z. B. @LEFT fuer LV_SYMBOL_LEFT
std::string symbol(const std::string &a) {
  static const struct {
    const char *name, *sym;
  } map[] = {{"@LEFT", LV_SYMBOL_LEFT},         {"@SETTINGS", LV_SYMBOL_SETTINGS},   {"@TRASH", LV_SYMBOL_TRASH},
             {"@REFRESH", LV_SYMBOL_REFRESH},   {"@OK", LV_SYMBOL_OK},               {"@BACKSPACE", LV_SYMBOL_BACKSPACE},
             {"@EYE_CLOSE", LV_SYMBOL_EYE_CLOSE}, {"@PLUS", LV_SYMBOL_PLUS},         {"@EDIT", LV_SYMBOL_EDIT},
             {"@LIST", LV_SYMBOL_LIST},         {"@HOME", LV_SYMBOL_HOME},           {"@WARNING", LV_SYMBOL_WARNING}};
  for (auto &m : map) {
    if (a == m.name) {
      return m.sym;
    }
  }
  return a;
}

std::vector<std::string> split(const std::string &s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
      i++;
    }
    if (i >= s.size() || s[i] == '#') {
      break;
    }
    std::string a;
    if (s[i] == '"') {
      i++;
      while (i < s.size() && s[i] != '"') {
        a += s[i++];
      }
      i++;
    } else {
      while (i < s.size() && s[i] != ' ' && s[i] != '\t') {
        a += s[i++];
      }
    }
    out.push_back(symbol(a));
  }
  return out;
}

bool visible(lv_obj_t *o) {
  for (lv_obj_t *p = o; p; p = lv_obj_get_parent(p)) {
    if (lv_obj_is_hidden(p)) {
      return false;
    }
  }
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  const SimDeviceSpec &d = SimBackend::spec();
  return a.x2 >= 0 && a.y2 >= 0 && a.x1 < d.width && a.y1 < d.height && a.x2 >= a.x1;
}

void collect(lv_obj_t *o, const lv_obj_class_t *cls, std::vector<lv_obj_t *> &out) {
  if (!o) {
    return;
  }
  if (lv_obj_has_class(o, cls) && visible(o)) {
    out.push_back(o);
  }
  uint32_t n = lv_obj_get_child_count(o);
  for (uint32_t i = 0; i < n; i++) {
    collect(lv_obj_get_child(o, (int32_t)i), cls, out);
  }
}

// Sichtbare Objekte einer Klasse auf dem Geraetedisplay (Bildschirm und obere Ebene fuer Dialoge)
std::vector<lv_obj_t *> visibleObjects(const lv_obj_class_t *cls) {
  std::vector<lv_obj_t *> out;
  lv_display_t *d = SimBackend::deviceDisplay();
  collect(lv_display_get_screen_active(d), cls, out);
  collect(lv_display_get_layer_top(d), cls, out);
  return out;
}

// Sichtbares Label, dessen Text needle enthaelt ("=Text": genau gleich); nth = wievielter Treffer (ab 0)
lv_obj_t *findLabel(const std::string &needle, int nth = 0) {
  bool exact = !needle.empty() && needle[0] == '=';
  std::string n = exact ? needle.substr(1) : needle;
  for (lv_obj_t *l : visibleObjects(&lv_label_class)) {
    const char *t = lv_label_get_text(l);
    if (t && (exact ? n == t : strstr(t, n.c_str()) != nullptr) && nth-- == 0) {
      return l;
    }
  }
  return nullptr;
}

void center(lv_obj_t *o, int32_t &x, int32_t &y) {
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  const SimDeviceSpec &d = SimBackend::spec();
  int32_t x1 = a.x1 < 0 ? 0 : a.x1, y1 = a.y1 < 0 ? 0 : a.y1;
  int32_t x2 = a.x2 >= d.width ? d.width - 1 : a.x2, y2 = a.y2 >= d.height ? d.height - 1 : a.y2;
  x = (x1 + x2) / 2;
  y = (y1 + y2) / 2;
}

// Taste einer sichtbaren Buttonmatrix (auch lv_keyboard) per Text suchen; Lage aus den Tastenflaechen
bool findMatrixButton(const std::string &text, int32_t &x, int32_t &y) {
  for (lv_obj_t *o : visibleObjects(&lv_buttonmatrix_class)) {
    lv_buttonmatrix_t *bm = (lv_buttonmatrix_t *)o;
    for (uint32_t i = 0; i < bm->btn_cnt; i++) {
      const char *t = lv_buttonmatrix_get_button_text(o, i);
      if (t && text == t) {
        lv_area_t a = bm->button_areas[i];
        lv_area_t c;
        lv_obj_get_coords(o, &c);
        x = c.x1 + (a.x1 + a.x2) / 2;
        y = c.y1 + (a.y1 + a.y2) / 2;
        return true;
      }
    }
  }
  return false;
}

bool gwField(MockGateway *g, const std::string &f, const std::string &want, std::string &have) {
  MockGatewaySnapshot s = g->snapshot();
  char b[32];
  if (f == "power") {
    snprintf(b, sizeof(b), "%d", s.light.power);
  } else if (f == "brightness") {
    snprintf(b, sizeof(b), "%u", s.light.brightness);
  } else if (f == "rgb") {
    snprintf(b, sizeof(b), "%02X%02X%02X", s.light.r, s.light.g, s.light.b);
  } else if (f == "music") {
    snprintf(b, sizeof(b), "%d", s.light.music ? 1 : 0);
  } else if (f == "ble") {
    snprintf(b, sizeof(b), "%d", s.ble ? 1 : 0);
  } else if (f == "online") {
    snprintf(b, sizeof(b), "%d", s.online ? 1 : 0);
  } else if (f == "paired") {
    snprintf(b, sizeof(b), "%d", s.pairedCount);
  } else {
    have = "unbekanntes Feld";
    return false;
  }
  have = b;
  if (have.size() != want.size()) {
    return false;
  }
  for (size_t i = 0; i < have.size(); i++) {
    if (toupper((unsigned char)have[i]) != toupper((unsigned char)want[i])) {
      return false;
    }
  }
  return true;
}

int num(const std::vector<std::string> &a, size_t i, int def) {
  return i < a.size() ? atoi(a[i].c_str()) : def;
}

} // namespace

bool SimScript::load(const std::string &path) {
  std::ifstream f(path);
  if (!f) {
    error_ = "Skript nicht lesbar: " + path;
    return false;
  }
  std::string line;
  int no = 0;
  while (std::getline(f, line)) {
    no++;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    std::vector<std::string> a = split(line);
    if (!a.empty()) {
      lines_.push_back({no, a});
    }
  }
  return true;
}

SimScript::Result SimScript::fail(const Line &l, const std::string &msg) {
  error_ = "Zeile " + std::to_string(l.no) + " (" + l.args[0] + "): " + msg;
  return Result::Failed;
}

SimScript::Result SimScript::step() {
  while (pc_ < lines_.size()) {
    if (phase_ == 0 && stepStart_ == 0) {
      stepStart_ = simMillis();
    }
    Result r = exec(lines_[pc_]);
    if (r == Result::Failed) {
      return r;
    }
    if (r == Result::Running) {
      return Result::Running;
    }
    pc_++; // Done: naechste Anweisung
    phase_ = 0;
    stepStart_ = 0;
    if (error_ == "quit") {
      error_.clear();
      return Result::Done;
    }
  }
  return Result::Done;
}

SimScript::Result SimScript::exec(const Line &l) {
  const std::vector<std::string> &a = l.args;
  const std::string &c = a[0];
  uint32_t el = simMillis() - stepStart_;

  if (c == "wait") {
    return el >= (uint32_t)num(a, 1, 0) ? Result::Done : Result::Running;
  }
  if (c == "echo") {
    std::string t;
    for (size_t i = 1; i < a.size(); i++) {
      t += (i > 1 ? " " : "") + a[i];
    }
    printf("[skript] %s\n", t.c_str());
    return Result::Done;
  }
  if (c == "quit") {
    error_ = "quit";
    return Result::Done;
  }
  if (c == "press" || c == "move" || c == "release") {
    SimBackend::injectMouse(false, c == "press" ? 1 : c == "move" ? 0 : 2, num(a, 1, 0), num(a, 2, 0));
    return Result::Done;
  }
  if (c == "click" || c == "click-text" || c == "click-switch" || c == "click-slider" || c == "click-button" ||
      c == "click-close") {
    static int32_t x, y;
    if (phase_ == 0) {
      if (c == "click") {
        if (a.size() < 3) {
          return fail(l, "click <x> <y>");
        }
        x = num(a, 1, 0);
        y = num(a, 2, 0);
      } else if (c == "click-switch") {
        // click-switch <n>: Mitte des n-ten sichtbaren Schalters (ab 0)
        std::vector<lv_obj_t *> sw = visibleObjects(&lv_switch_class);
        size_t n = (size_t)num(a, 1, 0);
        if (n >= sw.size()) {
          return el > DEFAULT_WAIT_MS ? fail(l, "kein sichtbarer Schalter " + a[1]) : Result::Running;
        }
        center(sw[n], x, y);
      } else if (c == "click-slider") {
        // click-slider <n> <prozent>: Klick auf die Stelle bei <prozent> der Reglerbreite
        std::vector<lv_obj_t *> sl = visibleObjects(&lv_slider_class);
        size_t n = (size_t)num(a, 1, 0);
        if (n >= sl.size()) {
          return el > DEFAULT_WAIT_MS ? fail(l, "kein sichtbarer Regler " + a[1]) : Result::Running;
        }
        lv_area_t ar;
        lv_obj_get_coords(sl[n], &ar);
        int pct = num(a, 2, 50);
        x = ar.x1 + (int32_t)((lv_area_get_width(&ar) - 1) * pct / 100);
        y = (ar.y1 + ar.y2) / 2;
      } else if (c == "click-close") {
        // click-close: Schliessen-Knopf (X) des obersten Dialogs
        std::vector<lv_obj_t *> b = visibleObjects(&lv_msgbox_header_button_class);
        if (b.empty()) {
          return el > DEFAULT_WAIT_MS ? fail(l, "kein Dialog mit Schliessen-Knopf") : Result::Running;
        }
        center(b.back(), x, y);
      } else if (c == "click-button") {
        // click-button "<text>": Taste einer Buttonmatrix/Bildschirmtastatur
        if (a.size() < 2 || !findMatrixButton(a[1], x, y)) {
          return el > DEFAULT_WAIT_MS ? fail(l, "keine sichtbare Taste \"" + (a.size() > 1 ? a[1] : "") + "\"") : Result::Running;
        }
      } else {
        if (a.size() < 2) {
          return fail(l, "click-text \"Text\"");
        }
        lv_obj_t *o = findLabel(a[1], num(a, 2, 0));
        if (!o) {
          return el > DEFAULT_WAIT_MS ? fail(l, "Text nicht sichtbar: \"" + a[1] + "\"") : Result::Running;
        }
        center(o, x, y);
      }
      SimBackend::injectMouse(false, 0, x, y);
      SimBackend::injectMouse(false, 1, x, y);
      phase_ = 1;
      stepStart_ = simMillis();
      return Result::Running;
    }
    if (phase_ == 1) {
      if (el < 80) {
        return Result::Running;
      }
      SimBackend::injectMouse(false, 2, x, y);
      phase_ = 2;
      stepStart_ = simMillis();
      return Result::Running;
    }
    return el >= 150 ? Result::Done : Result::Running; // Oberflaeche reagieren lassen
  }
  if (c == "drag") {
    if (a.size() < 5) {
      return fail(l, "drag <x1> <y1> <x2> <y2> [ms]");
    }
    int x1 = num(a, 1, 0), y1 = num(a, 2, 0), x2 = num(a, 3, 0), y2 = num(a, 4, 0), ms = num(a, 5, 300);
    if (phase_ == 0) {
      SimBackend::injectMouse(false, 0, x1, y1);
      SimBackend::injectMouse(false, 1, x1, y1);
      phase_ = 1;
      stepStart_ = simMillis();
      return Result::Running;
    }
    if (phase_ == 1) {
      float t = el >= (uint32_t)ms ? 1.0f : (float)el / ms;
      SimBackend::injectMouse(false, 0, x1 + (int)((x2 - x1) * t), y1 + (int)((y2 - y1) * t));
      if (t >= 1.0f) {
        SimBackend::injectMouse(false, 2, x2, y2);
        phase_ = 2;
        stepStart_ = simMillis();
      }
      return Result::Running;
    }
    // Wisch-/Einrast-Animation abwarten (mindestens 150 ms, hoechstens 2 s)
    return el >= 2000 || (el >= 150 && lv_anim_count_running() == 0) ? Result::Done : Result::Running;
  }
  if (c == "expect-text" || c == "expect-no-text") {
    if (a.size() < 2) {
      return fail(l, c + " \"Text\" [ms]");
    }
    bool want = c == "expect-text";
    if ((findLabel(a[1]) != nullptr) == want) {
      return Result::Done;
    }
    return el > (uint32_t)num(a, 2, DEFAULT_WAIT_MS) ? fail(l, want ? "nicht sichtbar: \"" + a[1] + "\"" : "noch sichtbar: \"" + a[1] + "\"")
                                                     : Result::Running;
  }
  if (c == "expect-switch") {
    // expect-switch <n> on|off|enabled|disabled : n-ter sichtbarer Schalter (ab 0) auf dem Geraetedisplay
    if (a.size() < 3) {
      return fail(l, "expect-switch <n> on|off|enabled|disabled [ms]");
    }
    std::vector<lv_obj_t *> sw = visibleObjects(&lv_switch_class);
    size_t n = (size_t)num(a, 1, 0);
    bool ok = false;
    std::string have = "kein Schalter";
    if (n < sw.size()) {
      bool on = lv_obj_has_state(sw[n], LV_STATE_CHECKED), dis = lv_obj_has_state(sw[n], LV_STATE_DISABLED);
      have = std::string(on ? "on" : "off") + "," + (dis ? "disabled" : "enabled");
      ok = (a[2] == "on" && on) || (a[2] == "off" && !on) || (a[2] == "disabled" && dis) || (a[2] == "enabled" && !dis);
    }
    if (ok) {
      return Result::Done;
    }
    return el > (uint32_t)num(a, 3, DEFAULT_WAIT_MS) ? fail(l, "Schalter " + a[1] + " ist " + have) : Result::Running;
  }
  if (c == "expect-slider") {
    // expect-slider <n> <wert> : n-ter sichtbarer Regler
    std::vector<lv_obj_t *> sl = visibleObjects(&lv_slider_class);
    size_t n = (size_t)num(a, 1, 0);
    int want = num(a, 2, -1);
    if (n < sl.size() && lv_slider_get_value(sl[n]) == want) {
      return Result::Done;
    }
    std::string have = n < sl.size() ? std::to_string(lv_slider_get_value(sl[n])) : "kein Regler";
    return el > (uint32_t)num(a, 3, DEFAULT_WAIT_MS) ? fail(l, "Regler " + a[1] + " steht auf " + have) : Result::Running;
  }
  if (c == "expect-gw") {
    // expect-gw <n> <feld> <wert> [ms] : Zustand im simulierten Gateway n (ab 1)
    if (a.size() < 4) {
      return fail(l, "expect-gw <n> <feld> <wert> [ms]");
    }
    size_t n = (size_t)num(a, 1, 0);
    if (n < 1 || n > simGateways().size()) {
      return fail(l, "kein Gateway " + a[1]);
    }
    std::string have;
    if (gwField(simGateways()[n - 1], a[2], a[3], have)) {
      return Result::Done;
    }
    return el > (uint32_t)num(a, 4, DEFAULT_WAIT_MS) ? fail(l, a[2] + " ist " + have + ", erwartet " + a[3]) : Result::Running;
  }
  if (c == "gw") {
    // gw <n> online|ble|music|power <0|1> | gw <n> brightness <1..100> | gw <n> pairing
    size_t n = (size_t)num(a, 1, 0);
    if (a.size() < 3 || n < 1 || n > simGateways().size()) {
      return fail(l, "gw <n> <befehl> [wert]");
    }
    MockGateway *g = simGateways()[n - 1];
    int v = num(a, 3, 0);
    const std::string &k = a[2];
    if (k == "online") {
      g->post(MockGateway::Cmd::SetOnline, v);
    } else if (k == "ble") {
      g->post(MockGateway::Cmd::SetBle, v);
    } else if (k == "music") {
      g->post(MockGateway::Cmd::SetMusic, v);
    } else if (k == "power") {
      g->post(MockGateway::Cmd::SetPower, v);
    } else if (k == "brightness") {
      g->post(MockGateway::Cmd::SetBrightness, v);
    } else if (k == "pairing") {
      g->post(MockGateway::Cmd::OpenPairing, 60000);
    } else {
      return fail(l, "unbekannter Befehl " + k);
    }
    return Result::Done;
  }
  if (c == "loss") {
    simAir().setLossPercent(num(a, 1, 0));
    return Result::Done;
  }
  if (c == "shot" || c == "shot-device") {
    if (a.size() < 2) {
      return fail(l, c + " <datei.bmp>");
    }
    if (phase_ == 0) { // einen Durchlauf warten, damit LVGL neu gezeichnet hat
      phase_ = 1;
      return Result::Running;
    }
    bool ok = c == "shot" ? SimBackend::saveWindow(a[1].c_str()) : SimBackend::saveDevice(a[1].c_str());
    if (!ok) {
      return fail(l, "Bild nicht gespeichert: " + a[1]);
    }
    printf("[skript] Bild %s\n", a[1].c_str());
    return Result::Done;
  }
  return fail(l, "unbekannte Anweisung");
}
