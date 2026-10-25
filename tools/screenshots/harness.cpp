// Host-Harness fuer Screenshots der LCD-UI.
// Kompiliert die echte UI-Logik aus src/main.cpp (vom Skript render.sh extrahiert, als ui_part.inc)
// gegen LVGL und rendert vier feste Zustaende in PPM-Dateien. Hardware (LovyanGFX, BLE, WLAN) ist gestubbt.
#include "lvgl.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <string>
#include "CalibCore.h"
#include "Palette.h"

// --- Stubs fuer Arduino/ESP-Teile ---
static uint32_t millis(){return 0;}
static void ledcWrite(int,int){}
#define LEVI_BACKLIGHT_ACTIVE_HIGH true
enum wl_status_t{WL_IDLE_STATUS=0,WL_CONNECTED=3};
static struct { wl_status_t status(){return WL_CONNECTED;} } WiFi;
namespace lgfx{struct swap565_t{};}
struct StubLcd{ void waitDMA(){} void pushImageDMA(int,int,int,int,const lgfx::swap565_t*){} };
static StubLcd lcd;

struct State { bool ble=false; bool music=false; bool raw=false; int8_t power=-1; uint8_t r=139,g=92,b=246,brightness=75; uint32_t revision=0; uint32_t colorRev=0; uint32_t sentRevision=0; };
static State gState;
static State snapshot(){return gState;}
static int8_t selected=-1;
static constexpr int MUSIC_IDX=LeviCal::N;
static constexpr int LIST_COUNT=LeviCal::N+1;
static void setPower(bool){}
static void setBrightness(int){}
static void setRGB(uint8_t,uint8_t,uint8_t){}
static void setMusic(bool){}

#include "ui_part.inc"

static uint8_t *fb=nullptr;
static constexpr int FW=W, FH=H;   // Aufloesung kommt aus main.cpp (W, H)
static void flushCb(lv_display_t *d,const lv_area_t *a,uint8_t *px){
  const uint16_t *src=(const uint16_t*)px;
  int w=lv_area_get_width(a),h=lv_area_get_height(a);
  for(int y=0;y<h;y++)for(int x=0;x<w;x++){
    uint16_t v=src[y*w+x];
    uint8_t r=((v>>11)&31)*255/31,g=((v>>5)&63)*255/63,b=(v&31)*255/31;
    int idx=((a->y1+y)*FW+(a->x1+x))*3;
    fb[idx]=r;fb[idx+1]=g;fb[idx+2]=b;
  }
  lv_display_flush_ready(d);
}
static std::string outDir;
static void shot(lv_display_t *d,const char *name){
  lv_refr_now(d);
  FILE *f=fopen((outDir+"/"+name+".ppm").c_str(),"wb");
  if(!f){fprintf(stderr,"kann %s nicht schreiben\n",name);return;}
  fprintf(f,"P6\n%d %d\n255\n",FW,FH);fwrite(fb,1,FW*FH*3,f);fclose(f);
}
int main(int argc,char **argv){
  outDir=argc>1?argv[1]:".";
  lv_init();
  lv_tick_set_cb([](){return (uint32_t)0;});
  static uint8_t buf[W*H*2];
  fb=new uint8_t[FW*FH*3]();
  lv_display_t *d=lv_display_create(W,H);
  lv_display_set_buffers(d,buf,nullptr,sizeof(buf),LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(d,flushCb);
  display=d;
  makeUI();

  gState.power=0;gState.ble=false;updateUI();
  lv_obj_set_tile_id(tv,0,0,LV_ANIM_OFF);shot(d,"01_aus_getrennt");

  gState.power=1;gState.ble=true;gState.r=139;gState.g=92;gState.b=246;selected=-1;updateUI();
  lv_obj_set_tile_id(tv,0,0,LV_ANIM_OFF);shot(d,"02_an_mitte");

  LeviPalette::get(5,gState.r,gState.g,gState.b);selected=5;updateUI();
  lv_obj_set_tile_id(tv,0,1,LV_ANIM_OFF);shot(d,"03_farbe");

  gState.r=139;gState.g=92;gState.b=246;selected=-1;gState.brightness=40;updateUI();
  lv_obj_set_tile_id(tv,0,2,LV_ANIM_OFF);shot(d,"04_helligkeit");
  return 0;
}
