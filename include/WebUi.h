#pragma once
#include <Arduino.h>
static const char LEVI_HTML[] PROGMEM=R"LEVIHTML(<!doctype html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>LeviLuma</title>
<style>
*{box-sizing:border-box}body{margin:0;min-height:100vh;background:#eaeaea;color:#171717;font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;display:flex;align-items:center;justify-content:center;padding:18px}
.shell{background:#f3f3f3;border-radius:24px;padding:20px 17px 13px;width:min(100%,430px)}
header{text-align:center;padding:5px 0 20px}h1{font-size:36px;letter-spacing:-1.3px;margin:0;font-weight:760;display:flex;align-items:center;justify-content:center;gap:8px}
svg{display:block}header p{margin:4px 0;color:#777;font-size:15px}.branddots{display:flex;justify-content:center;gap:9px;margin-top:16px}.branddots i{width:15px;height:15px;border-radius:50%}
.card{background:white;border-radius:20px;padding:21px 20px 22px;box-shadow:0 4px 20px #00000007}
.top,.line{display:flex;align-items:center;justify-content:space-between;gap:12px}.top strong{font-size:17px}.chip{padding:6px 10px;border-radius:99px;background:#ededed;color:#686868;font-size:12px;font-weight:650;white-space:nowrap}.chip.ok{background:#d8f5df;color:#1e743e}
.line{margin:24px 0}.state{display:flex;align-items:center;gap:8px;font-size:19px;font-weight:400}.state svg{width:20px;height:20px}.power{border:0;border-radius:99px;background:#191919;color:#fff;font-size:14px;font-weight:700;padding:12px 16px;cursor:pointer}
.divider{height:1px;background:#ededed;margin:19px 0}.preview{width:84px;height:84px;border-radius:50%;border:4px solid #EDEDEB;background:#8b5cf6;margin:18px auto 10px;border-color:#EDEDEB}
.preview{border-color:#EDEDEB} .muted{color:#818181}.center{text-align:center;font-size:13px}
.swatches{display:flex;justify-content:center;gap:8px;margin:19px 0}.swatches button{width:32px;height:32px;border-radius:50%;border:0;cursor:pointer}.swatches button.sel{outline:3px solid #292929;outline-offset:0}
.colorline{display:flex;justify-content:center;align-items:center;gap:9px}.colorline input[type=color]{position:absolute;opacity:0;width:1px;height:1px}.colorpill{display:flex;align-items:center;gap:9px;border:1px solid #e1e1e1;border-radius:99px;padding:8px 14px;cursor:pointer;font-size:14px;color:#555}.colorpill .sample{width:15px;height:15px;border-radius:50%}
.brightnessline{display:flex;align-items:center;justify-content:space-between}.brightnessline b{font-size:17px}.brightnessline span{font-size:20px;color:#888}
input[type=range]{appearance:none;-webkit-appearance:none;width:100%;height:6px;border-radius:99px;background:linear-gradient(to right,#1c1c1c 75%,#e5e5e5 75%);margin:25px 0 13px;cursor:pointer}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:14px;height:28px;border-radius:99px;background:#fff;box-shadow:0 1px 6px #0004;border:1px solid #ddd}
input[type=range]::-moz-range-thumb{width:14px;height:28px;border-radius:99px;background:#fff;box-shadow:0 1px 6px #0004;border:1px solid #ddd}
footer{text-align:center;color:#8b8b8b;font-size:12px;padding:18px 0 2px}.footline{display:flex;justify-content:center;align-items:center;gap:7px}.note{font-size:10px;margin:8px auto 0;max-width:310px;line-height:1.4}
</style></head><body><div class="shell"><header><h1><svg width="33" height="37" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M9 18h6m-5 4h4m-2-20a7 7 0 0 0-4 12.7c.7.5 1 1.4 1 2.3h6c0-.9.3-1.8 1-2.3A7 7 0 0 0 12 2Z"/></svg>LeviLuma</h1><p>Light. Color. Control.</p><div class="branddots"><i style="background:#E24F62"></i><i style="background:#E7A23B"></i><i style="background:#5FC36B"></i><i style="background:#52B4D1"></i><i style="background:#8B5CF6"></i></div></header>
<main class="card"><div class="top"><strong>LED-Steuerung</strong><span id="ble" class="chip">BLE getrennt</span></div>
<div class="line"><span class="state"><svg id="powerIcon" viewBox="0 0 24 24" fill="none" stroke="#aaa" stroke-width="2.4" stroke-linecap="round"><path d="M12 2v10"/><path d="M6.1 5.8a9 9 0 1 0 11.8 0"/></svg><span id="pstatus">Status unbekannt</span></span><button class="power" id="power">Einschalten</button></div>
<div class="divider"></div><div class="preview" id="preview"></div><p class="muted center">Farbe auswählen</p><div class="swatches" id="swatches"></div>
<div class="colorline"><label class="colorpill" for="color"><svg width="19" height="19" viewBox="0 0 24 24" fill="none" stroke="#777" stroke-width="1.8" stroke-linecap="round"><path d="M12 3a9 9 0 1 0 0 18h1a2 2 0 0 0 1.5-3.3 1.5 1.5 0 0 1 1.2-2.5h2.1A4.2 4.2 0 0 0 22 11a9 9 0 0 0-10-8Z"/><circle cx="7.5" cy="11" r="1"/><circle cx="10" cy="6.5" r="1"/><circle cx="16" cy="7.5" r="1"/></svg><span class="sample" id="sample"></span><span id="hex">#8B5CF6</span></label><input type="color" id="color" value="#8b5cf6"></div>
<div class="divider"></div><div class="brightnessline"><b>Helligkeit</b><span id="value">75%</span></div><input id="brightness" type="range" min="1" max="100" value="75"></main>
<footer><div class="footline"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="#888" stroke-width="2" stroke-linecap="round"><path d="M2 8a15 15 0 0 1 20 0M5 12a10 10 0 0 1 14 0M9 16a5 5 0 0 1 6 0"/><circle cx="12" cy="20" r="1" fill="#888"/></svg>LeviLuma · ESP32-S3 · Lokale Steuerung</div><p class="note" title="Der LED-Controller bestätigt seinen Zustand nicht.">AN/AUS zeigt den zuletzt gesetzten Zustand, keine Bestätigung durch die LED.</p></footer></div>
<script>
const $=id=>document.getElementById(id),palette=['#ff2d55','#ff7a00','#00e060','#00c8ff','#8b5cf6'];let s={power:'unknown',r:139,g:92,b:246,brightness:75},editingUntil=0,sendTimer;
const rgb=h=>[1,3,5].map(i=>parseInt(h.slice(i,i+2),16)),hex=()=>('#'+[s.r,s.g,s.b].map(v=>v.toString(16).padStart(2,'0')).join(''));
function post(url){editingUntil=Date.now()+1200;fetch(url,{method:'POST'}).then(()=>refresh()).catch(()=>{});}
function setColor(h){[s.r,s.g,s.b]=rgb(h);render();post('/api/color?r='+s.r+'&g='+s.g+'&b='+s.b)}
palette.forEach(h=>{let b=document.createElement('button');b.style.background=h;b.setAttribute('aria-label',h);b.onclick=()=>setColor(h);$('swatches').appendChild(b)});
function render(){let h=hex();$('preview').style.background=h;$('sample').style.background=h;$('hex').textContent=h.toUpperCase();$('color').value=h;
$('value').textContent=s.brightness+'%';$('brightness').value=s.brightness;let pct=(s.brightness-1)/99*100;$('brightness').style.background='linear-gradient(to right,#191919 '+pct+'%,#e5e5e5 '+pct+'%)';
$('pstatus').textContent=s.power==='on'?'Eingeschaltet':s.power==='off'?'Ausgeschaltet':'Status unbekannt';$('power').textContent=s.power==='on'?'Ausschalten':'Einschalten';$('powerIcon').setAttribute('stroke',s.power==='on'?'#23a455':'#aaa');
[...$('swatches').children].forEach((b,i)=>b.classList.toggle('sel',palette[i]===h));}
function refresh(){fetch('/api/state',{cache:'no-store'}).then(r=>r.json()).then(d=>{let b=$('ble');b.textContent=d.ble?'BLE verbunden':'BLE getrennt';b.className='chip'+(d.ble?' ok':'');if(Date.now()>editingUntil){s=d;render();}}).catch(()=>{$('ble').textContent='ESP offline';$('ble').className='chip';});}
$('power').onclick=()=>{let on=s.power!=='on';s.power=on?'on':'off';render();post('/api/power?on='+(on?1:0))};
$('color').oninput=e=>setColor(e.target.value);
$('brightness').oninput=e=>{s.brightness=+e.target.value;render();editingUntil=Date.now()+1200;clearTimeout(sendTimer);sendTimer=setTimeout(()=>post('/api/brightness?value='+s.brightness),35)};
render();refresh();setInterval(refresh,1000);
</script></body></html>)LEVIHTML";
