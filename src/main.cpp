#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <lvgl.h>
#include "LGFX_Board.h"
#include "LotusProtocol.h"
#include "WebUi.h"
#include "Config.h"
#include "Palette.h"
#include "CalibCore.h"
#include "ColorMath.h"
#include <math.h>
#include <Wire.h>

// Logging ueber stdout (UART0-Konsole), also in demselben Monitor wie die NimBLE-Meldungen.
// ESP_LOGI wurde unterdrueckt (Log-Level), printf geht direkt auf die Konsole.
// Serial (USB-CDC) wird nicht mehr genutzt: dort kam nichts an, wenn der Monitor am anderen Port haengt.
#define LEVI_LOG(fmt,...) printf("[%8lu] levi: " fmt "\n",(unsigned long)millis(),##__VA_ARGS__)

#ifndef LEVI_DISPLAY_ROTATION
#define LEVI_DISPLAY_ROTATION 0
#endif
static LGFX_Board lcd;
static WebServer web(80);
static SemaphoreHandle_t stateLock;
static constexpr uint16_t W=240,H=240;
alignas(4) static uint8_t drawBuf[240*24*2]; // 11520 bytes RGB565; LVGL buffer aligned
static lv_display_t *display=nullptr;
static lv_indev_t *input=nullptr;
static lv_obj_t *tv=nullptr,*tileBright=nullptr,*tilePower=nullptr,*tileColor=nullptr;
static lv_obj_t *bar=nullptr,*barLbl=nullptr,*center=nullptr,*pillLbl=nullptr,*pillLbl2=nullptr,*holdFill=nullptr,*statusWifi=nullptr,*statusBle=nullptr;
static lv_obj_t *lblWifi=nullptr,*lblBle=nullptr;static bool uiWifi=false;
static lv_obj_t *dots[LeviCal::N+1]{};
struct State { bool ble=false; bool music=false; bool raw=false; int8_t power=-1; uint8_t r=139,g=92,b=246,brightness=75; uint32_t revision=0; uint32_t colorRev=0; uint32_t sentRevision=0; };  // sentRevision: letzte Revision, die vollstaendig an BLE gesendet wurde (Sendebestaetigung, keine Ausfuehrungsbestaetigung)
static State state;
static uint32_t lastTouchMs=0,lastWifiAttempt=0;
static int8_t selected=-1;   // Index in der Farbliste (0..16 Kalibrierfarben, MUSIC_IDX), -1 = freie Farbe
// Farbliste: die 17 Kalibrierziele, danach der Musikmodus.
static constexpr int MUSIC_IDX=LeviCal::N;
static constexpr int LIST_COUNT=LeviCal::N+1;
static int wrapIdx(int i){return ((i%LIST_COUNT)+LIST_COUNT)%LIST_COUNT;}
static uint32_t uiRevision=UINT32_MAX;
static bool uiBle=false;
static int8_t uiSelected=-2;
static bool touchDown=false;
static uint16_t touchX=120,touchY=120;
static bool touchBlocked=false;
static bool screenDim=false;
// Nach so vielen ms ohne Beruehrung springt die Ansicht zurueck auf die Mitte (An/Aus).
#ifndef LEVI_RECENTER_MS
#define LEVI_RECENTER_MS 60000
#endif
static uint8_t currentBacklight=255;

// Kalibrierung: Kanal-Gains und Gamma, im NVS gespeichert. Ausgabe = gamma(gain*in).
#ifndef LEVI_CALIB_GR
#define LEVI_CALIB_GR 1.000f
#endif
#ifndef LEVI_CALIB_GG
#define LEVI_CALIB_GG 0.690f
#endif
#ifndef LEVI_CALIB_GB
#define LEVI_CALIB_GB 0.940f
#endif
// Standard-Kalibrierung aus Config.h (sonst FastLED TypicalSMD5050).
// Kalibrierung: calModel und calCustom werden nur unter calLock gelesen oder geschrieben.
// Leser (BLE-Task, Web) holen sich eine vollstaendige Kopie, Schreiber rechnen die neue Kopie
// ausserhalb des Locks und tauschen sie dann atomar aus.
static SemaphoreHandle_t calLock=nullptr;
static LeviCal::Model calModel;
static bool calCustom=false;   // true, wenn eine Kalibrierung aus dem Web im NVS liegt
static LeviCal::Model calSnapshot(bool *custom=nullptr){
  xSemaphoreTake(calLock,portMAX_DELAY);
  LeviCal::Model m=calModel;
  if(custom)*custom=calCustom;
  xSemaphoreGive(calLock);
  return m;
}
static void calCommit(const LeviCal::Model &m,bool custom){
  xSemaphoreTake(calLock,portMAX_DELAY);
  calModel=m;calCustom=custom;
  xSemaphoreGive(calLock);
}
// Standard: Config-Tabelle (LEVI_CALIB_POINTS) oder lineare Gains (sonst FastLED TypicalSMD5050).
static void calibDefaultPts(LeviCal::Model &m){
  LeviCal::defaultPts(m,LEVI_CALIB_GR,LEVI_CALIB_GG,LEVI_CALIB_GB);
#ifdef LEVI_CALIB_POINTS
  static const uint8_t cfgPts[LeviCal::N*3]={LEVI_CALIB_POINTS};
  memcpy(m.pts,cfgPts,sizeof(cfgPts));
#endif
}
static void calibLoad(){
  LeviCal::Model m;calibDefaultPts(m);bool custom=false;
  Preferences p;p.begin("levi",true);
  if(p.getBytesLength("pts")==sizeof(m.pts)){p.getBytes("pts",m.pts,sizeof(m.pts));custom=true;}
  p.end();
  LeviCal::solve(m);
  calCommit(m,custom);
}
// Neue Punkte (51 Werte) speichern: Gewichte ausserhalb des Locks berechnen, dann tauschen.
static void calibSave(const uint8_t *pts){
  LeviCal::Model m=calSnapshot();
  memcpy(m.pts,pts,sizeof(m.pts));
  LeviCal::solve(m);
  Preferences p;p.begin("levi",false);p.putBytes("pts",m.pts,sizeof(m.pts));p.end();
  calCommit(m,true);
}
static void calibReset(){
  Preferences p;p.begin("levi",false);p.remove("pts");p.end();
  LeviCal::Model m;calibDefaultPts(m);LeviCal::solve(m);
  calCommit(m,false);
}
static String calibJson(){
  bool custom=false;LeviCal::Model cm=calSnapshot(&custom);
  String j="{\"custom\":";j+=custom?"true":"false";j+=",\"points\":[";
  const uint8_t *flat=&cm.pts[0][0];
  for(int i=0;i<LeviCal::N*3;i++){if(i)j+=',';j+=String(flat[i]);}
  j+="]}";return j;
}
static State snapshot(){ xSemaphoreTake(stateLock,portMAX_DELAY); State s=state; xSemaphoreGive(stateLock); return s; }

static void setPower(bool on){ xSemaphoreTake(stateLock,portMAX_DELAY); state.power=on?1:0; ++state.revision; xSemaphoreGive(stateLock); }
static void setBrightness(int b){ xSemaphoreTake(stateLock,portMAX_DELAY); state.brightness=constrain(b,1,100); ++state.revision; xSemaphoreGive(stateLock); }
static void setRaw(uint8_t r,uint8_t g,uint8_t b){ xSemaphoreTake(stateLock,portMAX_DELAY); state.raw=true; state.r=r;state.g=g;state.b=b; ++state.revision; ++state.colorRev; xSemaphoreGive(stateLock); }
static void setRGB(uint8_t r,uint8_t g,uint8_t b){ xSemaphoreTake(stateLock,portMAX_DELAY); state.raw=false; state.music=false; state.r=r;state.g=g;state.b=b; ++state.revision; ++state.colorRev; xSemaphoreGive(stateLock);
  selected=-1;for(int i=0;i<LeviCal::N;i++)if(LeviCal::TARGET[i][0]==r&&LeviCal::TARGET[i][1]==g&&LeviCal::TARGET[i][2]==b){selected=(int8_t)i;break;} }
static void setMusic(bool on){xSemaphoreTake(stateLock,portMAX_DELAY);state.music=on;++state.revision;xSemaphoreGive(stateLock);if(on)selected=MUSIC_IDX;}
static void selectColor(int idx){int n=wrapIdx(idx);if(n==MUSIC_IDX){setMusic(true);return;}setRGB(LeviCal::TARGET[n][0],LeviCal::TARGET[n][1],LeviCal::TARGET[n][2]);}
static void backlight(uint8_t value){currentBacklight=value;ledcWrite(0,LEVI_BACKLIGHT_ACTIVE_HIGH?value:255-value);}
static void wake(){lastTouchMs=millis();if(screenDim){screenDim=false;backlight(255);}}
static lv_color_t rgbColor(uint8_t r,uint8_t g,uint8_t b){return lv_color_make(r,g,b);}
// Heller Hintergrund -> dunkle Schrift, sonst weiss.
static void applyContrast(uint8_t r,uint8_t g,uint8_t b){
  bool light=(r*299+g*587+b*114)/1000>170;
  lv_color_t fg=light?lv_color_hex(0x1A1A1A):lv_color_white();
  lv_obj_set_style_text_color(pillLbl,fg,0);
  lv_obj_set_style_text_color(pillLbl2,fg,0);
}
static void flush(lv_display_t *disp,const lv_area_t *a,uint8_t *px){
  int32_t width=a->x2-a->x1+1,height=a->y2-a->y1+1;
  lcd.startWrite();lcd.setAddrWindow(a->x1,a->y1,width,height);
  lcd.pushPixels((const uint16_t*)px, width * height, true);
  lcd.endWrite();lv_display_flush_ready(disp);
}
// Touch-Treiber: eine Beruehrung im gedimmten Zustand weckt nur das Display und wird nicht
// an die Oberflaeche weitergegeben, bis der Finger wieder oben ist. Kurze Aussetzer des CST816S
// (Loslassen unter 100 ms) werden ueberbrueckt. Gesten erkennt LVGL.
static void readTouch(lv_indev_t*,lv_indev_data_t *d){
  static uint32_t seenMs=0;
  if(touchDown){seenMs=millis();lastTouchMs=seenMs;}
  bool down=touchDown||(millis()-seenMs<100);
  if(!down)touchBlocked=false;
  else if(touchDown&&screenDim){wake();touchBlocked=true;}
  d->state=(down&&!touchBlocked)?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
  d->point.x=touchX;d->point.y=touchY;
}
// Deaktiviert und halbtransparent, wenn das Element gerade keine Eingabe annehmen soll.
static void setEnabled(lv_obj_t *o,bool en,lv_opa_t opaOn=LV_OPA_COVER){
  if(en)lv_obj_remove_state(o,LV_STATE_DISABLED);else lv_obj_add_state(o,LV_STATE_DISABLED);
  lv_obj_set_style_opa(o,en?opaOn:LV_OPA_40,0);
}
static void updateUI(){
  State s=snapshot();
  bool wifi=WiFi.status()==WL_CONNECTED;
  if(s.revision==uiRevision && s.ble==uiBle && selected==uiSelected && wifi==uiWifi)return;
  uiRevision=s.revision;uiBle=s.ble;uiSelected=selected;uiWifi=wifi;
  bool on=s.power==1;
  bool active=on&&s.ble;   // Helligkeit und Farben nur eingeschaltet und mit Band verbunden
  lv_color_t color=rgbColor(s.r,s.g,s.b);
  applyContrast(s.r,s.g,s.b);
  // Mitte: An/Aus ist auch ausgeschaltet bedienbar, sobald verbunden
  lv_obj_set_style_bg_color(center,color,0);
  lv_obj_set_style_shadow_color(center,color,0);
  setEnabled(center,s.ble,on?LV_OPA_COVER:LV_OPA_60);
  lv_label_set_text(pillLbl,on?"Ausschalten":"Einschalten");
  lv_label_set_text(pillLbl2,on?"Ausschalten":"Einschalten");
  // Statuspunkt: gruen verbunden, rot getrennt
  lv_obj_set_style_bg_color(statusWifi,wifi?lv_color_hex(0x79DA89):lv_color_hex(0xE5484D),0);
  lv_obj_set_style_bg_color(statusBle,s.ble?lv_color_hex(0x79DA89):lv_color_hex(0xE5484D),0);
  // Helligkeit
  lv_obj_set_style_bg_color(bar,color,LV_PART_INDICATOR);
  setEnabled(bar,active);
  setEnabled(barLbl,active);
  if(lv_slider_get_value(bar)!=s.brightness)lv_slider_set_value(bar,s.brightness,LV_ANIM_OFF);
  char label[12];snprintf(label,sizeof(label),"%u%%",(unsigned)s.brightness);
  lv_label_set_text(barLbl,label);
  // Farbpunkte: aktive Farbe mit weissem Rahmen
  for(int k=0;k<LeviCal::N;k++){
    lv_obj_set_style_border_width(dots[k],k==selected?2:0,0);
    if(k==MUSIC_IDX)lv_obj_set_style_bg_color(dots[k],lv_color_hex(0x333333),0);
    else lv_obj_set_style_bg_color(dots[k],lv_color_make(LeviCal::TARGET[k][0],LeviCal::TARGET[k][1],LeviCal::TARGET[k][2]),0);
    setEnabled(dots[k],active);
  }
}

// Helligkeitsbalken: nur Benutzereingaben (Indev aktiv) setzen den Zustand, nicht die Anzeige selbst.
static void barChanged(lv_event_t *e){
  if(lv_event_get_code(e)!=LV_EVENT_VALUE_CHANGED||!lv_indev_get_act())return;
  if(snapshot().power!=1)return;
  setBrightness(lv_slider_get_value(bar));
}
// Ein Tipp auf die Farbseite springt zur naechsten Farbe; nach der letzten wieder zur ersten.
static void nextColorClicked(lv_event_t *e){
  (void)e;
  State s=snapshot();
  if(s.power!=1||!s.ble)return;
  int n=selected<0?0:(selected+1)%LeviCal::N;
  selectColor(n);
}

// Halten-Fuellung in der Pille: laeuft in 500 ms von links nach rechts voll, verschwindet beim Loslassen.
static void holdExec(void *obj,int32_t v){lv_obj_set_width((lv_obj_t*)obj,v);}
static void holdStart(){
  lv_anim_delete(holdFill,nullptr);
  lv_obj_set_width(holdFill,0);
  lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,holdFill);lv_anim_set_exec_cb(&a,holdExec);
  lv_anim_set_values(&a,0,lv_obj_get_width(center));lv_anim_set_duration(&a,500);
  lv_anim_start(&a);
}
static void holdStop(){
  lv_anim_delete(holdFill,nullptr);
  lv_obj_set_width(holdFill,0);
}
static lv_point_t centerPressPt={0,0};
static void centerPressed(lv_event_t *e){(void)e;holdStart();lv_indev_t *in=lv_indev_get_act();if(in)lv_indev_get_point(in,&centerPressPt);}
static void centerReleased(lv_event_t *e){(void)e;holdStop();}
static void centerClicked(lv_event_t *e){ // LONG_PRESSED
  (void)e;holdStop();
  lv_indev_t *in=lv_indev_get_act();if(!in)return;
  lv_point_t p;lv_indev_get_point(in,&p);
  int dx=p.x-centerPressPt.x,dy=p.y-centerPressPt.y;
  if((dx<0?-dx:dx)+(dy<0?-dy:dy)>20)return;
  setPower(snapshot().power!=1);
}

static void makeUI(){
  lv_obj_t *screen=lv_screen_active();
  lv_obj_set_style_bg_color(screen,lv_color_hex(0x000000),0);lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
  // Drei Kacheln nebeneinander: links Helligkeit, Mitte An/Aus (Start), rechts Farben
  // LVGL-Tileview: drei Kacheln, Wischen links/rechts blaettert
  tv=lv_tileview_create(screen);lv_obj_set_size(tv,240,240);lv_obj_center(tv);
  lv_obj_set_style_bg_color(tv,lv_color_hex(0x000000),0);lv_obj_set_style_bg_opa(tv,LV_OPA_COVER,0);
  lv_obj_set_style_border_width(tv,0,0);lv_obj_set_style_pad_all(tv,0,0);lv_obj_set_scrollbar_mode(tv,LV_SCROLLBAR_MODE_OFF);
  tileBright=lv_tileview_add_tile(tv,0,0,(lv_dir_t)LV_DIR_RIGHT);
  tilePower=lv_tileview_add_tile(tv,1,0,(lv_dir_t)(LV_DIR_LEFT|LV_DIR_RIGHT));
  tileColor=lv_tileview_add_tile(tv,2,0,(lv_dir_t)LV_DIR_LEFT);
  lv_obj_t *tiles[3]={tileBright,tilePower,tileColor};
  for(int i=0;i<3;i++){lv_obj_set_style_bg_opa(tiles[i],LV_OPA_TRANSP,0);lv_obj_set_style_pad_all(tiles[i],0,0);lv_obj_set_style_border_width(tiles[i],0,0);lv_obj_set_scrollbar_mode(tiles[i],LV_SCROLLBAR_MODE_OFF);}
  lv_obj_set_tile_id(tv,1,0,LV_ANIM_OFF);

  // Helligkeit: vertikaler Balken, Prozent oben
  // Helligkeit: waagerechte Spur, darueber der Prozentwert mit etwas Abstand
  bar=lv_slider_create(tileBright);lv_slider_set_range(bar,1,100);lv_obj_set_size(bar,180,18);lv_obj_align(bar,LV_ALIGN_CENTER,0,22);
  lv_obj_set_style_radius(bar,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(bar,lv_color_hex(0x2A2A2A),LV_PART_MAIN);lv_obj_set_style_border_width(bar,0,0);
  lv_obj_set_style_radius(bar,LV_RADIUS_CIRCLE,LV_PART_INDICATOR);lv_obj_set_style_border_width(bar,0,LV_PART_INDICATOR);
  lv_obj_set_style_radius(bar,LV_RADIUS_CIRCLE,LV_PART_KNOB);lv_obj_set_style_bg_color(bar,lv_color_white(),LV_PART_KNOB);lv_obj_set_style_bg_opa(bar,LV_OPA_COVER,LV_PART_KNOB);
  lv_obj_set_style_border_width(bar,0,LV_PART_KNOB);lv_obj_set_style_shadow_width(bar,0,LV_PART_KNOB);lv_obj_set_style_pad_all(bar,6,LV_PART_KNOB);
  lv_obj_add_event_cb(bar,barChanged,LV_EVENT_VALUE_CHANGED,nullptr);
  barLbl=lv_label_create(tileBright);lv_obj_set_style_text_color(barLbl,lv_color_white(),0);lv_obj_set_style_text_font(barLbl,&lv_font_montserrat_28,0);lv_obj_align(barLbl,LV_ALIGN_CENTER,0,-14);

  // Mitte: An/Aus mit Halten
  center=lv_obj_create(tilePower);lv_obj_set_size(center,150,48);lv_obj_center(center);lv_obj_add_flag(center,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(center,centerPressed,LV_EVENT_PRESSED,nullptr);lv_obj_add_event_cb(center,centerReleased,LV_EVENT_RELEASED,nullptr);
  lv_obj_add_event_cb(center,centerReleased,LV_EVENT_PRESS_LOST,nullptr);lv_obj_add_event_cb(center,centerClicked,LV_EVENT_LONG_PRESSED,nullptr);
  lv_obj_set_style_radius(center,LV_RADIUS_CIRCLE,0);lv_obj_set_style_clip_corner(center,true,0);lv_obj_set_style_border_width(center,0,0);lv_obj_set_style_pad_all(center,0,0);
  lv_obj_set_style_shadow_width(center,0,0);lv_obj_set_style_shadow_opa(center,LV_OPA_30,0);lv_obj_remove_flag(center,LV_OBJ_FLAG_SCROLLABLE);
  // Fuellung beim Halten (liegt unter der Beschriftung, von der Pille beschnitten)
  holdFill=lv_obj_create(center);lv_obj_set_size(holdFill,0,48);lv_obj_set_style_radius(holdFill,LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_border_width(holdFill,0,0);lv_obj_set_style_pad_all(holdFill,0,0);lv_obj_set_style_shadow_width(holdFill,0,0);
  lv_obj_set_style_bg_color(holdFill,lv_color_white(),0);lv_obj_set_style_bg_opa(holdFill,LV_OPA_30,0);
  lv_obj_remove_flag(holdFill,LV_OBJ_FLAG_CLICKABLE);lv_obj_align(holdFill,LV_ALIGN_LEFT_MID,0,0);
  // Halbfett: die Beschriftung zweimal mit 1 px Versatz (LVGL hat keine fette Montserrat-Variante)
  pillLbl=lv_label_create(center);lv_obj_set_style_text_color(pillLbl,lv_color_white(),0);lv_obj_align(pillLbl,LV_ALIGN_CENTER,-1,0);
  pillLbl2=lv_label_create(center);lv_obj_set_style_text_color(pillLbl2,lv_color_white(),0);lv_obj_align(pillLbl2,LV_ALIGN_CENTER,0,0);
  // Statuspunkte oben: links WLAN (W), rechts Bluetooth (B)
  statusWifi=lv_obj_create(tilePower);lv_obj_set_size(statusWifi,10,10);lv_obj_set_style_radius(statusWifi,LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_border_width(statusWifi,0,0);lv_obj_align(statusWifi,LV_ALIGN_TOP_MID,-11,22);
  statusBle=lv_obj_create(tilePower);lv_obj_set_size(statusBle,10,10);lv_obj_set_style_radius(statusBle,LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_border_width(statusBle,0,0);lv_obj_align(statusBle,LV_ALIGN_TOP_MID,11,22);
  lblWifi=lv_label_create(tilePower);lv_label_set_text(lblWifi,"W");lv_obj_set_style_text_color(lblWifi,lv_color_hex(0x888888),0);
  lv_obj_align_to(lblWifi,statusWifi,LV_ALIGN_OUT_LEFT_MID,-4,0);
  lblBle=lv_label_create(tilePower);lv_label_set_text(lblBle,"B");lv_obj_set_style_text_color(lblBle,lv_color_hex(0x888888),0);
  lv_obj_align_to(lblBle,statusBle,LV_ALIGN_OUT_RIGHT_MID,4,0);

  // Farbpunkte: 5 Spalten, 4 Zeilen (17 Kalibrierfarben plus Musik)
  for(int k=0;k<LeviCal::N;k++){
    dots[k]=lv_obj_create(tileColor);lv_obj_set_size(dots[k],14,14);lv_obj_remove_flag(dots[k],LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(dots[k],LV_RADIUS_CIRCLE,0);lv_obj_set_style_pad_all(dots[k],0,0);
    lv_obj_set_style_border_color(dots[k],lv_color_white(),0);lv_obj_set_style_shadow_width(dots[k],0,0);
    lv_obj_align(dots[k],LV_ALIGN_CENTER,(k%5-2)*34,(k/5)*34-51);
  }
  lv_obj_add_flag(tileColor,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(tileColor,nextColorClicked,LV_EVENT_CLICKED,nullptr);
  updateUI();
}

// Lotus-Lantern-/ELK-Familie (gleiches Protokoll, Service 0xFFF0): Namen beginnen mit einem dieser Praefixe.
static bool bleNameMatches(const std::string &n){
  static const char *prefixes[]={"ELK-BLEDDM","ELK-BLEDOM","ELK-BLEDOB","MELK","LEDBLE"};
  for(const char *p:prefixes){if(n.rfind(p,0)==0)return true;}
  return false;
}
class ScanCallbacks: public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* dev) override {(void)dev;} // Scan komplett laufen lassen, damit das staerkste Geraet gewaehlt wird
};
class ClientCallbacks: public NimBLEClientCallbacks {void onDisconnect(NimBLEClient*) override {LEVI_LOG("BLE: Verbindung getrennt");xSemaphoreTake(stateLock,portMAX_DELAY);state.ble=false;xSemaphoreGive(stateLock);}};
static ScanCallbacks scanCallbacks;
static ClientCallbacks clientCallbacks;
// Helligkeit fuer das Band: LEVI_BRIGHTNESS_MAX 100 = Werte 0..100, 255 = Prozent auf 0..255 umgerechnet.
#ifndef LEVI_BRIGHTNESS_MAX
#define LEVI_BRIGHTNESS_MAX 100
#endif
static uint8_t bandBrightness(uint8_t pct){return LEVI_BRIGHTNESS_MAX==255?(uint8_t)((pct*255+50)/100):pct;}
// Mikrofon-EQ-Modus nach 'Mikrofon an': 0 = normal, 1 = weich, 2 = dynamisch, 3 = disko.
#ifndef LEVI_MIC_EQ_MODE
#define LEVI_MIC_EQ_MODE 0
#endif
// Anzahl Zwischenschritte beim Farbwechsel (ein Schritt pro 200-ms-Slot).
#ifndef LEVI_FADE_STEPS
#define LEVI_FADE_STEPS 8
#endif
static void bleTask(void*){
  NimBLEDevice::init("LeviLuma");NimBLEClient *client=nullptr;NimBLERemoteCharacteristic *writer=nullptr;
  uint32_t lastColorRev=0,lastAttempt=0;int8_t lastPower=-1;bool lastMusic=false;int lastBright=-1;bool eqPending=false;
  ColorMath::HSV shown={0.f,0.f,1.f},fadeFrom=shown,fadeTo=shown;int fadeLeft=0,fadeN=1;bool fadeRaw=false,instantColor=true;
  for(;;){
    State s=snapshot();
    if(!client||!client->isConnected()||!writer){
      writer=nullptr; // Ungueltige BLE-Characteristic niemals wiederverwenden
      xSemaphoreTake(stateLock,portMAX_DELAY);state.ble=false;xSemaphoreGive(stateLock);
      if(millis()-lastAttempt>4000){
        lastAttempt=millis();NimBLEScan *scan=NimBLEDevice::getScan();scan->setActiveScan(true);scan->setAdvertisedDeviceCallbacks(&scanCallbacks);
        NimBLEScanResults results=scan->start(2,false);NimBLEAddress addr;bool found=false;int bestRssi=-200;
        LEVI_LOG("BLE: Scan fertig, %d Geraete gefunden", results.getCount());
        for(int i=0;i<results.getCount();i++){auto dev=results.getDevice(i);LEVI_LOG("BLE:   %s  RSSI %d  Name \"%s\"", dev.getAddress().toString().c_str(), dev.getRSSI(), dev.getName().c_str());if(bleNameMatches(dev.getName())&&(!found||dev.getRSSI()>bestRssi)){addr=dev.getAddress();bestRssi=dev.getRSSI();found=true;}}
        if(found) LEVI_LOG("BLE: staerkstes Geraet %s (RSSI %d dBm) gewaehlt", addr.toString().c_str(), bestRssi);
        if(!found) LEVI_LOG("BLE: kein Gerät der ELK/Lotus-Familie gefunden, naechster Versuch in 4 s");
        if(found){if(!client){client=NimBLEDevice::createClient();client->setClientCallbacks(&clientCallbacks);}
          if(client->connect(addr)){
            LEVI_LOG("BLE: verbunden mit %s", addr.toString().c_str());
            vTaskDelay(pdMS_TO_TICKS(1000)); // Band braucht 1 s nach dem Verbinden, bevor die Services gelesen werden
            auto svc=client->getService(NimBLEUUID((uint16_t)0xFFF0));writer=svc?svc->getCharacteristic(NimBLEUUID((uint16_t)0xFFF3)):nullptr;
            if(writer){xSemaphoreTake(stateLock,portMAX_DELAY);state.ble=true;xSemaphoreGive(stateLock);lastPower=-1;lastMusic=true;/* Modus unbekannt: Musik-aus wird beim Verbinden erzwungen */lastBright=-1;lastColorRev=s.colorRev-1;instantColor=true;shown=ColorMath::fromRgb(s.r,s.g,s.b);LEVI_LOG("BLE: Service 0xFFF0 / Characteristic 0xFFF3 gefunden");}
            else {LEVI_LOG("BLE: Service 0xFFF0 oder Characteristic 0xFFF3 fehlt, trenne"); client->disconnect();}
          } else LEVI_LOG("BLE: Verbindung zu %s fehlgeschlagen", addr.toString().c_str());
        }scan->clearResults();
      }
    }else{
      // Ein Befehl pro Zeitfenster, nie Helligkeit und Farbe im selben Schritt.
      // Schreiben ohne Antwort; nach jedem gesendeten Befehl 200 ms Pause.
      if(client->isConnected()&&writer&&writer->canWriteNoResponse()){
        uint8_t packet[9];bool attempted=false,ok=false;
        if(s.music!=lastMusic){
          attempted=true;Lotus::music(s.music,packet);ok=writer->writeValue(packet,9,false);
          if(ok){lastMusic=s.music;eqPending=s.music;LEVI_LOG("BLE: Musikmodus %s", s.music?"an":"aus");}
        }else if(eqPending){ // Mikrofon-EQ direkt nach 'Mikrofon an', wie die App
          attempted=true;Lotus::micEq(LEVI_MIC_EQ_MODE,packet);ok=writer->writeValue(packet,9,false);
          if(ok){eqPending=false;LEVI_LOG("BLE: Mikrofon-EQ %d gesetzt", LEVI_MIC_EQ_MODE);}
        }else if(s.brightness!=lastBright){
          attempted=true;Lotus::brightness(bandBrightness(s.brightness),packet);ok=writer->writeValue(packet,9,false);
          if(ok){lastBright=s.brightness;LEVI_LOG("BLE: Helligkeit %u%%", (unsigned)s.brightness);}
        }else if(s.power>=0&&s.power!=lastPower){
          attempted=true;Lotus::power(s.power==1,packet);ok=writer->writeValue(packet,9,false);
          if(ok){lastPower=s.power;LEVI_LOG("BLE: Power %s", s.power==1?"an":"aus");}
        }else if(s.power==1&&!s.music&&(s.colorRev!=lastColorRev||fadeLeft>0)){
          attempted=true;
          if(s.colorRev!=lastColorRev){ // neue Farbe: Ueberblendung ab dem zuletzt gesendeten Zwischenwert
            lastColorRev=s.colorRev;
            fadeRaw=s.raw;fadeTo=ColorMath::fromRgb(s.r,s.g,s.b);fadeFrom=shown;
            fadeN=(s.raw||instantColor)?1:LEVI_FADE_STEPS;fadeLeft=fadeN;instantColor=false;
          }
          int step=fadeN-fadeLeft+1;uint8_t cc[3]={s.r,s.g,s.b};
          if(!fadeRaw){shown=ColorMath::mix(fadeFrom,fadeTo,(float)step/fadeN);ColorMath::toRgb(shown,cc);LeviCal::Model cm=calSnapshot();LeviCal::apply(cm,cc[0],cc[1],cc[2],cc);}
          Lotus::color(cc[0],cc[1],cc[2],packet);
          LEVI_LOG("BLE: Farbe Schritt %d/%d -> %u,%u,%u%s",step,fadeN,(unsigned)cc[0],(unsigned)cc[1],(unsigned)cc[2],fadeRaw?" (raw)":"");
          ok=writer->writeValue(packet,9,false);if(ok)fadeLeft--;
        }
        if(attempted&&!ok) LEVI_LOG("BLE: Schreiben fehlgeschlagen");
        // Nichts mehr offen (kein Befehl, keine Ueberblendung): alles bis zu dieser Revision ist gesendet
        if(!attempted&&fadeLeft==0){xSemaphoreTake(stateLock,portMAX_DELAY);if(state.sentRevision<s.revision)state.sentRevision=s.revision;xSemaphoreGive(stateLock);}
        if(ok) vTaskDelay(pdMS_TO_TICKS(200));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(25));
  }
}
static bool integer(const String &v,int max){if(v.length()<1||v.length()>3)return false;for(char c:v)if(c<'0'||c>'9')return false;return v.toInt()<=max;}
static void stateJson(){State s=snapshot();String j="{\"wifi\":";j+=(WiFi.status()==WL_CONNECTED?"true":"false");j+=",\"ble\":";j+=(s.ble?"true":"false");j+=",\"power\":\"";j+=(s.power<0?"unknown":s.power==1?"on":"off");j+="\",\"r\":"+String(s.r)+",\"g\":"+String(s.g)+",\"b\":"+String(s.b)+",\"brightness\":"+String(s.brightness)+",\"music\":"+String(s.music?"true":"false")+",\"revision\":"+String(s.revision)+",\"sentRevision\":"+String(s.sentRevision)+"}";web.sendHeader("Cache-Control","no-store");web.send(200,"application/json",j);}
static void webInit(){
  web.on("/",HTTP_GET,[]{web.send_P(200,"text/html; charset=utf-8",LEVI_HTML);});web.on("/api/state",HTTP_GET,stateJson);
  web.on("/api/power",HTTP_POST,[]{String v=web.arg("on");if(v!="0"&&v!="1"){web.send(400,"text/plain","on=0|1");return;}setPower(v=="1");stateJson();});
  web.on("/api/brightness",HTTP_POST,[]{String v=web.arg("value");if(!integer(v,100)){web.send(400,"text/plain","value=0..100");return;}setBrightness(v.toInt());stateJson();});
  web.on("/api/raw",HTTP_POST,[]{String r=web.arg("r"),g=web.arg("g"),b=web.arg("b");if(!integer(r,255)||!integer(g,255)||!integer(b,255)){web.send(400,"text/plain","r,g,b=0..255");return;}setRaw(r.toInt(),g.toInt(),b.toInt());stateJson();});
  web.on("/api/calib",HTTP_GET,[]{web.sendHeader("Cache-Control","no-store");web.send(200,"application/json",calibJson());});
  web.on("/api/calib",HTTP_POST,[]{
    // p = 51 Werte (17 Zielfarben x R,G,B), komma-getrennt, je 0..255
    String p=web.arg("p");uint8_t tmp[LeviCal::N*3];int n=0,start=0;bool ok=true;
    while(true){
      int c=p.indexOf(',',start);String tok=(c<0)?p.substring(start):p.substring(start,c);tok.trim();
      if(n>=LeviCal::N*3){ok=false;break;}           // mehr als 51 Werte
      if(!integer(tok,255)){ok=false;break;}         // nur Ziffern, 1-3 Stellen, 0..255
      tmp[n++]=(uint8_t)tok.toInt();
      if(c<0)break;start=c+1;
    }
    if(!ok||n!=LeviCal::N*3){web.send(400,"text/plain","p=51 Werte 0..255");return;}
    calibSave(tmp);
    LEVI_LOG("Kalibrierung gespeichert (17 Punkte)");
    web.sendHeader("Cache-Control","no-store");web.send(200,"application/json",calibJson());});
  web.on("/api/calib/reset",HTTP_POST,[]{calibReset();LEVI_LOG("Kalibrierung zurueckgesetzt");web.send(200,"application/json",calibJson());});
  web.on("/api/music",HTTP_POST,[]{String v=web.arg("on");if(v!="0"&&v!="1"){web.send(400,"text/plain","on=0|1");return;}setMusic(v=="1");stateJson();});web.on("/api/color",HTTP_POST,[]{String r=web.arg("r"),g=web.arg("g"),b=web.arg("b");if(!integer(r,255)||!integer(g,255)||!integer(b,255)){web.send(400,"text/plain","r,g,b=0..255");return;}setRGB(r.toInt(),g.toInt(),b.toInt());stateJson();});web.begin();
}
static const char *wlStatusName(wl_status_t s){
  switch(s){
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED: return "SCAN_COMPLETED";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "UNKNOWN";
  }
}
static void onWifiEvent(arduino_event_id_t event,arduino_event_info_t info){
  switch(event){
    case ARDUINO_EVENT_WIFI_STA_START: WiFi.setHostname(LEVI_WIFI_HOSTNAME); LEVI_LOG("WLAN: STA gestartet, Hostname gesetzt auf %s", LEVI_WIFI_HOSTNAME); break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED: LEVI_LOG("WLAN: mit AP verbunden, Kanal %d", info.wifi_sta_connected.channel); break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: LEVI_LOG("WLAN: IP %s, RSSI %d dBm, aktiver Hostname \"%s\"", WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.getHostname()); break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: LEVI_LOG("WLAN: getrennt, Grund %u", (unsigned)info.wifi_sta_disconnected.reason); break;
    default: break;
  }
}
// Asynchroner WLAN-Scan: blockiert die Oberflaeche nicht. Ergebnis wird in der Schleife abgeholt.
static bool wlanScanning=false;
static void wlanScanStart(){
  WiFi.scanNetworks(true,false,false,300);
  wlanScanning=true;
}
// Gibt true zurueck, sobald ein laufender Scan abgeschlossen ist.
static bool wlanScanPoll(){
  if(!wlanScanning)return false;
  int n=WiFi.scanComplete();
  if(n==WIFI_SCAN_RUNNING)return false;
  wlanScanning=false;
  if(n<0){LEVI_LOG("WLAN-Scan fehlgeschlagen (%d)", n);}
  else{
    bool seen=false;
    for(int i=0;i<n;i++){
      if(WiFi.SSID(i)==LEVI_WIFI_SSID){seen=true;LEVI_LOG("WLAN: \"%s\" sichtbar, RSSI %d dBm, Kanal %d", LEVI_WIFI_SSID, WiFi.RSSI(i), WiFi.channel(i));}
    }
    LEVI_LOG("WLAN-Scan: %d Netze, Ziel-SSID %s", n, seen?"gefunden":"NICHT gefunden");
  }
  WiFi.scanDelete();
  return true;
}
static void wifiConnect(){
  static bool eventsRegistered=false;
  if(!eventsRegistered){WiFi.onEvent(onWifiEvent);eventsRegistered=true;}
  LEVI_LOG("WLAN: verbinde mit \"%s\"", LEVI_WIFI_SSID);
  WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);WiFi.setSleep(true);WiFi.setTxPower(LEVI_WIFI_TX_DBM<=8.5f?WIFI_POWER_8_5dBm:WIFI_POWER_11dBm);WiFi.setHostname(LEVI_WIFI_HOSTNAME);WiFi.begin(LEVI_WIFI_SSID,LEVI_WIFI_PASSWORD);lastWifiAttempt=millis();
}
void setup(){
  Serial.begin(115200);{uint32_t t0=millis();while(!Serial&&millis()-t0<2000)delay(10);}delay(200);
  LEVI_LOG("LeviLuma startet");
  stateLock=xSemaphoreCreateMutex();calLock=xSemaphoreCreateMutex();calibLoad();
  ledcSetup(0,5000,8);ledcAttachPin(2,0);backlight(0);
  lcd.init();
#ifndef LEVI_DISPLAY_ROTATION
#define LEVI_DISPLAY_ROTATION 0
#endif
  lcd.setRotation(LEVI_DISPLAY_ROTATION/90); // LovyanGFX: 0=0 Grad, 1=90, 2=180, 3=270
  lcd.fillScreen(TFT_BLACK);
  lv_init();display=lv_display_create(W,H);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,drawBuf,nullptr,sizeof(drawBuf),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
  input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,readTouch);lv_indev_set_display(input,display);
  makeUI();lv_timer_handler();backlight(255);lastTouchMs=millis();wifiConnect();webInit();
  xTaskCreatePinnedToCore(bleTask,"ble",8192,nullptr,1,nullptr,0);
}
void loop(){
  static uint32_t lastUI=0;uint32_t now=millis();
  {int32_t tx=0,ty=0;touchDown=lcd.getTouch(&tx,&ty);if(touchDown){touchX=(uint16_t)tx;touchY=(uint16_t)ty;}}
  lv_timer_handler();web.handleClient();
  if(now-lastUI>20){lastUI=now;updateUI();}
  if(!screenDim&&now-lastTouchMs>20000){screenDim=true;backlight(35);}
  // Lange ungenutzt: zurueck auf den Startbildschirm (Mitte), einmal pro Ruhephase
  static bool idleCentered=false;
  bool idle=now-lastTouchMs>LEVI_RECENTER_MS;
  if(idle&&!idleCentered)lv_obj_set_tile_id(tv,1,0,LV_ANIM_ON);
  idleCentered=idle;
  static bool mdns=false;static wl_status_t lastWl=WL_NO_SHIELD;
  // Nach einem abgeschlossenen Scan den Verbindungsversuch neu starten
  if(wlanScanPoll()){WiFi.disconnect();wifiConnect();}
  wl_status_t wl=WiFi.status();
  if(wl!=lastWl){LEVI_LOG("WLAN: Status %s -> %s", wlStatusName(lastWl), wlStatusName(wl));lastWl=wl;}
  if(wl==WL_CONNECTED){if(!mdns){mdns=MDNS.begin(LEVI_WIFI_HOSTNAME);if(mdns)MDNS.addService("http","tcp",80);LEVI_LOG("mDNS: %s.local %s", LEVI_WIFI_HOSTNAME, mdns?"aktiv":"FEHLER");}}
  else{
    if(mdns){MDNS.end();mdns=false;}
    // Retry bei jedem Status ausser CONNECTED (nicht nur DISCONNECTED), sonst bleibt der ESP bei NO_SSID_AVAIL/CONNECT_FAILED haengen.
    if(now-lastWifiAttempt>15000){
      lastWifiAttempt=now;
      LEVI_LOG("WLAN: neuer Verbindungsversuch (Status %s)", wlStatusName(wl));
      if(wl==WL_NO_SSID_AVAIL||wl==WL_CONNECT_FAILED){
        // Scan zuerst (asynchron), der Neustart der Verbindung folgt in wlanScanPoll()
        if(!wlanScanning)wlanScanStart();
      }else{
        WiFi.disconnect();
        wifiConnect();
      }
    }
  }
  delay(5);
}
