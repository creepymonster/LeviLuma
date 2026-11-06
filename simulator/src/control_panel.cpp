#include "control_panel.h"
#include <stdio.h>
#include <string.h>
#include "Palette.h"

namespace {

struct GwWidgets {
  MockGateway *gw;
  lv_obj_t *title, *online, *ble, *music, *swatch, *info;
};

std::vector<GwWidgets> gWs;
SimAir *gAir = nullptr;
lv_obj_t *gLoss = nullptr, *gLossLbl = nullptr, *gAirLbl = nullptr;

constexpr uint32_t WARN = 0xF59E0B;

lv_obj_t *box(lv_obj_t *parent, bool card) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_set_width(o, LV_PCT(100));
  lv_obj_set_height(o, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(o, 4, 0);
  lv_obj_set_scrollable(o, false);
  if (card) {
    lv_obj_set_style_pad_all(o, 8, 0);
  } else {
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
  }
  return o;
}

lv_obj_t *row(lv_obj_t *parent) {
  lv_obj_t *r = box(parent, false);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(r, 6, 0);
  return r;
}

lv_obj_t *text(lv_obj_t *parent, const char *t, bool muted = false) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, t);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
  if (muted) {
    lv_obj_set_style_text_opa(l, LV_OPA_70, 0);
  }
  return l;
}

lv_obj_t *toggle(lv_obj_t *parent, const char *name, lv_event_cb_t cb, void *user) {
  lv_obj_t *r = row(parent);
  lv_obj_set_width(r, LV_SIZE_CONTENT);
  lv_obj_t *s = lv_switch_create(r);
  lv_obj_set_size(s, 40, 22);
  lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, user);
  text(r, name);
  return s;
}

bool userInput() {
  return lv_indev_active() != nullptr; // nur Eingaben, nicht das Nachfuehren aus dem Zustand
}

void onOnline(lv_event_t *e) {
  if (userInput()) {
    GwWidgets *w = (GwWidgets *)lv_event_get_user_data(e);
    w->gw->post(MockGateway::Cmd::SetOnline, lv_obj_has_state(w->online, LV_STATE_CHECKED));
  }
}

void onBle(lv_event_t *e) {
  if (userInput()) {
    GwWidgets *w = (GwWidgets *)lv_event_get_user_data(e);
    w->gw->post(MockGateway::Cmd::SetBle, lv_obj_has_state(w->ble, LV_STATE_CHECKED));
  }
}

void onMusic(lv_event_t *e) {
  if (userInput()) {
    GwWidgets *w = (GwWidgets *)lv_event_get_user_data(e);
    w->gw->post(MockGateway::Cmd::SetMusic, lv_obj_has_state(w->music, LV_STATE_CHECKED));
  }
}

void onPairing(lv_event_t *e) {
  GwWidgets *w = (GwWidgets *)lv_event_get_user_data(e);
  w->gw->post(MockGateway::Cmd::OpenPairing, 60000);
}

void onLoss(lv_event_t *) {
  gAir->setLossPercent(lv_slider_get_value(gLoss));
}

void sync(lv_obj_t *sw, bool on) {
  if (!lv_obj_has_state(sw, LV_STATE_PRESSED)) {
    lv_obj_set_state(sw, LV_STATE_CHECKED, on);
  }
}

const char *paletteName(uint8_t r, uint8_t g, uint8_t b) {
  for (int k = 0; k < LeviPalette::N; k++) {
    uint8_t pr, pg, pb;
    LeviPalette::get(k, pr, pg, pb);
    if (pr == r && pg == g && pb == b) {
      return LeviPalette::name(k);
    }
  }
  return "eigene Farbe";
}

void refresh(lv_timer_t *) {
  for (GwWidgets &w : gWs) {
    MockGatewaySnapshot s = w.gw->snapshot();
    sync(w.online, s.online);
    sync(w.ble, s.ble);
    sync(w.music, s.light.music);
    const LightSnapshot &l = s.light;
    // Simulierte Ausgabe des Bands: Farbe mal Helligkeit, aus = dunkel
    bool lit = s.ble && l.power == 1;
    lv_color_t c = lit ? lv_color_make(l.r * l.brightness / 100, l.g * l.brightness / 100, l.b * l.brightness / 100) : lv_color_hex(0x27272A);
    lv_obj_set_style_bg_color(w.swatch, c, 0);
    char id[18], t[360], key[64] = "";
    lvp_id_to_str(s.id, id, sizeof(id));
    if (s.hasKey) {
      snprintf(key, sizeof(key), "\nSchluessel (simuliert):\n%s", s.keyHex);
    }
    snprintf(t, sizeof(t), "Licht: %s, %u %%, #%02X%02X%02X (%s%s)\nMusikmodus %s, Revision %lu (ans Band %lu)\nController gekoppelt: %d, %s%s\nRahmen: %lu empfangen, %lu gesendet, %lu verworfen",
             l.power == 1 ? "an" : l.power == 0 ? "aus" : "unbekannt", l.brightness, l.r, l.g, l.b, paletteName(l.r, l.g, l.b),
             l.raw ? ", roh" : "", l.music ? "an" : "aus", (unsigned long)l.revision, (unsigned long)l.sentRevision, s.pairedCount,
             s.hasKey ? "mit Schluessel" : s.pairingOpen ? "Kopplung offen" : "Kopplung zu", key, (unsigned long)s.rxFrames,
             (unsigned long)s.txFrames, (unsigned long)s.rejected);
    lv_label_set_text(w.info, t);
    char title[80];
    snprintf(title, sizeof(title), "%s  %s", s.name, s.online ? "" : "(offline)");
    lv_label_set_text(w.title, title);
  }
  SimAir::Stats a = gAir->stats();
  char t[160];
  snprintf(t, sizeof(t), "Zugestellt %lu, verloren %lu,\nSchluessel passt nicht %lu, ohne Peer %lu", (unsigned long)a.delivered,
           (unsigned long)a.lost, (unsigned long)a.keyMismatch, (unsigned long)a.noPeer);
  lv_label_set_text(gAirLbl, t);
  snprintf(t, sizeof(t), "Paketverlust %d %%", gAir->lossPercent());
  lv_label_set_text(gLossLbl, t);
  if (!lv_obj_has_state(gLoss, LV_STATE_PRESSED)) {
    lv_slider_set_value(gLoss, gAir->lossPercent(), LV_ANIM_OFF);
  }
}

} // namespace

void controlPanelBuild(lv_display_t *panel, const char *deviceName, std::vector<MockGateway *> gateways, SimAir &air) {
  gAir = &air;
  lv_display_t *prev = lv_display_get_default();
  lv_display_set_default(panel); // nur waehrend des Aufbaus; danach wieder das Geraetedisplay
  lv_theme_t *th = lv_theme_default_init(panel, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_AMBER), true, &lv_font_montserrat_14);
  lv_display_set_theme(panel, th);

  lv_obj_t *scr = lv_display_get_screen_active(panel);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x18181B), 0);
  lv_obj_set_style_pad_all(scr, 10, 0);
  lv_obj_set_style_pad_row(scr, 8, 0);
  lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *h = text(scr, "SIMULATION - kein echtes Geraet");
  lv_obj_set_style_text_color(h, lv_color_hex(WARN), 0);
  lv_obj_set_style_text_font(h, &lv_font_montserrat_16, 0);
  char sub[160];
  snprintf(sub, sizeof(sub), "Links: Original-Oberflaeche %s.\nGateways, Funk und Band sind simuliert.", deviceName);
  lv_obj_t *sl = text(scr, sub, true);
  lv_label_set_long_mode(sl, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width(sl, LV_PCT(100));

  gWs.clear();
  gWs.reserve(gateways.size());
  for (MockGateway *g : gateways) {
    gWs.push_back(GwWidgets{g, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr});
    GwWidgets &w = gWs.back();
    lv_obj_t *card = box(scr, true);
    lv_obj_t *top = row(card);
    w.swatch = lv_obj_create(top);
    lv_obj_set_size(w.swatch, 22, 22);
    lv_obj_set_style_radius(w.swatch, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(w.swatch, 0, 0);
    lv_obj_set_scrollable(w.swatch, false);
    w.title = text(top, g->config().name);
    lv_obj_set_style_text_font(w.title, &lv_font_montserrat_16, 0);
    lv_obj_t *r1 = row(card);
    lv_obj_set_flex_flow(r1, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(r1, 4, 0);
    w.online = toggle(r1, "Online", onOnline, &w);
    w.ble = toggle(r1, "Band", onBle, &w);
    w.music = toggle(r1, "Musik", onMusic, &w);
    if (!g->config().hasKey) {
      lv_obj_t *b = lv_button_create(r1);
      lv_obj_t *bl = text(b, "BOOT: Kopplung freigeben");
      lv_obj_center(bl);
      lv_obj_add_event_cb(b, onPairing, LV_EVENT_CLICKED, &w);
    }
    w.info = text(card, "", true);
    lv_label_set_long_mode(w.info, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(w.info, LV_PCT(100));
  }

  lv_obj_t *airCard = box(scr, true);
  text(airCard, "Funk (ESP-NOW, simuliert)");
  gLossLbl = text(airCard, "", true);
  gLoss = lv_slider_create(airCard);
  lv_slider_set_range(gLoss, 0, 50);
  lv_obj_set_width(gLoss, LV_PCT(90));
  lv_obj_add_event_cb(gLoss, onLoss, LV_EVENT_VALUE_CHANGED, nullptr);
  gAirLbl = text(airCard, "", true);

  lv_obj_t *hint = text(scr, "Maus = Touch, ziehen = wischen. Mausrad scrollt hier. Shift+Esc beendet.", true);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width(hint, LV_PCT(100));

  lv_timer_create(refresh, 150, nullptr);
  refresh(nullptr);
  lv_display_set_default(prev);
}
