#pragma once
#include <Arduino.h>
static const char LEVI_HTML[] PROGMEM=R"LEVIHTML(<!doctype html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>LeviLuma</title>
<style>
*{box-sizing:border-box}body{margin:0;min-height:100vh;background:#eaeaea;color:#171717;font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;display:flex;align-items:center;justify-content:center;padding:18px}
.shell{background:#f3f3f3;border-radius:24px;padding:20px 17px 13px;width:min(100%,1000px)}
header{text-align:center;padding:5px 0 20px}h1{font-size:36px;letter-spacing:-1.3px;margin:0;font-weight:760;display:flex;align-items:center;justify-content:center;gap:8px}
svg{display:block}header p{margin:4px 0;color:#777;font-size:15px}.branddots{display:flex;justify-content:center;gap:9px;margin-top:16px}.branddots i{width:15px;height:15px;border-radius:50%}
.calib{margin:16px auto 0;max-width:600px;width:100%}
.tri{display:grid;grid-template-columns:minmax(0,1fr);gap:14px;width:100%;max-width:600px;margin:0 auto}
.tile{display:flex;flex-direction:column;justify-content:center;padding:20px 18px}
.power-tile{display:flex;flex-direction:column;align-items:center;padding:20px 18px;gap:12px}
.dots{display:flex;align-items:center;gap:6px}
.dl{font-size:11px;font-weight:600;color:#888;line-height:1}.dots .wifi,.dots .bt{color:#9AA3AE;display:block}
.dot{display:inline-block;width:10px;height:10px;border-radius:50%;background:#e5484d}.dot.ok{background:#79da89}.dot.unk{background:#fff;box-shadow:inset 0 0 0 1px #c9c9c9}.pcol{position:relative;display:inline-flex;align-items:center;color:#888}.pcol .bulb{position:absolute;left:50%;bottom:calc(100% + 3px);transform:translateX(-50%)}
.statebar{display:flex;justify-content:center;margin-top:auto}
.statebar .state{font-size:11px;font-weight:600;color:#888}
.power-tile .power{margin:auto 0}
.tile .brightnessline{margin-bottom:6px}
.calib button{background:#fff;color:#171717;border:1px solid #e1e1e1;border-radius:999px;padding:9px 16px;font:inherit;font-size:14px;cursor:pointer;margin:4px}
.calib button:active{background:#f3f3f3}
.card{background:white;border-radius:20px;padding:21px 20px 22px;box-shadow:0 4px 20px #00000007}
.top,.line{display:flex;align-items:center;justify-content:space-between;gap:12px}.top strong{font-size:17px;font-weight:600}.chip{padding:6px 10px;border-radius:99px;background:#ededed;color:#686868;font-size:12px;font-weight:650;white-space:nowrap}.chip.ok{background:#d8f5df;color:#1e743e}
.line{margin:24px 0}.state{display:flex;align-items:center;gap:8px;font-size:15px;font-weight:400}.state svg{width:20px;height:20px}.power{border:0;border-radius:99px;background:#191919;color:#fff;font-size:15px;font-weight:600;padding:12px 16px;cursor:pointer}
.divider{height:1px;background:#ededed;margin:19px 0}.preview{width:84px;height:84px;border-radius:50%;border:4px solid #EDEDEB;background:#8b5cf6;margin:18px auto 10px;border-color:#EDEDEB}
.preview{border-color:#EDEDEB} .muted{color:#818181}.center{text-align:center;font-size:13px}
.swatches{display:grid;grid-template-columns:repeat(5,32px);justify-content:center;gap:10px;margin:4px 0 18px}.swatches button{width:32px;height:32px;border-radius:50%;border:1px solid #e1e1e1;cursor:pointer}.swatches button.sel{outline:3px solid #292929;outline-offset:0}
.colorline{display:flex;justify-content:center;align-items:center;gap:9px}.colorline input[type=color]{position:absolute;opacity:0;width:1px;height:1px}.colorpill{display:flex;align-items:center;gap:9px;border:1px solid #e1e1e1;border-radius:99px;padding:8px 14px;cursor:pointer;font-size:15px;color:#555}.colorpill .sample{width:15px;height:15px;border-radius:50%}
.brightnessline{display:flex;align-items:center;justify-content:space-between}.brightnessline b{font-size:15px;font-weight:400}.brightnessline span{font-size:15px;color:#888}
input[type=range]{appearance:none;-webkit-appearance:none;width:100%;height:6px;border-radius:99px;background:linear-gradient(to right,#1c1c1c 75%,#e5e5e5 75%);margin:25px 0 13px;cursor:pointer}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:14px;height:28px;border-radius:99px;background:#fff;box-shadow:0 1px 6px #0004;border:1px solid #ddd}
input[type=range]::-moz-range-thumb{width:14px;height:28px;border-radius:99px;background:#fff;box-shadow:0 1px 6px #0004;border:1px solid #ddd}
footer{text-align:center;color:#8b8b8b;font-size:12px;padding:18px 0 2px}.footline{display:flex;justify-content:center;align-items:center;gap:7px}.note{font-size:10px;margin:8px auto 0;max-width:310px;line-height:1.4}
input[type=range]{-webkit-appearance:none;appearance:none;width:100%;height:28px;background:transparent;margin:6px 0;cursor:pointer;touch-action:none}
input[type=range]::-webkit-slider-runnable-track{height:8px;border-radius:4px;background:linear-gradient(to right,#292929 var(--p,50%),#e5e5e5 var(--p,50%))}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;width:26px;height:26px;margin-top:-9px;border-radius:50%;background:#fff;border:1px solid #d8d8d8;box-shadow:0 2px 6px rgba(0,0,0,.22);transition:transform .15s}
input[type=range]:active::-webkit-slider-thumb{transform:scale(1.12)}
input[type=range]::-moz-range-track{height:8px;border-radius:4px;background:#e5e5e5}
input[type=range]::-moz-range-progress{height:8px;border-radius:4px;background:#292929}
input[type=range]::-moz-range-thumb{width:26px;height:26px;border-radius:50%;background:#fff;border:1px solid #d8d8d8;box-shadow:0 2px 6px rgba(0,0,0,.22)}
input[type=range]:focus{outline:none}
input[type=range]:focus-visible::-webkit-slider-thumb{box-shadow:0 0 0 4px rgba(41,41,41,.18)}
.locked{opacity:.4;pointer-events:none}
.pname{text-align:center;font-size:14px;color:#666;min-height:20px;margin:-8px 0 14px}</style></head><body><div class="shell"><header><h1><svg width="33" height="37" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M9 18h6m-5 4h4m-2-20a7 7 0 0 0-4 12.7c.7.5 1 1.4 1 2.3h6c0-.9.3-1.8 1-2.3A7 7 0 0 0 12 2Z"/></svg>LeviLuma</h1><p>Light. Color. Control.</p><div class="branddots"><i style="background:#E24F62"></i><i style="background:#E7A23B"></i><i style="background:#5FC36B"></i><i style="background:#52B4D1"></i><i style="background:#8B5CF6"></i></div></header>
<main class="tri"><section class="card tile power-tile" id="tPower"><div class="dots"><svg class="wifi" width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round"><path d="M5.60 12.60A3.4 3.4 0 0 1 10.40 12.60"/><path d="M3.19 10.19A6.8 6.8 0 0 1 12.81 10.19"/><path d="M0.79 7.79A10.2 10.2 0 0 1 15.21 7.79"/><circle cx="8.0" cy="15.0" r="1.6" fill="currentColor" stroke="none"/></svg><span id="wifi" class="dot" title="WLAN getrennt"></span><span class="pcol"><svg class="bulb" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"><path d="M9 18h6m-5 4h4m-2-20a7 7 0 0 0-4 12.7c.7.5 1 1.4 1 2.3h6c0-.9.3-1.8 1-2.3A7 7 0 0 0 12 2Z"/></svg><span id="pwr" class="dot unk" title="Status unbekannt"></span></span><span id="ble" class="dot" title="BLE getrennt"></span><svg class="bt" width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linejoin="round" stroke-linecap="round"><path d="M4 4.5L12 12L8 15V1L12 4L4 12"/></svg></div><button class="power" id="power">Einschalten</button></section><section class="card tile" id="tColors"><div class="swatches" id="swatches"></div><div class="pname" id="pname"></div>
<div class="colorline"><label class="colorpill" for="color"><svg width="19" height="19" viewBox="0 0 24 24" fill="none" stroke="#777" stroke-width="1.8" stroke-linecap="round"><path d="M12 3a9 9 0 1 0 0 18h1a2 2 0 0 0 1.5-3.3 1.5 1.5 0 0 1 1.2-2.5h2.1A4.2 4.2 0 0 0 22 11a9 9 0 0 0-10-8Z"/><circle cx="7.5" cy="11" r="1"/><circle cx="10" cy="6.5" r="1"/><circle cx="16" cy="7.5" r="1"/></svg><span class="sample" id="sample"></span><span id="hex">#8B5CF6</span></label><input type="color" id="color" value="#8b5cf6"></div>
</section><section class="card tile" id="tBright"><div class="brightnessline"><b>Helligkeit</b><span id="value">75%</span></div><input id="brightness" type="range" min="1" max="100" value="75"></section></main>
<section class="card calib"><div class="top"><strong>Kalibrierung</strong></div>
<p class="muted" id="calCurrent">Lade Kalibrierung …</p>
<textarea id="calOut" readonly rows="4" style="width:100%;box-sizing:border-box;font:13px ui-monospace,Menlo,monospace;border:1px solid #e1e1e1;border-radius:12px;padding:10px;background:#fafafa;color:#171717;resize:none"></textarea>
<div class="divider"></div>
<div class="center"><button class="power" id="calStart">Kalibrierung starten</button> <button id="calReset">Zurücksetzen</button></div>
<div id="calBox" style="display:none">
  <p class="center" id="calStep"></p>
  <div class="preview" id="calPreview"></div>
  <div id="calSliders"></div>
  <div class="center"><button class="power" id="calNext">Übernehmen</button> <button id="calCancel">Abbrechen</button></div>
  <p class="muted center" id="calInfo"></p>
</div>
</section>
<footer><div class="footline"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="#888" stroke-width="2" stroke-linecap="round"><path d="M2 8a15 15 0 0 1 20 0M5 12a10 10 0 0 1 14 0M9 16a5 5 0 0 1 6 0"/><circle cx="12" cy="20" r="1" fill="#888"/></svg>LeviLuma · ESP32-S3 · Lokale Steuerung</div><p class="note" title="Der LED-Controller bestätigt seinen Zustand nicht.">AN/AUS zeigt den zuletzt gesetzten Zustand, keine Bestätigung durch die LED.</p></footer></div>
<script>
const $=id=>document.getElementById(id);let palette=[];let s={power:'unknown',r:139,g:92,b:246,brightness:75},editingUntil=0,sendTimer;
const rgb=h=>[1,3,5].map(i=>parseInt(h.slice(i,i+2),16)),hex=()=>('#'+[s.r,s.g,s.b].map(v=>v.toString(16).padStart(2,'0')).join(''));
function post(url){editingUntil=Date.now()+1200;fetch(url,{method:'POST'}).then(()=>refresh()).catch(()=>{});}
function setColor(h){s.music=false;[s.r,s.g,s.b]=rgb(h);render();post('/api/color?r='+s.r+'&g='+s.g+'&b='+s.b)}
function buildSwatches(){$('swatches').innerHTML='';palette.forEach(c=>{let h=c.hex;let b=document.createElement('button');b.style.background=h;b.title=c.name;b.setAttribute('aria-label',c.name);b.onclick=()=>setColor(h);$('swatches').appendChild(b)});{let m=document.createElement('button');m.innerHTML='<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#fff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/></svg>';m.style.background='#292929';m.style.color='#fff';m.setAttribute('aria-label','Musikmodus');m.onclick=()=>{s.music=true;render();post('/api/music?on=1')};$('swatches').appendChild(m)};}
fetch('/api/palette',{cache:'no-store'}).then(r=>r.json()).then(j=>{palette=j.colors||[];buildSwatches();render();}).catch(()=>{});
function render(){let h=hex();let on=s.power==='on';document.querySelectorAll('.brightnessline,#brightness,#swatches').forEach(el=>el.classList.toggle('locked',!on));$('sample').style.background=h;$('hex').textContent=h.toUpperCase();$('color').value=h;
$('value').textContent=s.brightness+'%';$('brightness').value=s.brightness;$('brightness').style.setProperty('--p',((s.brightness-1)/99*100)+'%');let pct=(s.brightness-1)/99*100;$('brightness').style.background='linear-gradient(to right,#191919 '+pct+'%,#e5e5e5 '+pct+'%)';
{let pw=$('pwr');pw.className='dot'+(s.power==='on'?' ok':s.power==='off'?'':' unk');pw.title=s.power==='on'?'Eingeschaltet (grün)':s.power==='off'?'Ausgeschaltet (rot)':'Status unbekannt (weiß)';}$('power').textContent=s.power==='on'?'Ausschalten':'Einschalten';
[...$('swatches').children].forEach((b,i)=>b.classList.toggle('sel',!s.music&&!!palette[i]&&palette[i].hex===h));let pi=palette.findIndex(c=>c.hex===h);$('pname').textContent=s.music?'Musik':(pi>=0?palette[pi].name:'Eigene Farbe');let mb=$('swatches').lastElementChild;if(mb)mb.classList.toggle('sel',!!s.music);}
function refresh(){fetch('/api/state',{cache:'no-store'}).then(r=>r.json()).then(d=>{let w=$('wifi');w.title=d.wifi?'WLAN verbunden':'WLAN getrennt';w.className='dot'+(d.wifi?' ok':'');let b=$('ble');b.title=d.ble?'BLE verbunden':'BLE getrennt';b.className='dot'+(d.ble?' ok':'');if(Date.now()>editingUntil){s=d;render();}}).catch(()=>{$('wifi').title='ESP offline';$('wifi').className='dot';$('ble').title='ESP offline';$('ble').className='dot';});}
const togglePower=()=>{let on=s.power!=='on';s.power=on?'on':'off';render();post('/api/power?on='+(on?1:0))};
$('power').onclick=togglePower;
$('color').oninput=e=>setColor(e.target.value);
$('brightness').oninput=e=>{s.brightness=+e.target.value;e.target.style.setProperty('--p',((s.brightness-1)/99*100)+'%');render();editingUntil=Date.now()+1200;clearTimeout(sendTimer);sendTimer=setTimeout(()=>post('/api/brightness?value='+s.brightness),35)};
render();refresh();setInterval(refresh,1000);
// 17 Zielfarben (wie LED-Calibrator): Weiss (Planck), 75 %, 100 %
const CAL=[['D65',255,255,255],['D50',255,243,224],['4000K',255,228,189],['3000K',255,197,143],['2700K',255,180,117],
['Rot 75%',255,64,64],['Gelb 75%',255,255,64],['Grün 75%',64,255,64],['Cyan 75%',64,255,255],['Blau 75%',64,64,255],['Magenta 75%',255,64,255],
['Orange',255,128,64],['Türkis',0,255,128],['Hellblau',0,128,255],['Blau-Violett',128,0,255],['Rot-Violett',255,0,128],['Sat. Cyan',0,192,255]];
let calIdx=0,calDone=[],calCur=[0,0,0],calTimer=null;
function calBuildSliders(){const box=$('calSliders');box.innerHTML='';['R','G','B'].forEach((c,i)=>{const row=document.createElement('div');row.style.cssText='display:flex;gap:8px;align-items:center;margin:6px 0';row.innerHTML=`<span style="width:20px;font-size:15px;color:#171717">${c}</span><input type="range" min="0" max="255" id="calR${i}" style="flex:1"><span id="calV${i}" style="width:36px;text-align:right;font-size:15px;color:#888">0</span>`;box.appendChild(row);const inp=row.querySelector('input');inp.oninput=()=>{calCur[i]=+inp.value;$('calV'+i).textContent=inp.value;inp.style.setProperty('--p',(inp.value/255*100)+'%');calSendRaw();};});}
function calUpdateSliders(){[0,1,2].forEach(i=>{const el=$('calR'+i);el.value=calCur[i];el.style.setProperty('--p',(calCur[i]/255*100)+'%');$('calV'+i).textContent=calCur[i];});}
function calSendRaw(){clearTimeout(calTimer);calTimer=setTimeout(()=>fetch(`/api/raw?r=${calCur[0]}&g=${calCur[1]}&b=${calCur[2]}`,{method:'POST'}),60);}
function calRestoreColor(){post(`/api/color?r=${s.r}&g=${s.g}&b=${s.b}`);}
function calShow(){const c=CAL[calIdx];$('calStep').textContent=`Schritt ${calIdx+1}/${CAL.length}: ${c[0]} – LED auf die Vorschau einstellen`;$('calPreview').style.background=`rgb(${c[1]},${c[2]},${c[3]})`;calCur=[c[1],c[2],c[3]];calUpdateSliders();calSendRaw();}
function calRender(j){
  $('calCurrent').textContent=j.custom?'Eigene Kalibrierung aktiv (17 Punkte)':'Standard (FastLED TypicalSMD5050)';
  $('calOut').value='#define LEVI_CALIB_POINTS '+j.points.join(', ');
}
function calLoadCurrent(){fetch('/api/calib',{cache:'no-store'}).then(r=>r.json()).then(calRender).catch(()=>{});}
function calStart(){calIdx=0;calDone=[];$('calBox').style.display='';$('calInfo').textContent='';calShow();}
function calFinish(){
  const flat=[];calDone.forEach(c=>flat.push(c[0],c[1],c[2]));
  fetch('/api/calib?p='+flat.join(','),{method:'POST'}).then(r=>r.json()).then(j=>{calRender(j);$('calInfo').textContent='Gespeichert.';}).catch(()=>{$('calInfo').textContent='Fehler beim Speichern.';});
  $('calBox').style.display='none';calRestoreColor();
}
$('calStart').onclick=calStart;
$('calCancel').onclick=()=>{$('calBox').style.display='none';calRestoreColor();};
$('calNext').onclick=()=>{calDone.push([...calCur]);calIdx++;if(calIdx<CAL.length)calShow();else calFinish();};
$('calReset').onclick=()=>{fetch('/api/calib/reset',{method:'POST'}).then(r=>r.json()).then(j=>{calRender(j);$('calInfo').textContent='Zurückgesetzt.';});};
calBuildSliders();calLoadCurrent();
</script></body></html>)LEVIHTML";
