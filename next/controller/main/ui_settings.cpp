// Einstellungen: Menue, Gateway-Verwaltung (bekannt und unbekannt), Kopplung mit optionalem
// Schluessel, Gateway-Details (Name, Status, Abgleich, Kalibrierung, Entkoppeln) und Info.
// Die Gateway-Einstellungen sind immer sichtbar; Zigbee erscheint nur mit aktiviertem Build-Flag.
#include <stdio.h>
#include <string.h>
#include "levi_key.h"
#include "lv_theme_shadcn.h"
#include "sdkconfig.h"
#include "ui.h"
#if CONFIG_LEVILUMA_ZIGBEE_ENABLED
#include "zigbee_bridge.h"
#endif

void uiGatewayStatusText(const GatewayView &v, char *out, size_t cap);

// ---------------- Menue ----------------

static void goHome() {
  uiShowHome();
}

static void menuGateways(lv_event_t *) {
  uiShowGateways();
}
static void menuTouch(lv_event_t *) {
  uiShowTouchCal(false);
}
static void menuInfo(lv_event_t *) {
  uiShowInfo();
}
#if CONFIG_LEVILUMA_ZIGBEE_ENABLED
static void menuZigbee(lv_event_t *) {
  uiShowZigbee();
}
#endif

static lv_obj_t *menuEntry(lv_obj_t *list, const char *icon, const char *text, lv_event_cb_t cb) {
  return uiListButton(list, icon, text, cb);
}

void uiShowSettings() {
  lv_obj_t *c = uiScreen("Einstellungen", goHome, nullptr);
  lv_obj_t *list = uiList(c);
  menuEntry(list, LV_SYMBOL_LIST, "Gateways", menuGateways);
#if CONFIG_LEVILUMA_ZIGBEE_ENABLED
  menuEntry(list, LV_SYMBOL_SHUFFLE, "Zigbee (FRITZ!Box)", menuZigbee);
#endif
  menuEntry(list, LV_SYMBOL_EDIT, "Touch kalibrieren", menuTouch);
  menuEntry(list, LV_SYMBOL_HOME, "Info", menuInfo);
}

// ---------------- Gateway-Liste ----------------

static lv_obj_t *gwList = nullptr;
static uint32_t gwListSig = 0;

static void backToSettings() {
  uiShowSettings();
}

static void searchClicked(lv_event_t *) {
  appDiscover();
  uiToast("Suche gesendet. Gateways melden sich innerhalb weniger Sekunden.");
}

static void pairedClicked(lv_event_t *e) {
  uiShowGatewaySettings((int)(intptr_t)lv_event_get_user_data(e));
}

static void unknownClicked(lv_event_t *e) {
  uiShowPair((int)(intptr_t)lv_event_get_user_data(e));
}

static uint32_t listSignature() {
  uint32_t h = 2166136261u;
  GatewayView v;
  uint32_t now = uiNow();
  for (int i = 0; i < appGatewayCount(); i++) {
    if (!appView(i, v)) {
      continue;
    }
    // Unbekannte Gateways nur zeigen, solange sie sich melden
    bool visible = v.paired || v.online;
    uint32_t parts[4] = {(uint32_t)i, (uint32_t)(v.paired | (v.online << 1) | (visible << 2) | ((uint32_t)v.pairPhase << 3)), v.caps,
                         (uint32_t)strlen(v.name)};
    for (uint32_t p : parts) {
      h = (h ^ p) * 16777619u;
    }
    for (const char *s = v.name; *s; s++) {
      h = (h ^ (uint8_t)*s) * 16777619u;
    }
  }
  (void)now;
  return h;
}

static void buildGwList() {
  lv_obj_clean(gwList);
  GatewayView v;
  int unknown = 0;
  // Gekoppelte Gateways stehen ohne Ueberschrift oben in der Liste
  for (int i = 0; i < appGatewayCount(); i++) {
    if (!appView(i, v) || !v.paired) {
      continue;
    }
    char st[48], line[96];
    uiGatewayStatusText(v, st, sizeof(st));
    snprintf(line, sizeof(line), "%s\n%s%s", v.name, st, v.hasKey ? "  (Schluessel)" : "");
    uiListButton(gwList, v.online ? LV_SYMBOL_OK : LV_SYMBOL_WARNING, line, pairedClicked, (void *)(intptr_t)i);
  }
  uiListText(gwList, "Weitere"); // gefundene, noch nicht gekoppelte Gateways

  for (int i = 0; i < appGatewayCount(); i++) {
    if (!appView(i, v) || v.paired || !v.online) {
      continue;
    }
    char id[18], line[110];
    lvp_id_to_str(v.id, id, sizeof(id));
    const char *hint = (v.caps & LVP_GW_KEY_REQUIRED) ? "Schluessel noetig" : (v.caps & LVP_GW_PAIRING_OPEN) ? "Kopplung offen" : "Kopplung am Gateway freigeben";
    snprintf(line, sizeof(line), "%s\n%s - %s", v.name, id, hint);
    uiListButton(gwList, LV_SYMBOL_PLUS, line, unknownClicked, (void *)(intptr_t)i);
    unknown++;
  }
  if (!unknown) {
    uiLabel(gwList, "Keine gefunden (Suche laeuft automatisch)", true);
  }
}

static void gwListRefresh(bool) {
  uint32_t sig = listSignature();
  if (sig != gwListSig) {
    gwListSig = sig;
    buildGwList();
  }
}

void uiShowGateways() {
  lv_obj_t *c = uiScreen("Gateways", backToSettings, gwListRefresh);
  lv_obj_t *search = lv_button_create(uiHeader());
  lv_theme_shadcn_set_variant(search, LV_THEME_SHADCN_VARIANT_OUTLINE);
  lv_theme_shadcn_set_size(search, LV_THEME_SHADCN_SIZE_SM);
  lv_obj_t *l = lv_label_create(search);
  lv_label_set_text(l, LV_SYMBOL_REFRESH " Suchen");
  lv_obj_add_event_cb(search, searchClicked, LV_EVENT_CLICKED, nullptr);
  gwList = uiList(c);
  gwListSig = 0;
  appDiscover();
  gwListRefresh(true);
}

// ---------------- Kopplung ----------------

static int pairIdx = -1;
static lv_obj_t *pairStatus = nullptr, *keyArea = nullptr, *pairBtn = nullptr;
static bool pairStarted = false, pairUsedKey = false;

static void backToGateways() {
  uiShowGateways();
}

static void startPair() {
  GatewayView v;
  if (!appView(pairIdx, v)) {
    return;
  }
  uint8_t key[LEVI_KEY_LEN];
  const uint8_t *k = nullptr;

  if (v.caps & LVP_GW_KEY_REQUIRED) {
    if (!levi_key_parse_hex(lv_textarea_get_text(keyArea), key)) {
      lv_label_set_text(pairStatus, "Schluessel: genau 32 Hex-Zeichen (0-9, A-F)");
      return;
    }
    k = key;
  }
  lvp_err_t e = appPair(pairIdx, k);
  if (e != LVP_OK) {
    lv_label_set_text(pairStatus, appErrorText(e, false));
    return;
  }
  pairStarted = true;
  pairUsedKey = k != nullptr;
  lv_label_set_text(pairStatus, "Kopple ...");
}

static void pairClicked(lv_event_t *) {
  startPair();
}

static void keyReady(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_READY) {
    startPair();
  }
}

static void pairRefresh(bool) {
  GatewayView v;
  if (!appView(pairIdx, v)) {
    uiShowGateways();
    return;
  }
  if (v.paired && pairStarted) {
    pairStarted = false;
    uiShowGatewaySettings(pairIdx);
    uiToast("Gateway gekoppelt.");
    return;
  }
  if (!pairStarted) {
    return;
  }
  if (v.pairPhase == PairPhase::Failed) {
    pairStarted = false;
    char t[128];
    if (v.lastTimeout && !v.lastError) {
      snprintf(t, sizeof(t), "%s", pairUsedKey ? "Keine Antwort: Schluessel pruefen oder Gateway ausser Reichweite" : "Keine Antwort vom Gateway");
    } else {
      snprintf(t, sizeof(t), "Fehlgeschlagen: %s", appErrorText(v.lastError, v.lastTimeout));
    }
    lv_label_set_text(pairStatus, t);
  }
}

void uiShowPair(int idx) {
  pairIdx = idx;
  pairStarted = false;
  GatewayView v;
  if (!appView(idx, v)) {
    uiShowGateways();
    return;
  }
  char title[48];
  snprintf(title, sizeof(title), "Koppeln: %s", v.name);
  lv_obj_t *c = uiScreen(title, backToGateways, pairRefresh);
  lv_obj_set_style_pad_row(c, 4, 0);
  char id[18], line[96];
  lvp_id_to_str(v.id, id, sizeof(id));
  snprintf(line, sizeof(line), "ID %s, Firmware %u.%u", id, v.fwVersion >> 8, v.fwVersion & 0xFF);
  uiLabel(c, line, true);

  if (v.caps & LVP_GW_KEY_REQUIRED) {
    keyArea = lv_textarea_create(c);
    lv_textarea_set_one_line(keyArea, true);
    lv_textarea_set_max_length(keyArea, LEVI_KEY_HEX_LEN);
    lv_textarea_set_accepted_chars(keyArea, "0123456789ABCDEFabcdef");
    lv_textarea_set_placeholder_text(keyArea, "Schluessel (32 Hex-Zeichen)");
    lv_obj_set_width(keyArea, LV_PCT(100));
    lv_obj_add_event_cb(keyArea, keyReady, LV_EVENT_READY, nullptr);
    static const char *hexMap[] = {"1", "2", "3", "4", "5", "6", "7", "8", "\n", "9", "0", "A", "B", "C", "D", "E", "F", "\n",
                                   LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, ""};
    static const lv_buttonmatrix_ctrl_t W1 = LV_BUTTONMATRIX_CTRL_WIDTH_1, W2 = LV_BUTTONMATRIX_CTRL_WIDTH_2;
    static const lv_buttonmatrix_ctrl_t hexCtrl[] = {W1, W1, W1, W1, W1, W1, W1, W1, W1, W1, W1, W1, W1, W1, W1, W1, W2, W2};
    lv_obj_t *kb = lv_keyboard_create(c);
    lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, hexMap, hexCtrl);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
    lv_keyboard_set_textarea(kb, keyArea);
    lv_obj_set_size(kb, LV_PCT(100), 96);
    pairStatus = uiLabel(c, "Schluessel wie am Gateway hinterlegt eingeben, dann " LV_SYMBOL_OK, true);
  } else {
    keyArea = nullptr;
    uiLabel(c, "Dieses Gateway nutzt keinen Schluessel: die Verbindung ist unverschluesselt.");
    uiLabel(c, (v.caps & LVP_GW_PAIRING_OPEN) ? "Kopplungsfenster ist offen." : "Am Gateway die BOOT-Taste kurz druecken (Kopplungsfenster).", true);
    pairBtn = uiButton(c, "Koppeln", pairClicked);
    pairStatus = uiLabel(c, "", true);
  }
  lv_label_set_long_mode(pairStatus, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width(pairStatus, LV_PCT(100));
}

// ---------------- Gateway-Details ----------------

static int setIdx = -1;
static lv_obj_t *infoLbl = nullptr;

static void syncClicked(lv_event_t *) {
  appRequestState(setIdx);
  appRequestCalib(setIdx);
}

static void calibClicked(lv_event_t *) {
  uiShowCalibration(setIdx);
}

static void doUnpair(void *) {
  appUnpair(setIdx);
  uiShowGateways();
}

static void unpairClicked(lv_event_t *) {
  uiConfirm("Entkoppeln", "Gateway entkoppeln? Schluessel und Zuordnungen werden geloescht.", "Entkoppeln", doUnpair, nullptr);
}

static void renameClicked(lv_event_t *);

static void detailRefresh(bool) {
  GatewayView v;
  if (!appView(setIdx, v) || !v.paired) {
    uiShowGateways();
    return;
  }
  lv_label_set_text(uiHeaderTitle(), v.name);
  char id[18], seen[32], t[400];
  lvp_id_to_str(v.id, id, sizeof(id));
  if (v.lastSeenAgoMs == UINT32_MAX) {
    snprintf(seen, sizeof(seen), "nie");
  } else {
    snprintf(seen, sizeof(seen), "vor %lu s", (unsigned long)(v.lastSeenAgoMs / 1000));
  }
  char st[48];
  uiGatewayStatusText(v, st, sizeof(st));
  int n = snprintf(t, sizeof(t),
                   "ID: %s\nGemeldeter Name: %s\nFirmware: %u.%u\nVerbindung: %s (zuletzt %s)\nBand (BLE): %s\nZustand: %s\nSchluessel: %s",
                   id, v.announcedName[0] ? v.announcedName : "-", v.fwVersion >> 8, v.fwVersion & 0xFF, v.online ? "online" : "offline", seen,
                   v.stateValid && (v.state.flags & LVP_STATE_BLE) ? "verbunden" : "getrennt", st, v.hasKey ? "individuell (verschluesselt)" : "keiner (unverschluesselt)");
#if CONFIG_LEVILUMA_ZIGBEE_ENABLED
  if (n > 0 && n < (int)sizeof(t)) {
    if (v.zbEndpoint && v.zbEndpoint != 0xFF) {
      snprintf(t + n, sizeof(t) - n, "\nZigbee-Endpoint: %u", v.zbEndpoint);
    } else {
      snprintf(t + n, sizeof(t) - n, "\nZigbee-Endpoint: keiner");
    }
  }
#else
  (void)n;
#endif
  lv_label_set_text(infoLbl, t);
}

void uiShowGatewaySettings(int idx) {
  setIdx = idx;
  lv_obj_t *c = uiScreen("Gateway", backToGateways, detailRefresh);
  lv_obj_t *row = uiRow(c);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_row(row, 6, 0);
  uiButton(row, LV_SYMBOL_EDIT " Name", renameClicked, nullptr, false);
  uiButton(row, LV_SYMBOL_REFRESH " Abgleich", syncClicked, nullptr, false);
  uiButton(row, "Kalibrierung", calibClicked, nullptr, false);
  lv_obj_t *card = uiCard(c);
  infoLbl = uiLabel(card, "");
  lv_label_set_long_mode(infoLbl, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width(infoLbl, LV_PCT(100));
  lv_obj_t *un = uiButton(c, LV_SYMBOL_TRASH " Entkoppeln", unpairClicked);
  lv_theme_shadcn_set_variant(un, LV_THEME_SHADCN_VARIANT_DESTRUCTIVE);
  detailRefresh(true);
}

// ---------------- Anzeigename ----------------

static lv_obj_t *nameArea = nullptr;

static void backToDetail() {
  uiShowGatewaySettings(setIdx);
}

static void nameReady(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) {
    if (!appSetAlias(setIdx, lv_textarea_get_text(nameArea))) {
      uiToast("Speichern fehlgeschlagen");
      return;
    }
    uiShowGatewaySettings(setIdx);
  } else if (code == LV_EVENT_CANCEL) {
    uiShowGatewaySettings(setIdx);
  }
}

static void renameClicked(lv_event_t *) {
  GatewayView v;
  if (!appView(setIdx, v)) {
    return;
  }
  lv_obj_t *c = uiScreen("Anzeigename", backToDetail, nullptr);
  lv_obj_set_style_pad_row(c, 4, 0);
  nameArea = lv_textarea_create(c);
  lv_textarea_set_one_line(nameArea, true);
  lv_textarea_set_max_length(nameArea, LVP_NAME_MAX);
  lv_textarea_set_text(nameArea, v.alias);
  lv_textarea_set_placeholder_text(nameArea, v.announcedName[0] ? v.announcedName : "Name");
  lv_obj_set_width(nameArea, LV_PCT(100));
  lv_obj_add_event_cb(nameArea, nameReady, LV_EVENT_ALL, nullptr);
  lv_obj_t *kb = lv_keyboard_create(c);
  lv_keyboard_set_textarea(kb, nameArea);
  lv_obj_set_size(kb, LV_PCT(100), 130);
  uiLabel(c, "Leer = Name des Gateways. " LV_SYMBOL_OK " speichert.", true);
}

// ---------------- Info ----------------

static lv_obj_t *infoText = nullptr;

static void infoRefresh(bool) {
  CtrlLink::Stats s = appStats();
  char id[18], t[320];
  lvp_id_to_str(appOwnId(), id, sizeof(id));
  snprintf(t, sizeof(t),
           "LeviLuma Next Controller\nBoard: RockBase NM-CYD-C5 (ESP32-C5)\nController-ID: %s\nESP-NOW-Kanal: %d, Protokoll v%d\nZigbee: %s\n\nGesendet %lu, "
           "wiederholt %lu, Zeitueberschreitungen %lu\nEmpfangen %lu, verworfen %lu, Ablehnungen %lu",
           id, CONFIG_LEVILUMA_ESPNOW_CHANNEL, LVP_VERSION,
#if CONFIG_LEVILUMA_ZIGBEE_ENABLED
           "aktiviert",
#else
           "nicht enthalten (Build-Flag aus)",
#endif
           (unsigned long)s.sent, (unsigned long)s.resent, (unsigned long)s.timeouts, (unsigned long)s.rx, (unsigned long)s.rxErrors,
           (unsigned long)s.nacks);
  lv_label_set_text(infoText, t);
}

void uiShowInfo() {
  lv_obj_t *c = uiScreen("Info", backToSettings, infoRefresh);
  lv_obj_t *card = uiCard(c);
  infoText = uiLabel(card, "");
  lv_label_set_long_mode(infoText, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width(infoText, LV_PCT(100));
  infoRefresh(true);
}
