#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <lvgl.h>
#include "lv_theme_shadcn.h"
#include "LGFX_Board.h"
#include "LotusProtocol.h"
#include "Web.h"
#include "Config.h"
#include "Palette.h"
#include "BulbIcon.h"
#include "WifiIcon.h"
#include "BluetoothIcon.h"
#include "CalibCore.h"
#include "AppState.h"
#include "Ble.h"
#include "ColorMath.h"
#include <math.h>
#include <Wire.h>

// Logging ueber stdout (UART0-Konsole), also in demselben Monitor wie die NimBLE-Meldungen.
// ESP_LOGI wurde unterdrueckt (Log-Level), printf geht direkt auf die Konsole.
// Serial (USB-CDC) wird nicht mehr genutzt: dort kam nichts an, wenn der Monitor am anderen Port haengt.

#ifndef LEVI_DISPLAY_ROTATION
#define LEVI_DISPLAY_ROTATION 0
#endif
static_assert(LEVI_DISPLAY_ROTATION==0 || LEVI_DISPLAY_ROTATION==90 || LEVI_DISPLAY_ROTATION==180 || LEVI_DISPLAY_ROTATION==270,
              "LEVI_DISPLAY_ROTATION muss 0, 90, 180 oder 270 sein");
// Touch-Testscreen: 1 = nur Test-Oberflaeche (Tippen zeichnet Kreis + Koordinaten), 0 = normale Bedienung.
#ifndef LEVI_TOUCH_TEST
#define LEVI_TOUCH_TEST 0
#endif
// Touch: CST816S direkt ueber I2C (Wire). Liefert Rohkoordinaten im Panel-Koordinatensystem;
// mapTouch dreht sie passend zur Displaydrehung, damit LVGL die Oberflaeche richtig trifft.
namespace Cst {
static constexpr uint8_t ADDR=0x15;
static constexpr int PIN_SDA=6, PIN_SCL=7, PIN_RST=13;
static uint32_t lastOkMs=0;
static bool readRegs(uint8_t reg,uint8_t *buf,uint8_t n){
  Wire.beginTransmission(ADDR);Wire.write(reg);
  if(Wire.endTransmission(false)!=0)return false;
  if(Wire.requestFrom((int)ADDR,(int)n)!=n)return false;
  for(uint8_t i=0;i<n;i++)buf[i]=Wire.read();
  return true;
}
static void reset(){
  pinMode(PIN_RST,OUTPUT);digitalWrite(PIN_RST,LOW);delay(10);digitalWrite(PIN_RST,HIGH);delay(60);
}
static void init(){
  Wire.begin(PIN_SDA,PIN_SCL,400000);
  reset();lastOkMs=millis();
}
// true, wenn ein Finger auf dem Display ist. Antwortet der Chip nicht mehr, wird er zurueckgesetzt.
static bool read(uint16_t &x,uint16_t &y){
  uint8_t d[6];
  if(!readRegs(0x01,d,6)){
    if(millis()-lastOkMs>500){LEVI_LOG("Touch: keine Antwort, Reset");reset();lastOkMs=millis();}
    return false;
  }
  lastOkMs=millis();
  if(d[1]==0)return false;
  x=((d[2]&0x0F)<<8)|d[3];
  y=((d[4]&0x0F)<<8)|d[5];
  return true;
}
}
static void mapTouch(uint16_t rx,uint16_t ry,uint16_t &x,uint16_t &y){
  if(rx>239)rx=239;
  if(ry>239)ry=239;
  switch((LEVI_DISPLAY_ROTATION/90)&3){
    case 1: x=239-ry; y=rx; break;        // 90 Grad
    case 2: x=239-rx; y=239-ry; break;    // 180 Grad
    case 3: x=ry; y=239-rx; break;        // 270 Grad
    default: x=rx; y=ry;                  // 0 Grad
  }
}

static LGFX_Board lcd;
static constexpr uint16_t W=240,H=240;
alignas(4) static uint8_t drawBuf[240*24*2];   // 11520 bytes RGB565; LVGL buffer aligned
alignas(4) static uint8_t drawBuf2[240*24*2];  // zweiter Puffer: LVGL rendert, waehrend DMA aus dem ersten sendet
static lv_display_t *display=nullptr;
static lv_indev_t *input=nullptr;
static lv_obj_t *tv=nullptr,*tileBright=nullptr,*tilePower=nullptr,*tileColor=nullptr;
static lv_obj_t *bar=nullptr,*barLbl=nullptr,*center=nullptr,*pillLbl=nullptr,*holdFill=nullptr,*statusWifi=nullptr,*statusBle=nullptr;
static lv_obj_t *btIcon=nullptr,*wifiIcon=nullptr;
static lv_image_dsc_t btDsc{};
static lv_image_dsc_t wifiDsc{};
static lv_obj_t *statusPwr=nullptr,*bulbImg=nullptr;   // Einschalt-Status (weiss unbekannt, rot aus, gruen an) mit Gluehbirne
static lv_image_dsc_t bulbDsc{};
static lv_obj_t *dots[LeviPalette::N+1]{};
static lv_obj_t *pickerName=nullptr,*pickerHex=nullptr;   // Name und Hex-Wert in der Mitte des Farbwaehlers
static uint32_t lastTouchMs=0,lastWifiAttempt=0;
// Farbliste des Farbwaehlers (LeviPalette, Config LEVI_PICKER_LIST), danach der Musikmodus.
static int wrapIdx(int i){return ((i%LIST_COUNT)+LIST_COUNT)%LIST_COUNT;}
static bool touchDown=false;
static uint16_t touchX=120,touchY=120;
static bool touchBlocked=false;
static bool screenDim=false;
// Nach so vielen ms ohne Beruehrung springt die Ansicht zurueck auf die Mitte (An/Aus).
#ifndef LEVI_RECENTER_MS
#define LEVI_RECENTER_MS 60000
#endif
static uint8_t currentBacklight=255;

static void selectColor(int idx){int n=wrapIdx(idx);if(n==MUSIC_IDX){setMusic(true);return;}uint8_t r,g,b;LeviPalette::get(n,r,g,b);setRGB(r,g,b);}
static void backlight(uint8_t value){currentBacklight=value;ledcWrite(0,LEVI_BACKLIGHT_ACTIVE_HIGH?value:255-value);}
static void wake(){lastTouchMs=millis();if(screenDim){screenDim=false;backlight(255);}}
static lv_color_t rgbColor(uint8_t r,uint8_t g,uint8_t b){return lv_color_make(r,g,b);}
// RGB565-Bytes tauschen (LovyanGFX). Falls Farben vertauscht wirken: LEVI_RGB565_SWAP 0 setzen.
#ifndef LEVI_RGB565_SWAP
#define LEVI_RGB565_SWAP 1
#endif
// Flush-Zeiten messen: 1 = alle 5 s Mittel und Maximum der Wartezeiten auf die DMA ins Log.
#ifndef LEVI_FLUSH_STATS
#define LEVI_FLUSH_STATS 0
#endif
static void flush(lv_display_t *disp,const lv_area_t *a,uint8_t *px){
  // Synchrone Uebertragung (wie vor dem DMA-Umbau): LVGL wartet, bis die Pixel im Display sind.
  int32_t width=a->x2-a->x1+1,height=a->y2-a->y1+1;
  lcd.startWrite();lcd.setAddrWindow(a->x1,a->y1,width,height);
  lcd.pushPixels((const uint16_t*)px,width*height,LEVI_RGB565_SWAP);
  lcd.endWrite();
  lv_display_flush_ready(disp);
}
// Touch-Treiber: eine Beruehrung im gedimmten Zustand weckt nur das Display und wird nicht
// an die Oberflaeche weitergegeben, bis der Finger wieder oben ist. Kurze Aussetzer des CST816S
// (Loslassen unter 100 ms) werden ueberbrueckt. Gesten erkennt LVGL.
// Kurze Aussetzer des Touch-Controllers (Loslassen unter dieser Dauer) werden ueberbrueckt. 40 ms statt 100 ms,
// damit der echte Release schneller ankommt. Per Config.h (LEVI_TOUCH_BRIDGE_MS) einstellbar.
#ifndef LEVI_TOUCH_BRIDGE_MS
#define LEVI_TOUCH_BRIDGE_MS 40
#endif
static void readTouch(lv_indev_t*,lv_indev_data_t *d){
  static uint32_t seenMs=0;
  if(touchDown){seenMs=millis();lastTouchMs=seenMs;}
  bool down=touchDown||(millis()-seenMs<LEVI_TOUCH_BRIDGE_MS);
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
// Anzeige nur dort aktualisieren, wo sich der Wert geaendert hat.
struct UiView { bool on,ble,wifi,active; uint8_t r,g,b,brightness; int8_t selected; int8_t power; };
static UiView uiLast{};
static bool uiInit=false;
static void updatePowerUI(const UiView &v){
  // Farben kommen vom Theme: immer die Default-Variante, der Disabled-Zustand ist die Theme-Darstellung
  lv_theme_shadcn_set_variant(center,LV_THEME_SHADCN_VARIANT_DEFAULT);
  // Ohne BLE nicht bedienbar, ohne eigene Deckkraft: das Theme zeichnet den Disabled-Zustand
  if(v.ble)lv_obj_remove_state(center,LV_STATE_DISABLED);else lv_obj_add_state(center,LV_STATE_DISABLED);
  lv_obj_set_style_opa(center,LV_OPA_COVER,0);
  lv_label_set_text(pillLbl,v.on?"Ausschalten":"Einschalten");
}
static void updateConnectionUI(const UiView &v){
  // Statuspunkte: gruen verbunden, rot getrennt
  lv_obj_set_style_bg_color(statusWifi,v.wifi?lv_color_hex(0x79DA89):lv_color_hex(0xE5484D),0);
  lv_obj_set_style_bg_color(statusBle,v.ble?lv_color_hex(0x79DA89):lv_color_hex(0xE5484D),0);
  lv_obj_set_style_bg_color(statusPwr,v.power==1?lv_color_hex(0x79DA89):v.power==0?lv_color_hex(0xE5484D):lv_color_hex(0xFFFFFF),0);
}
static void updateBrightnessUI(const UiView &v){
  setEnabled(bar,v.active);
  setEnabled(barLbl,v.active);
  if(lv_slider_get_value(bar)!=v.brightness)lv_slider_set_value(bar,v.brightness,LV_ANIM_OFF);
  char label[12];snprintf(label,sizeof(label),"%u%%",(unsigned)v.brightness);
  lv_label_set_text(barLbl,label);
}
static void updatePickerLabels(const UiView &v);
static void updateColorUI(const UiView &v){
  // Farbpunkte: aktive Farbe mit weissem Rahmen
  for(int k=0;k<LeviPalette::N;k++){
    lv_obj_set_style_border_width(dots[k],k==v.selected?2:0,0);
    setEnabled(dots[k],v.active);
  }
  updatePickerLabels(v);
}
// Name des Eintrags (oder Musik / Eigene Farbe) und Hex-Wert in der Kreismitte.
static void updatePickerLabels(const UiView &v){
  char name[40]="",hex[12]="";
  if(v.selected==MUSIC_IDX){snprintf(name,sizeof(name),"Musik");}
  else{
    if(v.selected>=0&&v.selected<LeviPalette::N)snprintf(name,sizeof(name),"%s",LeviPalette::name(v.selected));
    else snprintf(name,sizeof(name),"Eigene Farbe");
    snprintf(hex,sizeof(hex),"#%02X%02X%02X",(unsigned)v.r,(unsigned)v.g,(unsigned)v.b);
  }
  lv_label_set_text(pickerName,name);lv_label_set_text(pickerHex,hex);
}
// Die festen Farben der Punkte werden nur einmal gesetzt.
static void setColorDotsOnce(){
  for(int k=0;k<LeviPalette::N;k++){uint8_t r,g,b;LeviPalette::get(k,r,g,b);lv_obj_set_style_bg_color(dots[k],lv_color_make(r,g,b),0);}
  lv_obj_set_style_bg_color(dots[MUSIC_IDX],lv_color_hex(0x333333),0);
}
static void updateUI(){
  State s=snapshot();
  UiView v{s.power==1,s.ble,WiFi.status()==WL_CONNECTED,false,s.r,s.g,s.b,s.brightness,selected,s.power};
  v.active=v.on&&v.ble;   // Helligkeit und Farben nur eingeschaltet und mit Band verbunden
  bool first=!uiInit;uiInit=true;
  const UiView &l=uiLast;
  bool powerChanged=first||v.on!=l.on||v.ble!=l.ble||v.r!=l.r||v.g!=l.g||v.b!=l.b;
  bool connChanged=first||v.wifi!=l.wifi||v.ble!=l.ble||v.power!=l.power;
  bool brightChanged=first||v.brightness!=l.brightness||v.active!=l.active||v.r!=l.r||v.g!=l.g||v.b!=l.b;
  bool colorChanged=first||v.selected!=l.selected||v.active!=l.active||v.r!=l.r||v.g!=l.g||v.b!=l.b;
  if(!(powerChanged||connChanged||brightChanged||colorChanged))return;
  if(first)setColorDotsOnce();
  if(powerChanged)updatePowerUI(v);
  if(connChanged)updateConnectionUI(v);
  if(brightChanged)updateBrightnessUI(v);
  if(colorChanged)updateColorUI(v);
  uiLast=v;
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
  int n=selected<0?0:(selected+1)%LeviPalette::N;
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

// Farbklima und Typografie: shadcn-Theme (lvgl_theme_shadcn), dunkle Basis zinc_dark, unveraendert.
static lv_theme_shadcn_palette_t themePalette{};
static void themeInit(lv_display_t *disp){
  static const lv_theme_shadcn_fonts_t fonts={.small=&lv_font_montserrat_16,.normal=&lv_font_montserrat_16,.large=&lv_font_montserrat_28,
                                              .h1=&lv_font_montserrat_48,.h2=&lv_font_montserrat_32,.h3=&lv_font_montserrat_28};
  themePalette=*lv_theme_shadcn_palette_zinc_dark();
  lv_theme_shadcn_init(disp,&themePalette,&fonts,LV_THEME_SHADCN_FLAG_NONE);
}
static void makeUI(){
  themeInit(lv_display_get_default());
  lv_obj_t *screen=lv_screen_active();
  lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
  // Drei Kacheln nebeneinander: links Helligkeit, Mitte An/Aus (Start), rechts Farben
  // LVGL-Tileview: drei Kacheln, Wischen links/rechts blaettert
  tv=lv_tileview_create(screen);lv_obj_set_size(tv,240,240);lv_obj_center(tv);
  lv_obj_set_style_bg_opa(tv,LV_OPA_TRANSP,0);
  lv_obj_set_style_border_width(tv,0,0);lv_obj_set_style_pad_all(tv,0,0);lv_obj_set_scrollbar_mode(tv,LV_SCROLLBAR_MODE_OFF);
  // Untereinander: An/Aus oben, Farbe in der Mitte, Helligkeit unten (Wischen nur vertikal)
  tilePower=lv_tileview_add_tile(tv,0,0,(lv_dir_t)LV_DIR_BOTTOM);
  tileColor=lv_tileview_add_tile(tv,0,1,(lv_dir_t)(LV_DIR_TOP|LV_DIR_BOTTOM));
  tileBright=lv_tileview_add_tile(tv,0,2,(lv_dir_t)LV_DIR_TOP);
  lv_obj_t *tiles[3]={tilePower,tileColor,tileBright};
  for(int i=0;i<3;i++){lv_obj_set_style_bg_opa(tiles[i],LV_OPA_TRANSP,0);lv_obj_set_style_pad_all(tiles[i],0,0);lv_obj_set_style_border_width(tiles[i],0,0);lv_obj_set_scrollbar_mode(tiles[i],LV_SCROLLBAR_MODE_OFF);}
  lv_obj_set_tile_id(tv,0,0,LV_ANIM_OFF);

  // Helligkeit: vertikaler Balken, Prozent oben
  // Helligkeit: waagerechte Spur, darueber der Prozentwert mit etwas Abstand
  bar=lv_slider_create(tileBright);lv_slider_set_range(bar,1,100);lv_obj_set_size(bar,180,18);lv_obj_align(bar,LV_ALIGN_CENTER,0,22);
  lv_obj_add_event_cb(bar,barChanged,LV_EVENT_VALUE_CHANGED,nullptr);
  barLbl=lv_label_create(tileBright);lv_obj_set_style_text_font(barLbl,&lv_font_montserrat_28,0);lv_obj_align(barLbl,LV_ALIGN_CENTER,0,-14);

  // Mitte: An/Aus mit Halten
  center=lv_button_create(tilePower);lv_obj_set_size(center,150,48);lv_obj_center(center);lv_obj_add_flag(center,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(center,centerPressed,LV_EVENT_PRESSED,nullptr);lv_obj_add_event_cb(center,centerReleased,LV_EVENT_RELEASED,nullptr);
  lv_obj_add_event_cb(center,centerReleased,LV_EVENT_PRESS_LOST,nullptr);lv_obj_add_event_cb(center,centerClicked,LV_EVENT_LONG_PRESSED,nullptr);
  lv_obj_set_style_radius(center,LV_RADIUS_CIRCLE,0);lv_obj_set_style_clip_corner(center,true,0);lv_obj_set_style_border_width(center,0,0);lv_obj_set_style_pad_all(center,0,0);
  lv_obj_set_style_shadow_width(center,0,0);lv_obj_set_style_shadow_opa(center,LV_OPA_30,0);lv_obj_remove_flag(center,LV_OBJ_FLAG_SCROLLABLE);
  // Fuellung beim Halten (liegt unter der Beschriftung, von der Pille beschnitten)
  holdFill=lv_obj_create(center);lv_obj_set_size(holdFill,0,48);lv_obj_set_style_radius(holdFill,LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_border_width(holdFill,0,0);lv_obj_set_style_pad_all(holdFill,0,0);lv_obj_set_style_shadow_width(holdFill,0,0);
  lv_obj_set_style_bg_color(holdFill,lv_color_white(),0);lv_obj_set_style_bg_opa(holdFill,LV_OPA_20,0);
  lv_obj_remove_flag(holdFill,LV_OBJ_FLAG_CLICKABLE);lv_obj_align(holdFill,LV_ALIGN_LEFT_MID,0,0);
  // Halbfett: die Beschriftung zweimal mit 1 px Versatz (LVGL hat keine fette Montserrat-Variante)
  pillLbl=lv_label_create(center);lv_obj_set_style_text_font(pillLbl,&lv_font_montserrat_16,0);lv_obj_center(pillLbl);
  // Statuspunkte oben: links WLAN (W), rechts Bluetooth (B)
  statusWifi=lv_obj_create(tilePower);lv_obj_set_size(statusWifi,10,10);lv_obj_set_style_radius(statusWifi,LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_border_width(statusWifi,0,0);lv_obj_align(statusWifi,LV_ALIGN_TOP_MID,-22,22);
  statusBle=lv_obj_create(tilePower);lv_obj_set_size(statusBle,10,10);lv_obj_set_style_radius(statusBle,LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_border_width(statusBle,0,0);lv_obj_align(statusBle,LV_ALIGN_TOP_MID,22,22);
  statusPwr=lv_obj_create(tilePower);lv_obj_set_size(statusPwr,10,10);lv_obj_set_style_radius(statusPwr,LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_border_width(statusPwr,0,0);lv_obj_align(statusPwr,LV_ALIGN_TOP_MID,0,22);
  // Gluehbirne ueber dem Einschalt-Status (A8-Bitmap, Farbe wie die Buchstaben W und B)
  bulbDsc.header.magic=LV_IMAGE_HEADER_MAGIC;bulbDsc.header.cf=LV_COLOR_FORMAT_A8;bulbDsc.header.flags=0;
  bulbDsc.header.w=12;bulbDsc.header.h=12;bulbDsc.header.stride=12;bulbDsc.header.reserved_2=0;
  bulbDsc.data_size=sizeof(kBulbA8);bulbDsc.data=kBulbA8;
  bulbImg=lv_image_create(tilePower);lv_image_set_src(bulbImg,&bulbDsc);
  lv_obj_set_style_image_recolor(bulbImg,lv_color_hex(0x888888),0);lv_obj_set_style_image_recolor_opa(bulbImg,LV_OPA_COVER,0);
  lv_obj_align_to(bulbImg,statusPwr,LV_ALIGN_OUT_TOP_MID,0,-3);
  // WLAN-Symbol links vom WLAN-Kreis (A8-Bitmap, gleiche Farbe wie B)
  wifiDsc.header.magic=LV_IMAGE_HEADER_MAGIC;wifiDsc.header.cf=LV_COLOR_FORMAT_A8;wifiDsc.header.flags=0;
  wifiDsc.header.w=12;wifiDsc.header.h=12;wifiDsc.header.stride=12;wifiDsc.header.reserved_2=0;
  wifiDsc.data_size=sizeof(kWifiA8);wifiDsc.data=kWifiA8;
  wifiIcon=lv_image_create(tilePower);lv_image_set_src(wifiIcon,&wifiDsc);
  lv_obj_set_style_image_recolor(wifiIcon,lv_color_hex(0x9AA3AE),0);lv_obj_set_style_image_recolor_opa(wifiIcon,LV_OPA_COVER,0);
  lv_obj_align_to(wifiIcon,statusWifi,LV_ALIGN_OUT_LEFT_MID,-4,-2);
  // Bluetooth-Rune rechts vom BLE-Kreis (A8-Bitmap, gleiche Farbe wie das WLAN-Symbol)
  btDsc.header.magic=LV_IMAGE_HEADER_MAGIC;btDsc.header.cf=LV_COLOR_FORMAT_A8;btDsc.header.flags=0;
  btDsc.header.w=12;btDsc.header.h=12;btDsc.header.stride=12;btDsc.header.reserved_2=0;
  btDsc.data_size=sizeof(kBtA8);btDsc.data=kBtA8;
  btIcon=lv_image_create(tilePower);lv_image_set_src(btIcon,&btDsc);
  lv_obj_set_style_image_recolor(btIcon,lv_color_hex(0x9AA3AE),0);lv_obj_set_style_image_recolor_opa(btIcon,LV_OPA_COVER,0);
  lv_obj_align_to(btIcon,statusBle,LV_ALIGN_OUT_RIGHT_MID,4,0);

  // Farbpunkte auf einem Kreis (passt zum runden Display), oben beginnend, im Uhrzeigersinn. Letzter Punkt: Musik.
  const int nDots=LeviPalette::N+1;
  const float ringR=90.0f;
  for(int k=0;k<nDots;k++){
    dots[k]=lv_obj_create(tileColor);lv_obj_set_size(dots[k],14,14);lv_obj_remove_flag(dots[k],LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(dots[k],LV_RADIUS_CIRCLE,0);lv_obj_set_style_pad_all(dots[k],0,0);
    lv_obj_set_style_border_color(dots[k],lv_color_white(),0);lv_obj_set_style_shadow_width(dots[k],0,0);
    float a=-1.5707963f+6.2831853f*k/nDots;
    lv_obj_align(dots[k],LV_ALIGN_CENTER,(int)lroundf(cosf(a)*ringR),(int)lroundf(sinf(a)*ringR));
  }
  pickerName=lv_label_create(tileColor);lv_obj_set_style_text_font(pickerName,&lv_font_montserrat_16,0);lv_obj_align(pickerName,LV_ALIGN_CENTER,0,-10);
  pickerHex=lv_label_create(tileColor);lv_theme_shadcn_set_text_role(pickerHex,LV_THEME_SHADCN_TEXT_MUTED);lv_obj_set_style_text_font(pickerHex,&lv_font_montserrat_16,0);lv_obj_align(pickerHex,LV_ALIGN_CENTER,0,12);
  lv_obj_add_flag(tileColor,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(tileColor,nextColorClicked,LV_EVENT_CLICKED,nullptr);
  updateUI();
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
#if LEVI_TOUCH_TEST
// Tippen: Bildschirm leeren, dann Kreis an der Tippposition mit den Koordinaten daneben zeichnen.
static void touchTestPressed(lv_event_t *e){
  lv_obj_t *pad=(lv_obj_t*)lv_event_get_target(e);
  lv_point_t p;lv_indev_get_point(lv_indev_active(),&p);
  lv_obj_clean(pad);
  lv_obj_t *c=lv_obj_create(pad);
  lv_obj_remove_flag(c,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(c,30,30);lv_obj_set_pos(c,p.x-15,p.y-15);
  lv_obj_set_style_radius(c,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_opa(c,LV_OPA_TRANSP,0);
  lv_obj_set_style_border_width(c,3,0);lv_obj_set_style_border_color(c,lv_color_hex(0xFF0000),0);
  char buf[24];snprintf(buf,sizeof(buf),"%d, %d",(int)p.x,(int)p.y);
  lv_obj_t *l=lv_label_create(pad);
  lv_label_set_text(l,buf);lv_obj_set_style_text_color(l,lv_color_white(),0);
  lv_obj_set_pos(l,p.x+18,p.y-8);
  LEVI_LOG("Touch-Test: %d, %d", (int)p.x, (int)p.y);
}
static void touchTestInit(){
  lv_obj_t *screen=lv_screen_active();
  lv_obj_t *pad=lv_obj_create(screen);
  lv_obj_set_size(pad,240,240);lv_obj_set_pos(pad,0,0);
  lv_obj_set_style_bg_color(pad,lv_color_hex(0x000000),0);lv_obj_set_style_bg_opa(pad,LV_OPA_COVER,0);
  lv_obj_set_style_border_width(pad,0,0);lv_obj_set_style_pad_all(pad,0,0);
  lv_obj_remove_flag(pad,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(pad,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(pad,touchTestPressed,LV_EVENT_PRESSED,nullptr);
  LEVI_LOG("Touch-Testscreen aktiv (LEVI_TOUCH_TEST 1)");
}
#endif

// Zeitbasis fuer LVGL (ms seit Start). Wird in setup() nach lv_init() gesetzt.
static uint32_t levTickMs(){return millis();}

void setup(){
  Serial.begin(115200);{uint32_t t0=millis();while(!Serial&&millis()-t0<2000)delay(10);}delay(200);
  LEVI_LOG("LeviLuma startet");
  stateInit();
  ledcSetup(0,5000,8);ledcAttachPin(2,0);backlight(0);
  lcd.init();LEVI_LOG("DBG: lcd.init fertig");
#ifndef LEVI_DISPLAY_ROTATION
#define LEVI_DISPLAY_ROTATION 0
#endif
  lcd.setRotation(LEVI_DISPLAY_ROTATION/90); // LovyanGFX: 0=0 Grad, 1=90, 2=180, 3=270
  Cst::init();
  lcd.fillScreen(TFT_BLACK);LEVI_LOG("DBG: fillScreen fertig");
  lv_init();LEVI_LOG("DBG: lv_init fertig");
  lv_tick_set_cb(levTickMs); // LVGL-Zeitbasis: Animationen, Long-Press, Timer
  display=lv_display_create(W,H);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,drawBuf,drawBuf2,sizeof(drawBuf),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
  input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(input,readTouch);lv_indev_set_display(input,display);
#if LEVI_TOUCH_TEST
  touchTestInit();
#else
  makeUI();LEVI_LOG("DBG: makeUI fertig");
#endif
  lv_timer_handler();backlight(255);lastTouchMs=millis();wifiConnect();webStart();
  bleStart();
}
void loop(){
  static uint32_t lastUI=0;uint32_t now=millis();
  {uint16_t rx=0,ry=0;touchDown=Cst::read(rx,ry);if(touchDown)mapTouch(rx,ry,touchX,touchY);}
  lv_timer_handler();webLoop();
#if !LEVI_TOUCH_TEST
  if(now-lastUI>20){lastUI=now;updateUI();}
#endif
  if(!screenDim&&now-lastTouchMs>20000){screenDim=true;backlight(35);}
  // Lange ungenutzt: zurueck auf den Startbildschirm (Mitte), einmal pro Ruhephase
  static bool idleCentered=false;
  bool idle=now-lastTouchMs>LEVI_RECENTER_MS;
#if !LEVI_TOUCH_TEST
  if(idle&&!idleCentered)lv_obj_set_tile_id(tv,0,0,LV_ANIM_OFF);
#endif
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
