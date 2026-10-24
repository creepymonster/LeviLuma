// BLE-Modul: Verbindung zum ELK/Lotus-Band und Senden der Befehle aus dem geteilten Zustand.
// Liest nur Kopien (snapshot/calSnapshot) und meldet Verbindung und Sendestand ueber AppState.h.
#include <Arduino.h>
#include <string>
#include <NimBLEDevice.h>
#include "Config.h"
#include "AppState.h"
#include "Ble.h"
#include "LotusProtocol.h"
#include "CalibCore.h"
#include "ColorMath.h"

// Lotus-Lantern-/ELK-Familie (gleiches Protokoll, Service 0xFFF0): Namen beginnen mit einem dieser Praefixe.
static bool bleNameMatches(const std::string &n){
  static const char *prefixes[]={"ELK-BLEDDM","ELK-BLEDOM","ELK-BLEDOB","MELK","LEDBLE"};
  for(const char *p:prefixes){if(n.rfind(p,0)==0)return true;}
  return false;
}
class ScanCallbacks: public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* dev) override {(void)dev;} // Scan komplett laufen lassen, damit das staerkste Geraet gewaehlt wird
};
class ClientCallbacks: public NimBLEClientCallbacks {void onDisconnect(NimBLEClient*) override {LEVI_LOG("BLE: Verbindung getrennt");stateSetBle(false);}};
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
      stateSetBle(false);
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
            if(writer){stateSetBle(true);lastPower=-1;lastMusic=true;/* Modus unbekannt: Musik-aus wird beim Verbinden erzwungen */lastBright=-1;lastColorRev=s.colorRev-1;instantColor=true;shown=ColorMath::fromRgb(s.r,s.g,s.b);LEVI_LOG("BLE: Service 0xFFF0 / Characteristic 0xFFF3 gefunden");}
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
        if(!attempted&&fadeLeft==0){stateMarkSent(s.revision);}
        if(ok) vTaskDelay(pdMS_TO_TICKS(200));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(25));
  }
}

void bleStart(){
  xTaskCreatePinnedToCore(bleTask,"ble",8192,nullptr,1,nullptr,0);
}
