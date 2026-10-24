// Zustandsmodul: geteilter Zustand (BLE, Web, LVGL), Setter und Kalibrierung (NVS).
// stateLock schuetzt State, calLock schuetzt das Kalibriermodell. Leser arbeiten mit Kopien.
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include "Config.h"
#include "AppState.h"
#include "CalibCore.h"

SemaphoreHandle_t stateLock=nullptr;
static State state;
// Index in der Farbliste (0..16 Kalibrierfarben, MUSIC_IDX), -1 = freie Farbe.
// Besitz: nur der Loop-Task (LVGL-UI, Web-Handler setRGB/setMusic). Der BLE-Task liest 'selected' nicht,
// er arbeitet nur mit der Snapshot-Kopie von State. Deshalb ist kein Mutex noetig. Wenn BLE je darauf zugreift, unter stateLock verschieben.
int8_t selected=-1;
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
LeviCal::Model calSnapshot(bool *custom){
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
// NVS-Zugriffe: jeder Fehler wird geloggt und als Rueckgabewert gemeldet.
static void calibLoad(){
  LeviCal::Model m;calibDefaultPts(m);bool custom=false;
  Preferences p;
  if(!p.begin("levi",true)){LEVI_LOG("NVS: Oeffnen zum Lesen fehlgeschlagen, Standard-Kalibrierung");}
  else{
    if(p.getBytesLength("pts")==sizeof(m.pts)){
      if(p.getBytes("pts",m.pts,sizeof(m.pts))==sizeof(m.pts))custom=true;
      else{LEVI_LOG("NVS: Kalibrierung lesen fehlgeschlagen, Standard");calibDefaultPts(m);}
    }
    p.end();
  }
  LeviCal::solve(m);
  calCommit(m,custom);
}
// Neue Punkte (51 Werte) speichern. Gibt false zurueck, wenn NVS nicht schreiben konnte.
bool calibSave(const uint8_t *pts){
  LeviCal::Model m=calSnapshot();
  memcpy(m.pts,pts,sizeof(m.pts));
  LeviCal::solve(m);
  Preferences p;
  if(!p.begin("levi",false)){LEVI_LOG("NVS: Oeffnen zum Schreiben fehlgeschlagen");return false;}
  size_t written=p.putBytes("pts",m.pts,sizeof(m.pts));
  p.end();
  if(written!=sizeof(m.pts)){LEVI_LOG("NVS: Kalibrierung schreiben fehlgeschlagen (%u Bytes)",(unsigned)written);return false;}
  calCommit(m,true);
  return true;
}
// Entfernt die gespeicherte Kalibrierung. Gibt false zurueck, wenn NVS nicht erreichbar war.
bool calibReset(){
  Preferences p;
  if(!p.begin("levi",false)){LEVI_LOG("NVS: Oeffnen zum Loeschen fehlgeschlagen");return false;}
  bool removed=p.remove("pts");
  p.end();
  if(!removed)LEVI_LOG("NVS: Kalibrierung loeschen fehlgeschlagen (evtl. noch keine gespeichert)");
  LeviCal::Model m;calibDefaultPts(m);LeviCal::solve(m);
  calCommit(m,false);
  return true;
}
String calibJson(){
  bool custom=false;LeviCal::Model cm=calSnapshot(&custom);
  String j="{\"custom\":";j+=custom?"true":"false";j+=",\"points\":[";
  const uint8_t *flat=&cm.pts[0][0];
  for(int i=0;i<LeviCal::N*3;i++){if(i)j+=',';j+=String(flat[i]);}
  j+="]}";return j;
}
State snapshot(){ xSemaphoreTake(stateLock,portMAX_DELAY); State s=state; xSemaphoreGive(stateLock); return s; }
void stateSetBle(bool on){ xSemaphoreTake(stateLock,portMAX_DELAY); state.ble=on; xSemaphoreGive(stateLock); }
void stateMarkSent(uint32_t rev){ xSemaphoreTake(stateLock,portMAX_DELAY); if(state.sentRevision<rev)state.sentRevision=rev; xSemaphoreGive(stateLock); }

void setPower(bool on){ xSemaphoreTake(stateLock,portMAX_DELAY); state.power=on?1:0; ++state.revision; xSemaphoreGive(stateLock); }
void setBrightness(int b){ xSemaphoreTake(stateLock,portMAX_DELAY); state.brightness=constrain(b,1,100); ++state.revision; xSemaphoreGive(stateLock); }
void setRaw(uint8_t r,uint8_t g,uint8_t b){ xSemaphoreTake(stateLock,portMAX_DELAY); state.raw=true; state.r=r;state.g=g;state.b=b; ++state.revision; ++state.colorRev; xSemaphoreGive(stateLock); }
void setRGB(uint8_t r,uint8_t g,uint8_t b){ xSemaphoreTake(stateLock,portMAX_DELAY); state.raw=false; state.music=false; state.r=r;state.g=g;state.b=b; ++state.revision; ++state.colorRev; xSemaphoreGive(stateLock);
  selected=-1;for(int i=0;i<LeviCal::N;i++)if(LeviCal::TARGET[i][0]==r&&LeviCal::TARGET[i][1]==g&&LeviCal::TARGET[i][2]==b){selected=(int8_t)i;break;} }
void setMusic(bool on){xSemaphoreTake(stateLock,portMAX_DELAY);state.music=on;++state.revision;xSemaphoreGive(stateLock);if(on)selected=MUSIC_IDX;}
void stateInit(){
  stateLock=xSemaphoreCreateMutex();
  calLock=xSemaphoreCreateMutex();
  calibLoad();
}
