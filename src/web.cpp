// Web-API-Modul: HTTP-Endpunkte und Weboberflaeche.
// Aendert den Zustand nur ueber die Setter aus main.cpp (AppState.h), liest ihn ueber snapshot().
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "Config.h"
#include "AppState.h"
#include "CalibCore.h"
#include "WebUi.h"
#include "Web.h"

static WebServer web(80);

static bool integer(const String &v,int max){if(v.length()<1||v.length()>3)return false;for(char c:v)if(c<'0'||c>'9')return false;return v.toInt()<=max;}
static void stateJson(){State s=snapshot();String j="{\"wifi\":";j+=(WiFi.status()==WL_CONNECTED?"true":"false");j+=",\"ble\":";j+=(s.ble?"true":"false");j+=",\"power\":\"";j+=(s.power<0?"unknown":s.power==1?"on":"off");j+="\",\"r\":"+String(s.r)+",\"g\":"+String(s.g)+",\"b\":"+String(s.b)+",\"brightness\":"+String(s.brightness)+",\"music\":"+String(s.music?"true":"false")+",\"revision\":"+String(s.revision)+",\"sentRevision\":"+String(s.sentRevision)+"}";web.sendHeader("Cache-Control","no-store");web.send(200,"application/json",j);}
// Farbset des Farbwaehlers als JSON: {"colors":[{"name":"Rot","hex":"#ff4040"},...],"music":true}.
static String jsonEscape(const char *t){
  String o;
  for(const char *c=t;*c;c++){if(*c=='"'||*c=='\\')o+='\\';o+=*c;}
  return o;
}
static String paletteJson(){
  String j="{\"colors\":[";
  for(int i=0;i<LeviPalette::N;i++){
    uint8_t r,g,b;LeviPalette::get(i,r,g,b);
    char hex[10];snprintf(hex,sizeof(hex),"#%02x%02x%02x",r,g,b);
    if(i)j+=',';
    j+="{\"name\":\""+jsonEscape(LeviPalette::name(i))+"\",\"hex\":\""+String(hex)+"\"}";
  }
  j+="],\"music\":true}";
  return j;
}

void webStart(){
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
    if(!calibSave(tmp)){web.send(500,"text/plain","NVS-Fehler beim Speichern");return;}
    LEVI_LOG("Kalibrierung gespeichert (17 Punkte)");
    web.sendHeader("Cache-Control","no-store");web.send(200,"application/json",calibJson());});
  web.on("/api/calib/reset",HTTP_POST,[]{if(!calibReset()){web.send(500,"text/plain","NVS-Fehler");return;}LEVI_LOG("Kalibrierung zurueckgesetzt");web.send(200,"application/json",calibJson());});
  web.on("/api/music",HTTP_POST,[]{String v=web.arg("on");if(v!="0"&&v!="1"){web.send(400,"text/plain","on=0|1");return;}setMusic(v=="1");stateJson();});web.on("/api/color",HTTP_POST,[]{String r=web.arg("r"),g=web.arg("g"),b=web.arg("b");if(!integer(r,255)||!integer(g,255)||!integer(b,255)){web.send(400,"text/plain","r,g,b=0..255");return;}setRGB(r.toInt(),g.toInt(),b.toInt());stateJson();});web.on("/api/palette",HTTP_GET,[]{web.sendHeader("Cache-Control","no-store");web.send(200,"application/json",paletteJson());});web.begin();
}

void webLoop(){ web.handleClient(); }
