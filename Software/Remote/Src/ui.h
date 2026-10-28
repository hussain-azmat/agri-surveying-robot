#pragma once
// The web pages, served from flash. Plain HTML/CSS/JS, no external files or libraries, so they work
// on the rover's own network with no internet. All server-provided text is inserted with
// textContent (never innerHTML), so a hostile network name or path name cannot inject script.
#include <pgmspace.h>

static const char UI_CONTROL_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Rover control</title>
<style>
:root{--bg:#eceff4;--card:#fff;--panel:#f5f7fa;--tx:#0f1724;--mu:#667085;--ln:#d8dee8;--ac:#1f5fd6;--acs:#e6eefc;--ok:#12804a;--wa:#b45309;--er:#c8261c;--sh:0 1px 2px rgba(16,24,40,.06),0 4px 14px rgba(16,24,40,.05)}
:root[data-theme=dark]{--bg:#0d1117;--card:#161b22;--panel:#1d232c;--tx:#e6e9ef;--mu:#9aa4b5;--ln:#2a313b;--ac:#5b9cff;--acs:#182640;--ok:#4cc27a;--wa:#e5a73a;--er:#ff6b60;--sh:0 1px 2px rgba(0,0,0,.4)}
*{box-sizing:border-box}
html,body{margin:0}
body{background:var(--bg);color:var(--tx);font:14px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;padding-bottom:84px;-webkit-tap-highlight-color:transparent}
.mu{color:var(--mu)}.hide{display:none!important}
header.top{position:sticky;top:0;z-index:20;display:flex;align-items:center;gap:12px;padding:10px 16px;background:var(--card);border-bottom:1px solid var(--ln);box-shadow:var(--sh)}
.brand{display:flex;align-items:center;gap:10px;min-width:0}
.brand svg{flex:none;color:var(--ac)}
.brand b{display:block;font-size:15px;line-height:1.1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.brand span{font-size:12px;color:var(--mu)}
.chips{margin-left:auto;display:flex;align-items:center;gap:8px;flex-wrap:wrap;justify-content:flex-end}
.chip{display:inline-flex;align-items:center;gap:6px;padding:4px 10px;border:1px solid var(--ln);border-radius:99px;background:var(--panel);font-size:12px;white-space:nowrap}
.dot{width:8px;height:8px;border-radius:50%;background:var(--mu)}
.dot.ok{background:var(--ok)}.dot.wa{background:var(--wa)}.dot.er{background:var(--er)}
.bars{display:inline-flex;align-items:flex-end;gap:2px;height:12px}
.bars i{width:3px;background:var(--ln);border-radius:1px}.bars i.on{background:var(--ok)}
.bars i:nth-child(1){height:4px}.bars i:nth-child(2){height:7px}.bars i:nth-child(3){height:10px}.bars i:nth-child(4){height:12px}
.badge{padding:4px 12px;border-radius:6px;font-weight:700;font-size:12px;letter-spacing:.06em;background:var(--ln);color:var(--tx)}
.badge.MANUAL{background:var(--ok);color:#fff}.badge.AUTO{background:var(--ac);color:#fff}
.icon{width:32px;height:32px;padding:0;border-radius:8px;display:grid;place-items:center}
main.layout{max-width:1180px;margin:0 auto;padding:16px;display:grid;gap:16px;grid-template-columns:minmax(0,1fr)}
@media(min-width:900px){main.layout{grid-template-columns:minmax(0,5fr) minmax(0,6fr)}main.layout .wide{grid-column:1/-1}}
.card{background:var(--card);border:1px solid var(--ln);border-radius:14px;padding:16px;box-shadow:var(--sh)}
.card-h{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:12px}
h2{margin:0;font-size:12px;font-weight:700;color:var(--mu);text-transform:uppercase;letter-spacing:.08em}
button,select,input{font:inherit}
button{cursor:pointer;padding:9px 14px;border-radius:9px;border:1px solid var(--ln);background:var(--card);color:var(--tx);transition:background .12s,transform .05s}
button:hover:not(:disabled){background:var(--panel)}button:active:not(:disabled){transform:translateY(1px)}
button.p{background:var(--ac);border-color:var(--ac);color:#fff}button.p:hover:not(:disabled){background:var(--ac);filter:brightness(1.08)}
button.d{border-color:var(--er);color:var(--er)}
button:disabled{opacity:.45;cursor:not-allowed}
button:focus-visible,input:focus-visible,select:focus-visible{outline:2px solid var(--ac);outline-offset:2px}
input,select{padding:9px 10px;border-radius:9px;border:1px solid var(--ln);background:var(--panel);color:var(--tx);min-width:0}
input[type=range]{padding:0;accent-color:var(--ac);width:100%}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}.row+.row{margin-top:10px}
.seg{display:inline-flex;border:1px solid var(--ln);border-radius:9px;overflow:hidden}
.seg button{border:0;border-radius:0;padding:6px 12px;background:var(--panel);color:var(--mu);font-size:12px}
.seg button.on{background:var(--ac);color:#fff}
/* ---- remote ---- */
.armrow{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.armrow button{padding:12px;font-weight:700;letter-spacing:.05em}
.armnote{grid-column:1/-1;font-size:12px;color:var(--mu);min-height:17px}
.stage{position:relative;margin:16px auto 6px;width:min(100%,320px)}
.dpad{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;aspect-ratio:1/1}
.dpad button{padding:0;display:grid;place-items:center;border-radius:14px;background:linear-gradient(var(--card),var(--panel));border:1px solid var(--ln);box-shadow:0 2px 0 var(--ln);touch-action:none;user-select:none;-webkit-user-select:none;color:var(--tx)}
.dpad button svg{width:34px;height:34px;pointer-events:none}
.dpad button.dg{border-radius:10px;opacity:.8}.dpad button.dg svg{width:26px;height:26px}
.dpad button.on{background:var(--ac);border-color:var(--ac);color:#fff;box-shadow:none;transform:translateY(2px)}
.dpad #stopc{background:var(--er);border-color:var(--er);color:#fff;font-weight:800;font-size:13px;letter-spacing:.06em;border-radius:50%;box-shadow:0 2px 0 rgba(0,0,0,.25)}
.joy{position:relative;aspect-ratio:1/1;border-radius:50%;border:2px solid var(--ln);background:radial-gradient(circle,var(--panel) 0,var(--panel) 40%,var(--card) 100%);touch-action:none;user-select:none;-webkit-user-select:none}
.joy:before,.joy:after{content:"";position:absolute;background:var(--ln)}.joy:before{left:50%;top:8%;bottom:8%;width:1px}.joy:after{top:50%;left:8%;right:8%;height:1px}
#puck{position:absolute;width:26%;height:26%;left:37%;top:37%;border-radius:50%;background:var(--ac);box-shadow:0 3px 10px rgba(0,0,0,.25)}
.dpad.off [data-d]{opacity:.4;pointer-events:none}.joy.off{opacity:.4;pointer-events:none}
.speed{margin-top:14px;display:grid;gap:6px}
.speed .row{justify-content:space-between}
.readout{display:flex;gap:16px;justify-content:center;margin-top:8px;font-variant-numeric:tabular-nums;font-size:13px}
.readout b{display:inline-block;min-width:3.4em;text-align:right}
.wheels{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;margin-top:14px}
.wh{text-align:center;font-size:11px;color:var(--mu)}
.wh .tr{position:relative;height:56px;width:14px;margin:4px auto;background:var(--panel);border:1px solid var(--ln);border-radius:7px;overflow:hidden}
.wh .tr i{position:absolute;left:0;right:0;background:var(--ac);transition:height .1s,top .1s}
.wh .tr:after{content:"";position:absolute;left:0;right:0;top:50%;height:1px;background:var(--mu);opacity:.6}
.keys{margin-top:12px;display:flex;flex-wrap:wrap;gap:6px 14px;font-size:12px;color:var(--mu);justify-content:center}
kbd{font:600 11px ui-monospace,Consolas,monospace;padding:1px 6px;border:1px solid var(--ln);border-bottom-width:2px;border-radius:5px;background:var(--panel);color:var(--tx)}
/* ---- telemetry ---- */
.tele{display:grid;grid-template-columns:150px 1fr;gap:16px;align-items:center}
@media(max-width:520px){.tele{grid-template-columns:1fr}.compass{margin:0 auto}}
.compass{width:150px;height:150px}
.compass .ring{fill:var(--panel);stroke:var(--ln);stroke-width:2}
.compass text{fill:var(--mu);font:700 10px system-ui,sans-serif;text-anchor:middle}
.compass .tk{stroke:var(--ln);stroke-width:1.5}
.compass .needle{fill:var(--er)}.compass .tail{fill:var(--mu)}
.compass.na .needle,.compass.na .tail{opacity:.25}
.kv{display:grid;grid-template-columns:auto 1fr;gap:6px 14px;font-variant-numeric:tabular-nums}
.kv span:nth-child(odd){color:var(--mu)}
.kv span:nth-child(even){text-align:right;overflow-wrap:anywhere}
.pill{display:inline-block;padding:1px 8px;border-radius:99px;font-size:12px;font-weight:600;background:var(--panel);border:1px solid var(--ln)}
.pill.ok{color:var(--ok);border-color:var(--ok)}.pill.wa{color:var(--wa);border-color:var(--wa)}.pill.er{color:var(--er);border-color:var(--er)}
.mapbox{margin-top:14px;position:relative}
#map{width:100%;height:230px;border:1px solid var(--ln);border-radius:10px;background:var(--panel);display:block}
.mapbox .row{margin-top:8px;justify-content:space-between}
.prog{height:8px;border-radius:99px;background:var(--panel);border:1px solid var(--ln);overflow:hidden;margin:10px 0 4px}
.prog i{display:block;height:100%;width:0;background:var(--ac);transition:width .3s}
.rec{display:inline-flex;align-items:center;gap:6px;font-weight:600;color:var(--er)}
.rec:before{content:"";width:9px;height:9px;border-radius:50%;background:var(--er);animation:bl 1s infinite}
@keyframes bl{50%{opacity:.25}}
.note{font-size:12px;color:var(--mu);margin-top:8px}
.two{display:grid;gap:16px;grid-template-columns:minmax(0,1fr)}
@media(min-width:760px){.two{grid-template-columns:minmax(0,1fr) minmax(0,1fr)}}
/* ---- log, toasts, stop ---- */
#log{max-height:190px;overflow:auto;font:12px/1.5 ui-monospace,Consolas,monospace;display:grid;gap:2px}
#log div span{color:var(--mu);margin-right:8px}
#log .er{color:var(--er)}#log .ok{color:var(--ok)}#log .wa{color:var(--wa)}
#toasts{position:fixed;top:64px;left:50%;transform:translateX(-50%);z-index:30;display:grid;gap:8px;width:min(92vw,460px);pointer-events:none}
.toast{padding:10px 14px;border-radius:10px;background:var(--card);border:1px solid var(--ln);box-shadow:var(--sh);font-weight:600}
.toast.er{border-color:var(--er);color:var(--er)}.toast.ok{border-color:var(--ok);color:var(--ok)}.toast.wa{border-color:var(--wa);color:var(--wa)}
#lostbar{position:sticky;top:53px;z-index:19;background:var(--er);color:#fff;text-align:center;font-weight:700;padding:6px}
#stop{position:fixed;left:0;right:0;bottom:0;height:68px;z-index:25;border:0;border-radius:0;background:var(--er);color:#fff;font-size:20px;font-weight:800;letter-spacing:.14em}
#stop:hover:not(:disabled){background:var(--er);filter:brightness(1.08)}
#stop small{font-weight:500;font-size:11px;letter-spacing:.04em;opacity:.85;margin-left:10px}
.gate{border-color:var(--wa)}
@media(pointer:coarse){#stop small,.keys{display:none}}
a{color:var(--ac)}
</style></head><body>
<header class="top">
 <div class="brand">
  <svg width="30" height="30" viewBox="0 0 32 32" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="5" y="11" width="22" height="11" rx="3"/><circle cx="10" cy="24" r="3"/><circle cx="22" cy="24" r="3"/><path d="M16 11V6M12 6h8"/></svg>
  <div><b id="rid">Rover</b><span id="ver">Surveying rover</span></div>
 </div>
 <div class="chips">
  <span class="chip" id="linkChip"><i class="dot" id="linkDot"></i><span id="linkTxt">Connecting</span></span>
  <span class="chip" id="netChip"><span class="bars" id="bars"><i></i><i></i><i></i><i></i></span><span id="netTxt">-</span></span>
  <span class="chip" id="battChip">Battery <b id="battTxt">-</b></span>
  <span class="badge" id="badge">...</span>
  <button class="icon" id="theme" title="Switch light / dark" aria-label="Switch light or dark theme"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true"><circle cx="12" cy="12" r="9"/><path d="M12 3v18" /><path d="M12 3a9 9 0 0 1 0 18z" fill="currentColor"/></svg></button>
 </div>
</header>
<div id="lostbar" class="hide">LINK LOST. The rover stops by itself.</div>
<div id="toasts"></div>

<main class="layout">
 <section class="card gate wide" id="gate" hidden><div class="card-h"><h2>Access code</h2></div>
  <div class="row"><input id="code" placeholder="code from the rover's label" autocapitalize="none" autocomplete="off"><button class="p" id="codeBtn">Unlock</button></div></section>

 <section class="card" id="remote">
  <div class="card-h"><h2>Remote</h2>
   <div class="seg" id="modeSeg"><button data-m="pad" class="on">D-pad</button><button data-m="joy">Joystick</button></div></div>
  <div class="armrow">
   <button class="p" id="arm">ARM</button><button id="disarm">DISARM</button>
   <div class="armnote" id="armNote"></div>
  </div>
  <div class="stage">
   <div class="dpad" id="dpad">
    <button class="dg" data-d="fl" aria-label="Forward left"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"><path d="M17 17L7 7M7 17V7h10"/></svg></button>
    <button data-d="f" aria-label="Forward"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"><path d="M12 19V5M5 12l7-7 7 7"/></svg></button>
    <button class="dg" data-d="fr" aria-label="Forward right"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"><path d="M7 17L17 7M7 7h10v10"/></svg></button>
    <button data-d="l" aria-label="Turn left"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"><path d="M19 12H5M12 5l-7 7 7 7"/></svg></button>
    <button id="stopc" aria-label="Stop">STOP</button>
    <button data-d="r" aria-label="Turn right"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12h14M12 5l7 7-7 7"/></svg></button>
    <button class="dg" data-d="bl" aria-label="Back left"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"><path d="M17 7L7 17M7 7v10h10"/></svg></button>
    <button data-d="b" aria-label="Backward"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"><path d="M12 5v14M5 12l7 7 7-7"/></svg></button>
    <button class="dg" data-d="br" aria-label="Back right"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"><path d="M7 7l10 10M17 7v10H7"/></svg></button>
   </div>
   <div class="joy hide" id="joy"><div id="puck"></div></div>
  </div>
  <div class="readout"><span>Throttle <b id="rt">0.00</b></span><span>Turn <b id="rs">0.00</b></span></div>
  <div class="speed">
   <div class="row"><span class="mu">Speed limit</span><b id="spdv">60%</b></div>
   <input type="range" id="spd" min="20" max="100" step="5" value="60" aria-label="Speed limit">
   <div class="row"><button data-sp="30">Slow</button><button data-sp="60">Medium</button><button data-sp="100">Fast</button><span class="mu" id="cap" style="margin-left:auto"></span></div>
  </div>
  <div class="wheels" id="wheels"></div>
  <div class="keys"><span><kbd>&uarr;</kbd><kbd>&darr;</kbd><kbd>&larr;</kbd><kbd>&rarr;</kbd> or <kbd>W</kbd><kbd>A</kbd><kbd>S</kbd><kbd>D</kbd> drive (hold)</span><span><kbd>Space</kbd> / <kbd>Esc</kbd> STOP</span></div>
 </section>

 <section class="card" id="tele">
  <div class="card-h"><h2>Telemetry</h2><span class="pill" id="gpsPill">GPS</span></div>
  <div class="tele">
   <svg class="compass na" id="compass" viewBox="0 0 120 120" role="img" aria-label="Heading">
    <circle class="ring" cx="60" cy="60" r="56"/>
    <g id="ticks"></g>
    <text x="60" y="19">N</text><text x="105" y="64">E</text><text x="60" y="108">S</text><text x="15" y="64">W</text>
    <g id="needle"><polygon class="needle" points="60,24 66,60 54,60"/><polygon class="tail" points="60,96 66,60 54,60"/></g>
    <circle cx="60" cy="60" r="4" fill="var(--card)" stroke="var(--mu)" stroke-width="1.5"/>
    <text id="hdg" x="60" y="78" style="font-size:12px;fill:var(--tx)"></text>
   </svg>
   <div class="kv" id="kv"></div>
  </div>
  <div class="mapbox">
   <canvas id="map" aria-label="Track map"></canvas>
   <div class="row"><span class="note" id="mapNote" style="margin:0">North up. Trail of the GPS track since this page opened.</span><button id="mapClear">Clear trail</button></div>
  </div>
 </section>

 <section class="card" id="paths">
  <div class="card-h"><h2>Paths and auto mode</h2><span class="rec hide" id="recTag">REC</span></div>
  <div class="row"><input id="pname" placeholder="path name (optional)" maxlength="32" style="flex:1"><button id="recStart">Start recording</button><button id="recStop" class="d">Stop &amp; save</button></div>
  <div class="note" id="recNote"></div>
  <div class="row" style="margin-top:14px"><select id="plist" style="flex:1" aria-label="Saved paths"></select><button id="pload">Load</button><button id="prefresh">Refresh</button></div>
  <div class="row"><button class="p" id="auto">Start auto</button><span class="mu" id="autoNote"></span></div>
  <div class="prog" id="progBox"><i id="prog"></i></div>
  <div class="kv" id="auto2"></div>
  <div class="note">Place the rover at the start of the path first. If the heading is not aligned, drive forward about 5 m in manual. STOP always takes over.</div>
 </section>

 <section class="card" id="system">
  <div class="card-h"><h2>System</h2><span class="pill" id="otaPill">-</span></div>
  <div class="kv" id="sys"></div>
  <div class="prog hide" id="otaProgBox"><i id="otaProg"></i></div>
  <div class="row" style="margin-top:12px"><button id="ota">Check for update</button><button class="p" id="otaInstall" disabled>Install update</button></div>
  <div class="row"><button id="sync">Sync config</button><button id="cal">Calibrate IMU</button><a id="wifi" href="#">WiFi / token setup</a></div>
  <div class="row"><select id="bw" aria-label="Wheel"><option value="0">Front left</option><option value="1">Front right</option><option value="2">Rear left</option><option value="3">Rear right</option></select>
   <button id="bench">Spin 1 s</button><span class="mu">wheels OFF the ground</span></div>
 </section>

 <section class="card wide">
  <div class="card-h"><h2>Activity</h2><button id="logClear">Clear</button></div>
  <div id="log"></div>
 </section>
</main>
<button id="stop">STOP<small>Space or Esc</small></button>

<script>
const $=id=>document.getElementById(id);
const ls={get:k=>{try{return localStorage.getItem(k)}catch(e){return null}},set:(k,v)=>{try{localStorage.setItem(k,v)}catch(e){}}};
let key=ls.get('rk')||'',st=null,lastOk=0,lat=0,armedManual=false,shownUpd=false,prevMode='',prevReason='';
let ctl=ls.get('ctl')==='joy'?'joy':'pad',spd=Math.min(100,Math.max(20,parseInt(ls.get('spd')||'60',10)||60));
const q=(p,a)=>'/api/'+p+'?'+new URLSearchParams({...(a||{}),k:key});
async function api(p,a,m){try{const r=await fetch(q(p,a),{method:m||'POST',cache:'no-store'});
  if(r.status===401){$('gate').hidden=false;return {ok:false,msg:'access code needed'}}
  if(r.status===204)return {ok:true};
  return await r.json()}catch(e){return {ok:false,msg:'no connection'}}}
const hhmm=()=>new Date().toTimeString().slice(0,8);
function say(t,c){const cl=c||'wa';const d=document.createElement('div');d.className='toast '+cl;d.textContent=t;$('toasts').append(d);setTimeout(()=>d.remove(),4500);
 const l=document.createElement('div');l.className=cl;const s=document.createElement('span');s.textContent=hhmm();l.append(s,document.createTextNode(t));
 $('log').prepend(l);while($('log').childElementCount>60)$('log').lastChild.remove()}
function kv(el,rows){el.textContent='';for(const [k,v,c] of rows){const a=document.createElement('span');a.textContent=k;const b=document.createElement('span');
 if(c){const p=document.createElement('span');p.className='pill '+c;p.textContent=v;b.append(p)}else b.textContent=v;el.append(a,b)}}
$('codeBtn').onclick=()=>{key=$('code').value.trim();ls.set('rk',key);$('gate').hidden=true;poll()};
$('wifi').onclick=e=>{e.preventDefault();location.href='/setup?k='+encodeURIComponent(key)};
$('theme').onclick=()=>{const d=document.documentElement.dataset.theme==='dark'?'light':'dark';document.documentElement.dataset.theme=d;ls.set('theme',d)};
document.documentElement.dataset.theme=ls.get('theme')||(matchMedia('(prefers-color-scheme:dark)').matches?'dark':'light');
$('logClear').onclick=()=>{$('log').textContent=''};

// ---- static bits: compass ticks, wheel bars ----
(function(){for(let a=0;a<360;a+=30){if(a%90===0)continue;const r=a*Math.PI/180,ln=document.createElementNS('http://www.w3.org/2000/svg','line');
  ln.setAttribute('class','tk');ln.setAttribute('x1',(60+Math.sin(r)*50).toFixed(1));ln.setAttribute('y1',(60-Math.cos(r)*50).toFixed(1));
  ln.setAttribute('x2',(60+Math.sin(r)*56).toFixed(1));ln.setAttribute('y2',(60-Math.cos(r)*56).toFixed(1));$('ticks').append(ln)}
 for(const n of ['FL','FR','RL','RR']){const d=document.createElement('div');d.className='wh';const t=document.createElement('div');t.className='tr';t.append(document.createElement('i'));
  const l=document.createElement('div');l.textContent=n;d.append(t,l);$('wheels').append(d)}})();

// ---- status ----
const modeName={DISARMED:'DISARMED',MANUAL:'MANUAL',AUTO:'AUTO'};
function render(s){st=s;armedManual=s.mode==='MANUAL';const g=s.gps,i=s.imu;
 $('rid').textContent=s.id;$('ver').textContent='Firmware v'+s.ver;const b=$('badge');b.textContent=modeName[s.mode]||s.mode;b.className='badge '+s.mode;
 $('battTxt').textContent=s.batt?s.batt+' V':'n/a';
 const online=s.net==='ONLINE';const rs=online&&s.rssi?s.rssi:null;
 const lvl=rs===null?0:rs>-55?4:rs>-67?3:rs>-78?2:1;[...$('bars').children].forEach((e,k)=>e.classList.toggle('on',k<lvl));
 $('netTxt').textContent=online?(rs!==null?rs+' dBm':'online'):(s.net==='OFFLINE_AP'||s.net==='PORTAL'?'rover WiFi':s.net.toLowerCase());
 // heading
 const c=$('compass');c.classList.toggle('na',!(i.ok&&i.yawOk));$('needle').setAttribute('transform','rotate('+(i.yawOk?i.yaw:0)+' 60 60)');
 $('hdg').textContent=i.ok?(i.yawOk?i.yaw+'°':'no heading'):'no IMU';
 const gp=$('gpsPill');gp.className='pill '+(g.fix?'ok':(g.ok?'wa':'er'));gp.textContent=g.fix?'GPS fix':(g.ok?'searching':'no GPS');
 const kmh=(parseFloat(g.spd)*3.6).toFixed(1);
 kv($('kv'),[['Satellites / HDOP',g.ok?g.sats+' / '+g.hdop:'-'],['Position',g.fix?g.lat+', '+g.lon:'-'],['Speed',g.spd+' m/s ('+kmh+' km/h)'],['GPS rate',g.hz+' Hz'],
  ['Tilt',i.ok?i.tilt+'°':'-',i.ok&&parseFloat(i.tilt)>25?'er':null],['Gyro noise',i.ok?i.noise+' dps':'-'],['Straight-line drift',s.drift&&s.drift.ok?((parseFloat(s.drift.deg)>0?'+':'')+s.drift.deg+'°  ('+Math.round(s.drift.s)+' s)'):'-'],['Clock',s.clock?'set':'not set']]);
 // wheels
 [...$('wheels').children].forEach((w,k)=>{const v=Math.max(-1,Math.min(1,parseFloat(s.wheels[k])||0)),e=w.querySelector('i');e.style.height=Math.abs(v)*50+'%';e.style.top=v>=0?(50-v*50)+'%':'50%'});
 $('cap').textContent=s.cap?'rover cap '+Math.round(s.cap*100)+'%':'';
 // arm / drive
 $('arm').disabled=s.mode!=='DISARMED';$('disarm').disabled=s.mode==='DISARMED';
 $('armNote').textContent=s.mode==='DISARMED'?(s.reason?'Last stop: '+s.reason:'Disarmed. Press ARM to drive.'):(s.mode==='MANUAL'?'Armed. Hold a direction to drive; the rover stops when you let go.':'AUTO is running. Press STOP to take over.');
 $('dpad').classList.toggle('off',!armedManual);$('joy').classList.toggle('off',!armedManual);
 if(!armedManual)releaseAll();
 // paths
 $('recStart').disabled=!armedManual||s.rec;$('recStop').disabled=!s.rec;$('recTag').classList.toggle('hide',!s.rec);$('recTag').textContent='REC  '+s.recN+' pts';
 $('recNote').textContent=s.rec?'Recording. Drive the route, then Stop & save.':'Arm, press Start recording, drive the route, then Stop & save.';
 $('auto').disabled=s.mode==='AUTO'||!s.path;$('autoNote').textContent=s.path?'loaded: '+s.path:'no path loaded';
 const f=s.follow,act=s.mode==='AUTO'||f.state==='FINISHED';
 $('prog').style.width=(act&&f.n>0?Math.min(100,f.wp/f.n*100):0)+'%';
 kv($('auto2'),act?[['State',f.state,f.state==='RUNNING'||f.state==='FINISHED'?'ok':'er'],['Waypoint',f.wp+' / '+f.n],['Cross-track error',f.xte+' m'],['To next point',f.dist+' m']]:[]);
 // system
 const o=s.ota;$('otaPill').className='pill '+(o.avail?'wa':'ok');$('otaPill').textContent=o.avail?'v'+o.remote+' available':(o.state==='error'?'error':'up to date');
 const dl=o.state==='downloading';$('otaProgBox').classList.toggle('hide',!dl);$('otaProg').style.width=(o.pct||0)+'%';
 $('otaInstall').disabled=!(o.avail&&s.mode==='DISARMED');
 kv($('sys'),[['Firmware','v'+s.ver],['Update',(o.msg||o.state)+(o.auto?' (auto-install on)':' (manual install)')],['Network',s.net+(s.ip?'  '+s.ip:'')],['Address',s.host||'-'],
  ['Uptime',fmtUp(s.up)],['Free memory',s.heap?Math.round(s.heap/1024)+' KB':'-'],['Log',(s.log.bytes/1024).toFixed(1)+' KB pending, '+s.log.files+' queued, '+s.log.rows+' rows'],['Log space',s.log.mins>=0?'about '+s.log.mins+' min left':'-',s.log.mins>=0&&s.log.mins<5?'wa':null],...(s.log.drop>0?[['Rows dropped',String(s.log.drop),'wa']]:[]),['GitHub token',s.log.tok?'set':'not set',s.log.tok?'ok':'wa'],
  ...(s.log.err?[['Upload error',s.log.err,'er']]:[])]);
 // events
 if(s.mode!==prevMode&&prevMode){if(s.mode==='MANUAL')say('Armed (manual)','ok');else if(s.mode==='AUTO')say('Auto mode started','ok');else say('Disarmed'+(s.reason?': '+s.reason:''),s.reason&&s.reason!=='USER_DISARM'&&s.reason!=='USER_STOP'?'er':'wa')}
 prevMode=s.mode;
 if(s.updatedFrom&&!shownUpd){shownUpd=true;say('Updated from v'+s.updatedFrom+' to v'+s.ver+'. The rover is disarmed.','ok')}
 trackPoint(s);drawMap();
}
function fmtUp(t){if(t==null)return '-';const h=Math.floor(t/3600),m=Math.floor(t%3600/60);return (h?h+' h ':'')+m+' min'}
async function poll(){const t0=performance.now();const s=await api('status',null,'GET');
 if(s&&s.id){lastOk=Date.now();lat=Math.round(performance.now()-t0);render(s)}
 const lost=Date.now()-lastOk>1500;$('lostbar').classList.toggle('hide',!lost);
 $('linkDot').className='dot '+(lost?'er':(lat>600?'wa':'ok'));$('linkTxt').textContent=lost?'No link':lat+' ms';
 if(lost)releaseAll();
 setTimeout(poll,400)}
poll();
async function loadPaths(){const l=await api('paths',null,'GET');const sel=$('plist');sel.textContent='';
 const arr=Array.isArray(l)?l:[];if(!arr.length){const o=document.createElement('option');o.textContent='no saved paths';o.value='';sel.append(o)}
 for(const p of arr){const o=document.createElement('option');o.value=p.name;o.textContent=p.name+(p.pending?'  (uploading)':'');sel.append(o)}}
loadPaths();$('prefresh').onclick=loadPaths;

// ---- driving: D-pad buttons, arrow keys / WASD, joystick, speed ----
const held={};            // source -> Set of 'f','b','l','r'
const TOK={f:['f'],b:['b'],l:['l'],r:['r'],fl:['f','l'],fr:['f','r'],bl:['b','l'],br:['b','r']};
const KEYS={ArrowUp:'f',KeyW:'f',ArrowDown:'b',KeyS:'b',ArrowLeft:'l',KeyA:'l',ArrowRight:'r',KeyD:'r'};
let stick={x:0,y:0},cmd={t:0,s:0};
function press(src,set){held[src]=set;paint()}
function lift(src){delete held[src];paint()}
function releaseAll(){for(const k in held)delete held[k];stick={x:0,y:0};puck(0,0);paint()}
function dirs(){const u=new Set();for(const k in held)held[k].forEach(x=>u.add(x));return u}
function compute(){const sc=spd/100;
 if(ctl==='joy'){const dz=0.12,f=v=>Math.abs(v)<dz?0:v;return {t:-f(stick.y)*sc,s:f(stick.x)*Math.max(sc,.4)}}
 const u=dirs();let t=(u.has('f')?1:0)-(u.has('b')?1:0),s=(u.has('r')?1:0)-(u.has('l')?1:0);
 if(t&&s)s*=.55;return {t:t*sc,s:s*Math.max(sc,.4)}}
function paint(){const u=dirs();document.querySelectorAll('#dpad [data-d]').forEach(b=>{const need=TOK[b.dataset.d];  // lit only when it matches the whole pressed combination
  b.classList.toggle('on',u.size===need.length&&need.every(x=>u.has(x)))})}
document.querySelectorAll('#dpad [data-d]').forEach(b=>{const id='b'+b.dataset.d;
 b.addEventListener('pointerdown',e=>{if(!armedManual)return;e.preventDefault();try{b.setPointerCapture(e.pointerId)}catch(_){}press(id,new Set(TOK[b.dataset.d]))});
 const up=()=>lift(id);b.addEventListener('pointerup',up);b.addEventListener('pointercancel',up);b.addEventListener('lostpointercapture',up);
 b.addEventListener('contextmenu',e=>e.preventDefault())});
const typing=e=>/^(INPUT|SELECT|TEXTAREA)$/.test((e.target||{}).tagName||'');
document.addEventListener('keydown',e=>{
 if(e.code==='Escape'||(e.code==='Space'&&!typing(e))){e.preventDefault();doStop();return}
 if(typing(e))return;
 const k=KEYS[e.code];if(!k||e.ctrlKey||e.metaKey||e.altKey||!armedManual||ctl==='joy')return;
 e.preventDefault();if(!e.repeat)press('k'+e.code,new Set([k]))});
document.addEventListener('keyup',e=>{if(e.code==='Space'&&!typing(e))e.preventDefault();  // a focused button must not "click" on Space
 if(KEYS[e.code])lift('k'+e.code)});
window.addEventListener('blur',releaseAll);document.addEventListener('visibilitychange',()=>{if(document.hidden)releaseAll()});
// joystick
const joy=$('joy');let drag=false;
function puck(x,y){$('puck').style.left=(37+x*37)+'%';$('puck').style.top=(37+y*37)+'%'}
function ptr(e){const r=joy.getBoundingClientRect();let x=(e.clientX-r.left-r.width/2)/(r.width*.37),y=(e.clientY-r.top-r.height/2)/(r.height*.37);const n=Math.hypot(x,y);if(n>1){x/=n;y/=n}stick={x,y};puck(x,y)}
joy.onpointerdown=e=>{if(!armedManual)return;drag=true;try{joy.setPointerCapture(e.pointerId)}catch(_){}ptr(e)};
joy.onpointermove=e=>{if(drag)ptr(e)};
const jrel=()=>{drag=false;stick={x:0,y:0};puck(0,0)};joy.onpointerup=jrel;joy.onpointercancel=jrel;
// send the command 10x per second; silence = stop on the rover
setInterval(()=>{cmd=compute();$('rt').textContent=cmd.t.toFixed(2);$('rs').textContent=cmd.s.toFixed(2);
 if(armedManual&&Date.now()-lastOk<1500)fetch(q('drive',{t:cmd.t.toFixed(2),s:cmd.s.toFixed(2)}),{method:'POST',keepalive:true}).catch(()=>{})},100);
// mode + speed controls
function setCtl(m){ctl=m;ls.set('ctl',m);releaseAll();$('dpad').classList.toggle('hide',m==='joy');joy.classList.toggle('hide',m!=='joy');
 document.querySelectorAll('#modeSeg button').forEach(b=>b.classList.toggle('on',b.dataset.m===m))}
document.querySelectorAll('#modeSeg button').forEach(b=>b.onclick=()=>setCtl(b.dataset.m));setCtl(ctl);
function setSpd(v){spd=Math.min(100,Math.max(20,v));$('spd').value=spd;$('spdv').textContent=spd+'%';ls.set('spd',String(spd))}
$('spd').oninput=e=>setSpd(parseInt(e.target.value,10));document.querySelectorAll('[data-sp]').forEach(b=>b.onclick=()=>setSpd(parseInt(b.dataset.sp,10)));setSpd(spd);

$('arm').onclick=async()=>{const r=await api('arm');if(!r.ok)say(r.msg,'er')};
$('disarm').onclick=async()=>{armedManual=false;releaseAll();const r=await api('disarm');if(!r.ok)say(r.msg,'er')};
const doStop=async()=>{armedManual=false;releaseAll();for(let i=0;i<3;i++){const r=await api('stop');if(r.ok)break}};
$('stop').onclick=doStop;$('stopc').onclick=doStop;

// ---- paths / system buttons ----
$('recStart').onclick=async()=>{const r=await api('rec/start');say(r.msg,r.ok?'ok':'er')};
$('recStop').onclick=async()=>{const r=await api('rec/stop',{name:$('pname').value.trim()});say(r.msg,r.ok?'ok':'er');loadPaths()};
$('pload').onclick=async()=>{if(!$('plist').value)return say('no path selected','wa');say('loading path...','wa');const r=await api('path/load',{name:$('plist').value});say(r.msg,r.ok?'ok':'er')};
$('auto').onclick=async()=>{if(!confirm('Start auto mode? Keep clear of the rover. The STOP button always works.'))return;const r=await api('auto/start');if(!r.ok)say(r.msg,'er')};
$('ota').onclick=async()=>{say('checking for updates...','wa');const r=await api('ota/check');say(r.msg,r.ok?'ok':'er')};
$('otaInstall').onclick=async()=>{if(!confirm('Install the update now? The rover restarts and stays disarmed.'))return;say('installing...','wa');const r=await api('ota/install');say(r.msg,r.ok?'ok':'er')};
$('sync').onclick=async()=>{const r=await api('config/sync');say(r.msg,r.ok?'ok':'er')};
$('cal').onclick=async()=>{const r=await api('imu/calibrate');say(r.msg,r.ok?'ok':'er')};
$('bench').onclick=async()=>{const r=await api('bench',{w:$('bw').value,v:'0.3'});say(r.msg,r.ok?'ok':'er')};

// ---- track map (north up, local metres around the first fix) ----
let trail=[],org=null;
function trackPoint(s){const g=s.gps;if(!g.fix)return;const la=parseFloat(g.lat),lo=parseFloat(g.lon);if(!isFinite(la)||!isFinite(lo))return;
 if(!org)org={la,lo,k:Math.cos(la*Math.PI/180)*111320};
 const p={x:(lo-org.lo)*org.k,y:(la-org.la)*110540,yaw:s.imu.yawOk?parseFloat(s.imu.yaw):null};
 const l=trail[trail.length-1];if(!l||Math.hypot(p.x-l.x,p.y-l.y)>=1){trail.push(p);if(trail.length>800)trail.shift()}else{l.yaw=p.yaw}}
$('mapClear').onclick=()=>{trail=[];org=null;drawMap()};
function drawMap(){const cv=$('map'),dpr=window.devicePixelRatio||1,w=cv.clientWidth,h=cv.clientHeight;if(!w)return;
 if(cv.width!==Math.round(w*dpr)){cv.width=Math.round(w*dpr);cv.height=Math.round(h*dpr)}
 const x=cv.getContext('2d');x.setTransform(dpr,0,0,dpr,0,0);x.clearRect(0,0,w,h);
 const css=getComputedStyle(document.documentElement),col=n=>css.getPropertyValue(n).trim();
 if(trail.length<1){x.fillStyle=col('--mu');x.font='13px system-ui';x.textAlign='center';x.fillText('Waiting for a GPS fix...',w/2,h/2);return}
 let x0=1e9,x1=-1e9,y0=1e9,y1=-1e9;for(const p of trail){x0=Math.min(x0,p.x);x1=Math.max(x1,p.x);y0=Math.min(y0,p.y);y1=Math.max(y1,p.y)}
 const span=Math.max(x1-x0,y1-y0,30),pad=26,sc=(Math.min(w,h)-2*pad)/span,cx=(x0+x1)/2,cy=(y0+y1)/2;
 const X=p=>w/2+(p.x-cx)*sc,Y=p=>h/2-(p.y-cy)*sc;
 // grid every nice step
 const nice=[1,2,5,10,20,50,100,200,500,1000].find(v=>v*sc>=36)||1000;x.strokeStyle=col('--ln');x.lineWidth=1;x.fillStyle=col('--mu');x.font='10px system-ui';
 for(let gx=Math.ceil((cx-w/2/sc)/nice)*nice;gx<cx+w/2/sc;gx+=nice){const px=w/2+(gx-cx)*sc;x.beginPath();x.moveTo(px,0);x.lineTo(px,h);x.stroke()}
 for(let gy=Math.ceil((cy-h/2/sc)/nice)*nice;gy<cy+h/2/sc;gy+=nice){const py=h/2-(gy-cy)*sc;x.beginPath();x.moveTo(0,py);x.lineTo(w,py);x.stroke()}
 x.textAlign='left';x.fillText(nice+' m grid',8,h-8);x.textAlign='right';x.fillText('N',w-8,14);
 x.strokeStyle=col('--ac');x.lineWidth=2.5;x.lineJoin='round';x.beginPath();trail.forEach((p,k)=>k?x.lineTo(X(p),Y(p)):x.moveTo(X(p),Y(p)));x.stroke();
 const a=trail[0];x.fillStyle=col('--ok');x.beginPath();x.arc(X(a),Y(a),5,0,7);x.fill();
 const b=trail[trail.length-1],bx=X(b),by=Y(b);x.fillStyle=col('--er');
 if(b.yaw!==null){const r=b.yaw*Math.PI/180;x.save();x.translate(bx,by);x.rotate(r);x.beginPath();x.moveTo(0,-10);x.lineTo(7,8);x.lineTo(0,4);x.lineTo(-7,8);x.closePath();x.fill();x.restore()}else{x.beginPath();x.arc(bx,by,6,0,7);x.fill()}}
addEventListener('resize',drawMap);
</script></body></html>)HTML";

static const char UI_SETUP_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Rover WiFi setup</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--tx:#14181f;--mu:#667085;--ln:#dde1e7;--ac:#1d5fd1;--ok:#157f3b;--er:#c0261d}
@media(prefers-color-scheme:dark){:root{--bg:#12151a;--card:#1b2027;--tx:#e8ebf0;--mu:#98a2b3;--ln:#2b323c;--ac:#5b9cff;--ok:#4cc27a;--er:#ff7b72}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--tx);font:15px/1.4 system-ui,sans-serif}
main{max-width:520px;margin:0 auto;padding:14px;display:grid;gap:12px}
.card{background:var(--card);border:1px solid var(--ln);border-radius:12px;padding:14px}
h1{font-size:19px;margin:0}h2{margin:0 0 8px;font-size:14px;color:var(--mu);text-transform:uppercase}
label{display:block;margin:10px 0 3px;color:var(--mu);font-size:13px}
input{width:100%;font:inherit;padding:10px;border-radius:8px;border:1px solid var(--ln);background:var(--bg);color:var(--tx)}
button{font:inherit;padding:11px 16px;border-radius:10px;border:1px solid var(--ln);background:var(--card);color:var(--tx)}
button.p{background:var(--ac);border-color:var(--ac);color:#fff;width:100%;margin-top:14px}
.net{display:flex;justify-content:space-between;width:100%;text-align:left;margin-top:6px}
.mu{color:var(--mu);font-size:13px}.ok{color:var(--ok);font-weight:700}.er{color:var(--er);font-weight:700}
</style></head><body><main>
<h1>Rover WiFi setup</h1>
<section class="card"><h2>1. Pick your network</h2><div id="nets" class="mu">Scanning...</div>
 <button id="rescan" style="margin-top:8px">Scan again</button></section>
<section class="card"><h2>2. Details</h2>
 <label for="ssid">Network name</label><input id="ssid" autocapitalize="none" autocomplete="off">
 <label for="pass">WiFi password (leave empty for an open network)</label><input id="pass" type="password" autocomplete="off">
 <label for="token">GitHub token (optional: needed for uploads and private repos)</label><input id="token" type="password" autocomplete="off" placeholder="fine-grained token, this repo only">
 <label for="code">Rover access code (optional: 8 to 32 characters, replaces the one on the label)</label><input id="code" autocomplete="off">
 <button class="p" id="go">Test connection and save</button>
 <div id="res" style="margin-top:10px"></div>
 <div class="mu" style="margin-top:8px">The rover tests the connection first and saves it only if it works. Your phone may briefly drop off the rover's network while it tests: reconnect to it and the result appears here. Credentials are stored only in the rover's flash.</div></section>
<section class="card"><h2>Rover</h2><div class="mu" id="info"></div><div style="margin-top:8px"><a href="/">Back to the control page</a></div></section>
</main><script>
const $=id=>document.getElementById(id);
let key=localStorage.getItem('rk')||new URLSearchParams(location.search).get('k')||'';
const q=(p,a)=>p+'?'+new URLSearchParams({...(a||{}),k:key});
async function j(u,o){try{const r=await fetch(u,o);return await r.json()}catch(e){return null}}
async function scan(){const d=await j(q('/api/setup/scan'));const el=$('nets');el.textContent='';
 if(!d){el.textContent='Cannot reach the rover. Reconnect to its WiFi.';return}
 if(d.scanning){el.textContent='Scanning...';setTimeout(scan,1500);return}
 if(!d.networks.length){el.textContent='No networks found.';return}
 d.networks.sort((a,b)=>b.rssi-a.rssi).forEach(n=>{const b=document.createElement('button');b.className='net';
  const a=document.createElement('span');a.textContent=n.ssid;const c=document.createElement('span');c.className='mu';c.textContent=(n.open?'open  ':'')+n.rssi+' dBm';
  b.append(a,c);b.onclick=()=>{$('ssid').value=n.ssid;$('pass').focus()};el.append(b)})}
$('rescan').onclick=()=>{$('nets').textContent='Scanning...';scan()};scan();
$('go').onclick=async()=>{const res=$('res');res.className='mu';res.textContent='Testing... (up to 20 s)';
 const body=new URLSearchParams({ssid:$('ssid').value,pass:$('pass').value,token:$('token').value,code:$('code').value,k:key});
 const r=await j('/api/setup/connect',{method:'POST',body});
 if(r&&!r.ok){res.className='er';res.textContent=r.msg;return}poll()};
async function poll(){const s=await j(q('/api/setup/status'));const res=$('res');
 if(!s){res.className='mu';res.textContent='Waiting for the rover... reconnect your phone to the rover network if it dropped.';setTimeout(poll,1500);return}
 $('info').textContent=s.id+'  |  state: '+s.net+(s.ip?'  |  IP '+s.ip:'')+(s.host?'  |  '+s.host:'')+(s.hasToken?'  |  GitHub token set':'');
 if(s.test==='running'){res.className='mu';res.textContent='Testing...';setTimeout(poll,1000)}
 else if(s.test==='ok'){res.className='ok';res.textContent=s.msg+(s.ip?'  Rover IP: '+s.ip:'')+(s.host?'  ('+s.host+')':'')+'  Reconnect your phone to your normal WiFi.'}
 else if(s.test==='fail'){res.className='er';res.textContent=s.msg}}
poll();
</script></body></html>)HTML";
