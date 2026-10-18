#pragma once
// The web pages, served from flash. Plain HTML/CSS/JS, no external files or libraries, so they work
// on the rover's own network with no internet. All server-provided text is inserted with
// textContent (never innerHTML), so a hostile network name or path name cannot inject script.
#include <pgmspace.h>

static const char UI_CONTROL_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Rover</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--tx:#14181f;--mu:#667085;--ln:#dde1e7;--ac:#1d5fd1;--ok:#157f3b;--wa:#b26a00;--er:#c0261d}
@media(prefers-color-scheme:dark){:root{--bg:#12151a;--card:#1b2027;--tx:#e8ebf0;--mu:#98a2b3;--ln:#2b323c;--ac:#5b9cff;--ok:#4cc27a;--wa:#e0a43a;--er:#ff7b72}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--tx);font:15px/1.4 system-ui,sans-serif;padding-bottom:90px}
header{display:flex;gap:10px;align-items:center;padding:10px 14px;background:var(--card);border-bottom:1px solid var(--ln);position:sticky;top:0;z-index:5}
header b{font-size:17px}.mu{color:var(--mu);font-size:13px}
.badge{margin-left:auto;padding:3px 10px;border-radius:99px;font-weight:700;font-size:13px;background:var(--ln)}
.badge.MANUAL{background:var(--ok);color:#fff}.badge.AUTO{background:var(--ac);color:#fff}
main{max-width:640px;margin:0 auto;padding:12px;display:grid;gap:12px}
.card{background:var(--card);border:1px solid var(--ln);border-radius:12px;padding:12px 14px}
h2{margin:0 0 8px;font-size:14px;color:var(--mu);text-transform:uppercase;letter-spacing:.04em}
.grid{display:grid;grid-template-columns:repeat(2,1fr);gap:4px 14px}.grid span:nth-child(odd){color:var(--mu)}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-top:8px}
button{font:inherit;padding:10px 14px;border-radius:10px;border:1px solid var(--ln);background:var(--card);color:var(--tx)}
button.p{background:var(--ac);border-color:var(--ac);color:#fff}button:disabled{opacity:.45}
input,select{font:inherit;padding:9px;border-radius:8px;border:1px solid var(--ln);background:var(--bg);color:var(--tx);min-width:0}
.banner{padding:9px 12px;border-radius:10px;font-weight:600;border:1px solid}
.banner.er{color:var(--er);border-color:var(--er)}.banner.wa{color:var(--wa);border-color:var(--wa)}.banner.ok{color:var(--ok);border-color:var(--ok)}
#pad{width:230px;height:230px;margin:6px auto;border-radius:50%;background:radial-gradient(var(--bg),var(--ln));border:2px solid var(--ln);position:relative;touch-action:none;user-select:none}
#puck{width:70px;height:70px;border-radius:50%;background:var(--ac);position:absolute;left:80px;top:80px;opacity:.9}
#pad.off{opacity:.35}
#stop{position:fixed;left:0;right:0;bottom:0;height:76px;background:var(--er);color:#fff;font-size:22px;font-weight:800;border:0;border-radius:0;letter-spacing:.08em;z-index:9}
.hide{display:none}
</style></head><body>
<header><b id="rid">Rover</b><span class="mu" id="ver"></span><span class="badge" id="badge">...</span></header>
<main>
 <div id="msgs"></div>
 <section class="card" id="gate" hidden><h2>Access code</h2>
  <div class="row"><input id="code" placeholder="code from the rover's label" autocapitalize="none"><button class="p" id="codeBtn">Unlock</button></div></section>

 <section class="card"><h2>Status</h2><div class="grid" id="stat"></div></section>

 <section class="card"><h2>Drive</h2>
  <div class="row"><button class="p" id="arm">ARM</button><button id="disarm">Disarm</button><span class="mu" id="armNote"></span></div>
  <div id="pad" class="off"><div id="puck"></div></div>
  <div class="mu" style="text-align:center" id="padNote">Arm to drive. The rover stops if commands stop for half a second.</div></section>

 <section class="card"><h2>Record a path</h2>
  <div class="row"><input id="pname" placeholder="name (optional)" maxlength="32"><button id="recStart">Start</button><button id="recStop">Stop &amp; save</button></div>
  <div class="mu" id="recNote"></div></section>

 <section class="card"><h2>Follow a path (auto)</h2>
  <div class="row"><select id="plist"></select><button id="pload">Load</button><button id="prefresh">Refresh</button></div>
  <div class="row"><button class="p" id="auto">Start auto</button><span class="mu" id="autoNote"></span></div>
  <div class="mu">Place the rover at the start of the path first. If heading is not aligned, drive forward about 5 m in manual.</div></section>

 <section class="card"><h2>System</h2>
  <div class="row"><button id="ota">Check for update</button><button class="p" id="otaInstall" disabled>Install update</button></div>
  <div class="row"><button id="sync">Sync config</button><button id="cal">Calibrate IMU</button></div>
  <div class="row"><select id="bw"><option value="0">FL</option><option value="1">FR</option><option value="2">RL</option><option value="3">RR</option></select>
   <button id="bench">Spin wheel 1 s (wheels OFF the ground)</button></div>
  <div class="row"><a id="wifi" href="#">Change WiFi / token</a></div>
  <div class="mu" id="sysNote"></div></section>
</main>
<button id="stop">STOP</button>
<script>
const $=id=>document.getElementById(id);
let key=localStorage.getItem('rk')||'',st=null,lastOk=0,cmd={t:0,s:0},armed=false,shownUpd=false;
const q=(p,a)=>'/api/'+p+'?'+new URLSearchParams({...(a||{}),k:key});
async function api(p,a,m){try{const r=await fetch(q(p,a),{method:m||'POST',cache:'no-store'});
  if(r.status===401){$('gate').hidden=false;return {ok:false,msg:'access code needed'}}
  if(r.status===204)return {ok:true};
  return await r.json()}catch(e){return {ok:false,msg:'no connection'}}}
function say(t,c){const d=document.createElement('div');d.className='banner '+(c||'wa');d.textContent=t;$('msgs').prepend(d);setTimeout(()=>d.remove(),6000)}
function put(el,rows){el.textContent='';for(const [k,v] of rows){const a=document.createElement('span');a.textContent=k;const b=document.createElement('span');b.textContent=v;el.append(a,b)}}
$('codeBtn').onclick=()=>{key=$('code').value.trim();localStorage.setItem('rk',key);$('gate').hidden=true;poll()};
$('wifi').onclick=e=>{e.preventDefault();location.href='/setup?k='+encodeURIComponent(key)};

function render(s){st=s;armed=s.mode==='MANUAL';
 $('rid').textContent=s.id;$('ver').textContent='v'+s.ver;const b=$('badge');b.textContent=s.mode;b.className='badge '+s.mode;
 const g=s.gps,i=s.imu,r=[];
 r.push(['GPS',g.ok?(g.fix?`fix  ${g.sats} sats  HDOP ${g.hdop}`:'searching... '+g.sats+' sats'):'no data']);
 r.push(['Position',g.fix?g.lat+', '+g.lon:'-']);
 r.push(['Speed',g.spd+' m/s  ('+g.hz+' Hz)']);
 r.push(['Heading',i.ok?(i.yawOk?i.yaw+'°':'not aligned'):'IMU missing']);
 r.push(['Tilt / gyro noise',i.ok?i.tilt+'°  /  '+i.noise+' dps':'-']);
 r.push(['Battery',s.batt?s.batt+' V':'not measured']);
 r.push(['Network',s.net+'  '+s.ip+(s.host?'  ('+s.host+')':'')]);
 r.push(['Clock',s.clock?'set':'not set yet']);
 r.push(['Log',(s.log.bytes/1024).toFixed(1)+' KB pending, '+s.log.files+' queued'+(s.log.tok?'':'  (no GitHub token)')]);
 r.push(['Update',(s.ota.msg||s.ota.state)+(s.ota.auto?'  (installs automatically)':'  (manual install)')]);
 $('otaInstall').disabled=!(s.ota.avail&&s.mode==='DISARMED');
 if(s.mode==='AUTO'||s.follow.state==='FINISHED')r.push(['Auto',`${s.follow.state}  wp ${s.follow.wp}/${s.follow.n}  XTE ${s.follow.xte} m`]);
 if(s.mode==='DISARMED'&&s.reason)r.push(['Last stop',s.reason]);
 put($('stat'),r);
 $('arm').disabled=s.mode!=='DISARMED';$('disarm').disabled=s.mode==='DISARMED';
 $('pad').classList.toggle('off',!armed);
 $('padNote').textContent=armed?'Drag the circle. Release to stop.':'Arm to drive.';
 $('recStart').disabled=!armed||s.rec;$('recStop').disabled=!s.rec;
 $('recNote').textContent=s.rec?`Recording... ${s.recN} points. Drive the route, then Stop & save.`:'Arm, press Start, and drive the route.';
 $('auto').disabled=s.mode==='AUTO'||!s.path;$('autoNote').textContent=s.path?'loaded: '+s.path:'no path loaded';
 $('sysNote').textContent=s.log.err?('Upload: '+s.log.err):'';
 if(s.updatedFrom&&!shownUpd){shownUpd=true;say(`Updated from v${s.updatedFrom} to v${s.ver}. The rover is disarmed.`,'ok')}
}
async function poll(){const s=await api('status',null,'GET');if(s&&s.id){lastOk=Date.now();render(s)}
 const lost=Date.now()-lastOk>1500;let l=document.getElementById('lost');
 if(lost&&!l){l=document.createElement('div');l.id='lost';l.className='banner er';l.textContent='LINK LOST. The rover stops by itself.';$('msgs').prepend(l)}
 if(!lost&&l)l.remove();setTimeout(poll,400)}
poll();
async function loadPaths(){const l=await api('paths',null,'GET');const sel=$('plist');sel.textContent='';
 for(const p of (Array.isArray(l)?l:[])){const o=document.createElement('option');o.value=p.name;o.textContent=p.name+(p.pending?'  (uploading)':'');sel.append(o)}}
loadPaths();$('prefresh').onclick=loadPaths;

// ---- joystick: sends the command 10x per second; silence = stop on the rover ----
const pad=$('pad'),puck=$('puck');let drag=false;
function setCmd(x,y){const n=Math.hypot(x,y);if(n>1){x/=n;y/=n}const dz=0.12;const f=v=>Math.abs(v)<dz?0:v;cmd={t:-f(y),s:f(x)};
 puck.style.left=(80+x*80)+'px';puck.style.top=(80+y*80)+'px'}
function ptr(e){const r=pad.getBoundingClientRect();setCmd((e.clientX-r.left-r.width/2)/(r.width/2-30),(e.clientY-r.top-r.height/2)/(r.height/2-30))}
pad.onpointerdown=e=>{if(!armed)return;drag=true;pad.setPointerCapture(e.pointerId);ptr(e)};
pad.onpointermove=e=>{if(drag)ptr(e)};
const rel=()=>{drag=false;setCmd(0,0)};pad.onpointerup=rel;pad.onpointercancel=rel;
setInterval(()=>{if(armed&&Date.now()-lastOk<1500)fetch(q('drive',{t:cmd.t.toFixed(2),s:cmd.s.toFixed(2)}),{method:'POST',keepalive:true}).catch(()=>{})},100);
document.addEventListener('visibilitychange',()=>{if(document.hidden){setCmd(0,0)}});

$('arm').onclick=async()=>{const r=await api('arm');say(r.msg,r.ok?'ok':'er')};
$('disarm').onclick=async()=>{const r=await api('disarm');say(r.msg,'wa')};
const doStop=async()=>{setCmd(0,0);for(let i=0;i<3;i++){const r=await api('stop');if(r.ok)break}};
$('stop').onclick=doStop;document.addEventListener('keydown',e=>{if(e.code==='Space'||e.code==='Escape')doStop()});
$('recStart').onclick=async()=>{const r=await api('rec/start');say(r.msg,r.ok?'ok':'er')};
$('recStop').onclick=async()=>{const r=await api('rec/stop',{name:$('pname').value.trim()});say(r.msg,r.ok?'ok':'er');loadPaths()};
$('pload').onclick=async()=>{say('loading...','wa');const r=await api('path/load',{name:$('plist').value});say(r.msg,r.ok?'ok':'er')};
$('auto').onclick=async()=>{if(!confirm('Start auto mode? Keep clear of the rover. The STOP button always works.'))return;const r=await api('auto/start');say(r.msg,r.ok?'ok':'er')};
$('ota').onclick=async()=>{say('checking...','wa');const r=await api('ota/check');say(r.msg,r.ok?'ok':'er')};
$('otaInstall').onclick=async()=>{if(!confirm('Install the update now? The rover restarts and stays disarmed.'))return;say('installing...','wa');const r=await api('ota/install');say(r.msg,r.ok?'ok':'er')};
$('sync').onclick=async()=>{const r=await api('config/sync');say(r.msg,r.ok?'ok':'er')};
$('cal').onclick=async()=>{const r=await api('imu/calibrate');say(r.msg,r.ok?'ok':'er')};
$('bench').onclick=async()=>{const r=await api('bench',{w:$('bw').value,v:'0.3'});say(r.msg,r.ok?'ok':'er')};
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
