#pragma once
#include <Arduino.h>
const char WEBPAGE[] PROGMEM = R"HTMLPAGE(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no"/>
<title>Mars Rover Bot Control</title>
<style>
@import url('https://fonts.googleapis.com/css2?family=Chakra+Petch:wght@500;600;700&family=Nunito:wght@600;700;800&family=JetBrains+Mono:wght@500;700&display=swap');

:root{
  --disp:'Chakra Petch',system-ui,sans-serif;
  --body:'Nunito',system-ui,sans-serif;
  --mono:'JetBrains Mono',ui-monospace,monospace;
  --bg1:#171235; --bg2:#20183f;
  --surface:#271f4d; --surface2:#2f2660; --raise:#3a2f70;
  --edge:rgba(255,255,255,.07);
  --amber:#FFC24B; --amber-soft:rgba(255,194,75,.08);
  --coral:#FF5D73; --coral-dk:#c93049;
  --mint:#38E0AE; --ink:#F1ECFF; --muted:#9E93CE;
}
*{box-sizing:border-box}
html,body{margin:0}
body{
  font-family:var(--body); color:var(--ink); min-height:100vh;
  background:
    radial-gradient(900px 500px at 15% -5%, #2a1f57 0%, transparent 60%),
    radial-gradient(700px 500px at 100% 0%, #3a1f4e 0%, transparent 55%),
    linear-gradient(160deg,var(--bg1),var(--bg2));
  -webkit-font-smoothing:antialiased; padding:16px;
}
h1,h2,h3{font-family:var(--disp);margin:0;letter-spacing:.4px}
.wrap{max-width:1080px;margin:0 auto}
.hide{display:none!important}

/* header */
header{display:flex;align-items:center;justify-content:space-between;gap:14px;flex-wrap:wrap;margin-bottom:16px}
.brand{display:flex;align-items:center;gap:12px}
.logo{width:46px;height:46px;border-radius:13px;display:grid;place-items:center;font-size:24px;
  background:linear-gradient(160deg,var(--coral),#ff8aa0);box-shadow:0 6px 18px -6px var(--coral)}
.brand h1{font-size:1.35rem;font-weight:700}
.brand p{margin:1px 0 0;font-size:.76rem;color:var(--muted);font-weight:800;letter-spacing:1.5px;text-transform:uppercase}
.conn{display:flex;align-items:center;gap:8px;background:var(--surface);border:1px solid var(--edge);
  border-radius:14px;padding:6px 8px 6px 12px}
.conn input{border:0;background:transparent;color:var(--ink);font-family:var(--mono);font-weight:700;
  font-size:.9rem;outline:none;width:130px}
.conn input::placeholder{color:var(--muted)}
.status{display:flex;align-items:center;gap:7px;font-weight:800;font-size:.82rem;padding:0 6px}
.sdot{width:10px;height:10px;border-radius:50%;background:var(--muted)}
.sdot.on{background:var(--mint);box-shadow:0 0 0 0 var(--mint);animation:pulse 1.6s infinite}
.sdot.warn{background:var(--amber)}
@keyframes pulse{0%{box-shadow:0 0 0 0 rgba(56,224,174,.5)}70%{box-shadow:0 0 0 8px rgba(56,224,174,0)}}
.btn{font-family:var(--disp);font-weight:600;border:0;border-radius:11px;cursor:pointer;
  padding:9px 15px;font-size:.86rem;background:var(--coral);color:#fff;transition:transform .08s,filter .15s}
.btn:active{transform:scale(.95)}
.btn.ghost{background:var(--surface2);color:var(--ink);border:1px solid var(--edge)}
.btn.mint{background:var(--mint);color:#08281f}

/* layout */
.top{display:grid;grid-template-columns:minmax(0,380px) 1fr;gap:16px;margin-bottom:16px}
.card{background:linear-gradient(180deg,var(--surface),var(--surface2));border:1px solid var(--edge);
  border-radius:22px;padding:18px}
.card h2{font-size:1rem;font-weight:700;display:flex;align-items:center;gap:8px;margin-bottom:4px}
.card .hint{color:var(--muted);font-size:.8rem;font-weight:700;margin-bottom:14px}

/* LED PANEL (hero) */
.panel-card{display:flex;flex-direction:column;align-items:center}
.bezel{width:100%;max-width:340px;padding:16px;border-radius:22px;
  background:linear-gradient(160deg,#0c0a1c,#141026);
  box-shadow:inset 0 3px 14px rgba(0,0,0,.7), inset 0 -2px 0 rgba(255,255,255,.04), 0 0 40px -10px var(--amber);
  border:1px solid rgba(255,194,75,.14);animation:breathe 4s ease-in-out infinite}
@keyframes breathe{50%{box-shadow:inset 0 3px 14px rgba(0,0,0,.7),inset 0 -2px 0 rgba(255,255,255,.04),0 0 60px -6px var(--amber)}}
#screen{width:100%;height:auto;display:block;aspect-ratio:1;border-radius:8px;touch-action:none}
.caption{margin-top:14px;font-family:var(--disp);font-weight:600;font-size:1.05rem;text-transform:capitalize;
  color:var(--amber);text-align:center;min-height:26px}
.caption small{display:block;color:var(--muted);font-family:var(--body);font-weight:800;
  font-size:.72rem;text-transform:none;letter-spacing:.5px;margin-top:2px}

/* right column */
.modebar{display:flex;background:var(--bg1);border:1px solid var(--edge);border-radius:14px;padding:5px;margin-bottom:16px}
.modebar button{flex:1;border:0;background:transparent;color:var(--muted);font-family:var(--disp);
  font-weight:600;font-size:.92rem;padding:11px;border-radius:10px;cursor:pointer;transition:.2s}
.modebar button.on{background:var(--raise);color:var(--ink);box-shadow:0 3px 10px rgba(0,0,0,.3)}

.textmode label{font-weight:800;font-size:.82rem;color:var(--muted);display:block;margin-bottom:7px}
.textmode .row{display:flex;gap:8px}
.textmode input[type=text]{flex:1;background:var(--bg1);border:1px solid var(--edge);border-radius:12px;
  padding:12px 14px;color:var(--ink);font-family:var(--mono);font-weight:700;font-size:1rem;outline:none}
.presets{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}
.preset{background:var(--bg1);border:1px solid var(--edge);border-radius:10px;padding:8px 12px;
  font-weight:800;font-size:.82rem;cursor:pointer;color:var(--ink)}

/* DRIVE */
.drive{display:grid;grid-template-columns:auto 1fr;gap:26px;align-items:center}
.dpad{display:grid;grid-template-columns:repeat(3,66px);grid-template-rows:repeat(3,66px);gap:9px}
.pad-btn{border:0;border-radius:17px;color:var(--ink);font-size:1.5rem;cursor:pointer;user-select:none;
  background:linear-gradient(180deg,var(--raise),#2b2258);
  box-shadow:0 6px 0 #1a1440, inset 0 1px 0 rgba(255,255,255,.09);
  transition:transform .06s,box-shadow .06s;touch-action:none}
.pad-btn:active,.pad-btn.active{transform:translateY(5px);box-shadow:0 1px 0 #1a1440;
  outline:2px solid var(--amber);outline-offset:-2px}
.pad-btn.u{grid-column:2;grid-row:1}.pad-btn.l{grid-column:1;grid-row:2}
.pad-btn.s{grid-column:2;grid-row:2}.pad-btn.r{grid-column:3;grid-row:2}.pad-btn.d{grid-column:2;grid-row:3}
.pad-btn.s{background:linear-gradient(180deg,var(--coral),var(--coral-dk));box-shadow:0 6px 0 #7d1a2b;font-size:1.1rem;font-family:var(--disp);font-weight:700}
.pad-btn.s:active{box-shadow:0 1px 0 #7d1a2b;outline:none}

.drive-side .sp label{display:flex;justify-content:space-between;font-weight:800;font-size:.86rem;margin-bottom:8px}
.drive-side .sp b{font-family:var(--mono);color:var(--amber)}
input[type=range]{width:100%;-webkit-appearance:none;height:9px;border-radius:9px;
  background:linear-gradient(90deg,var(--amber),#5a4a25);outline:none}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:26px;height:26px;border-radius:50%;
  background:#fff;border:5px solid var(--amber);cursor:pointer;box-shadow:0 2px 8px rgba(0,0,0,.4)}
.aux{display:flex;gap:10px;margin-top:18px;flex-wrap:wrap}
.aux .btn{flex:1;min-width:120px}
.kbd{margin-top:16px;font-size:.78rem;color:var(--muted);font-weight:700;line-height:1.6}
.kbd kbd{font-family:var(--mono);background:var(--bg1);border:1px solid var(--edge);border-radius:6px;
  padding:2px 7px;font-size:.75rem;color:var(--ink)}

/* BLOCK PROGRAMMER */
.block-toolbar,.program-actions{display:grid;grid-template-columns:repeat(5,1fr);gap:8px;margin-top:12px}
.block-toolbar .btn,.program-actions .btn{min-width:0;padding:10px 6px}
.program-list{display:grid;gap:8px;margin-top:14px}
.code-block{display:grid;grid-template-columns:32px minmax(100px,1fr) 92px 110px 38px;gap:8px;
  align-items:center;background:var(--bg1);border:1px solid var(--edge);border-radius:12px;padding:8px}
.step-number{width:28px;height:28px;border-radius:50%;display:grid;place-items:center;background:var(--coral);
  color:#fff;font-family:var(--mono);font-weight:700}
.block-name{font-family:var(--disp);font-weight:600;text-align:left;text-transform:capitalize}
.code-block select,.code-block input{width:100%;height:38px;border:1px solid var(--edge);border-radius:8px;
  background:var(--surface2);color:var(--ink);padding:0 8px;font-family:var(--mono)}
.remove-block{width:38px;height:38px;border:0;border-radius:8px;background:var(--coral);color:#fff;font-weight:800;cursor:pointer}
.empty-program{color:var(--muted);font-weight:700;text-align:center;padding:14px}

/* CONSOLE */
.console{background:#0e0b20;border-radius:14px;border:1px solid var(--edge);padding:12px 14px;
  font-family:var(--mono);font-size:.8rem;height:150px;overflow-y:auto;line-height:1.7}
.logline{display:flex;gap:10px;white-space:nowrap}
.logline .ts{color:#5b5187}.logline .ar{color:var(--muted)}
.t-move .msg{color:var(--coral)} .t-face .msg,.t-text .msg{color:var(--amber)}
.t-mode .msg,.t-aux .msg{color:var(--mint)} .t-system .msg{color:var(--muted)}

.demo-note{background:rgba(255,194,75,.09);border:1px solid rgba(255,194,75,.25);border-radius:12px;
  padding:10px 14px;font-weight:700;font-size:.82rem;color:var(--amber);margin-bottom:16px}

@media(max-width:840px){
  .top{grid-template-columns:1fr}
  .panel-card{order:-1}
  .drive{grid-template-columns:1fr;gap:20px;justify-items:center}
  .drive-side{width:100%}
  .block-toolbar{grid-template-columns:repeat(2,1fr)}
  .code-block{grid-template-columns:28px 1fr 76px 86px 34px;gap:5px;font-size:.8rem}
}
@media(prefers-reduced-motion:reduce){*{animation:none!important}}
</style>
</head>
<body>
<div class="wrap">

  <header>
    <div class="brand">
      <div class="logo">🤖</div>
      <div><h1>Mars Rover Bot</h1><p>ESP32 · WiFi drive + text</p></div>
    </div>
    <div style="display:flex;gap:8px;align-items:center;flex-wrap:wrap">
      <div class="conn">
        <span style="color:var(--muted);font-weight:800;font-size:.78rem">http://</span>
        <input id="ip" placeholder="192.168.4.1" value="192.168.4.1"/>
      </div>
      <button class="btn" id="connectBtn">Connect</button>
      <div class="status"><span class="sdot" id="sdot"></span><span id="sTxt">Offline</span></div>
    </div>
  </header>

  <div class="demo-note" id="demoNote">Demo mode — no car connected. Everything works on screen and the commands that
    <em>would</em> be sent appear in the console below. Enter your car's IP and hit Connect to drive the real thing.</div>

  <!-- TOP: display panel + text controls -->
  <div class="top">
    <div class="card panel-card">
      <div class="bezel"><canvas id="screen" width="384" height="384"></canvas></div>
      <div class="caption" id="caption">HELLO STEM<small>What the car's pixel display is showing</small></div>
    </div>

    <div class="card">
      <h2>🖥️ Pixel display</h2>
      <div class="hint">Text mode — type a message and send it to the car</div>
      <div id="textMode" class="textmode">
        <label>Message to scroll across the display</label>
        <div class="row">
          <input type="text" id="textInput" maxlength="40" value="HELLO STEM"/>
          <button class="btn mint" id="scrollBtn">Scroll it</button>
        </div>
        <div class="presets" id="presets">
          <span class="preset">HELLO 👋</span><span class="preset">GO GO GO</span>
          <span class="preset">STEM ⚡</span><span class="preset">MARS ROVER</span><span class="preset">❤ ❤ ❤</span>
        </div>
      </div>
    </div>
  </div>

  <!-- DRIVE -->
  <div class="card" style="margin-bottom:16px">
    <h2>🎮 Drive</h2>
    <div class="hint">Hold a button (or arrow keys / WASD) to move — release to stop</div>
    <div class="drive">
      <div class="dpad">
        <button class="pad-btn u" data-dir="forward">▲</button>
        <button class="pad-btn l" data-dir="left">◀</button>
        <button class="pad-btn s" data-dir="stop">STOP</button>
        <button class="pad-btn r" data-dir="right">▶</button>
        <button class="pad-btn d" data-dir="back">▼</button>
      </div>
      <div class="drive-side">
        <div class="sp">
          <label>Speed <b><span id="spVal">200</span> / 255</b></label>
          <input type="range" id="speed" min="0" max="255" value="200"/>
        </div>
        <div class="kbd">
          <kbd>W</kbd><kbd>A</kbd><kbd>S</kbd><kbd>D</kbd> or <kbd>↑</kbd><kbd>↓</kbd><kbd>←</kbd><kbd>→</kbd> to drive ·
          <kbd>Space</kbd> to stop
        </div>
      </div>
    </div>
  </div>

  <!-- BLOCK PROGRAMMER -->
  <div class="card" style="margin-bottom:16px">
    <h2>🧩 Block code</h2>
    <div class="hint">Build a movement sequence, then send it to the car</div>
    <div class="block-toolbar">
      <button class="btn" data-add-block="forward">Forward</button>
      <button class="btn" data-add-block="backward">Backward</button>
      <button class="btn" data-add-block="left">Left</button>
      <button class="btn" data-add-block="right">Right</button>
      <button class="btn ghost" data-add-block="stop">Stop</button>
    </div>
    <div class="program-list" id="programList"></div>
    <div class="program-actions" style="grid-template-columns:repeat(3,1fr)">
      <button class="btn mint" id="runProgramBtn">Run program</button>
      <button class="btn ghost" id="clearProgramBtn">Clear blocks</button>
      <button class="btn" id="stopProgramBtn">Emergency stop</button>
    </div>
    <div class="hint" id="programStatus">Up to 12 blocks · speed 0–255 · duration in seconds</div>
  </div>

  <!-- CONSOLE -->
  <div class="card">
    <h2>📟 Command console</h2>
    <div class="hint">The exact messages sent to the ESP32 (this is your firmware's API)</div>
    <div class="console" id="console"></div>
  </div>
</div>

<script>
/* ============ DISPLAY GRID ENGINE ============ */
const N=16, SZ=384, cell=SZ/N;
const cv=document.getElementById('screen'), ctx=cv.getContext('2d');
const blank=()=>Array.from({length:N},()=>new Array(N).fill(0));

/* ============ DISPLAY STATE + TEXT RENDER ============ */
const D={mode:'text',grid:blank(),cols:null,off:0,last:0};

function textCols(text){
  const c=document.createElement('canvas');let x=c.getContext('2d');
  x.font='bold 13px "JetBrains Mono",monospace';
  const w=Math.max(16,Math.ceil(x.measureText(text).width)+2);
  c.width=w;c.height=N;x=c.getContext('2d');
  x.font='bold 13px "JetBrains Mono",monospace';x.textBaseline='middle';x.fillStyle='#fff';
  x.fillText(text,1,8);
  const d=x.getImageData(0,0,w,N).data,cols=[];
  for(let cx=0;cx<w;cx++){const col=[];for(let y=0;y<N;y++)col.push(d[(y*w+cx)*4+3]>90?1:0);cols.push(col);}
  for(let i=0;i<N;i++)cols.push(new Array(N).fill(0));
  return cols;
}

function currentGrid(t){
  if(D.cols){
    if(t-D.last>85){D.off=(D.off+1)%D.cols.length;D.last=t;}
    const g=blank();
    for(let cx=0;cx<N;cx++){
      const src=D.cols[(D.off+cx)%D.cols.length];
      for(let y=0;y<N;y++)if(src[y])g[y][cx]=1;
    }
    return g;
  }
  return D.grid;
}

function render(t){
  const g=currentGrid(t);
  ctx.clearRect(0,0,SZ,SZ);
  for(let r=0;r<N;r++)for(let c=0;c<N;c++){
    const x=c*cell+cell/2,y=r*cell+cell/2;
    if(g[r][c]){
      ctx.shadowColor='#FFC24B';ctx.shadowBlur=13;ctx.fillStyle='#FFD27A';
      ctx.beginPath();ctx.arc(x,y,cell*0.34,0,7);ctx.fill();
    } else {
      ctx.shadowBlur=0;ctx.fillStyle='rgba(255,194,75,.06)';
      ctx.beginPath();ctx.arc(x,y,cell*0.14,0,7);ctx.fill();
    }
  }
  requestAnimationFrame(render);
}
requestAnimationFrame(render);

/* ============ COMMS ============ */
const $=s=>document.querySelector(s);
const Comm={base:'',connected:false,
  async connect(ip){
    if(!ip)return;
    this.base='http://'+ip.replace(/^https?:\/\//,'').replace(/\/$/,'');
    setStatus('warn','Connecting…');
    const ctl=new AbortController(),timer=setTimeout(()=>ctl.abort(),2500);
    try{
      const r=await fetch(this.base+'/status',{signal:ctl.signal,cache:'no-store'});
      if(!r.ok)throw new Error('HTTP '+r.status);
      this.connected=true;setStatus('on','Connected');$('#demoNote').classList.add('hide');
      log('system','link established → '+ip);
    }catch(e){
      this.connected=false;setStatus('off','Offline');$('#demoNote').classList.remove('hide');
      log('system','could not reach car — staying in demo mode');
    }finally{clearTimeout(timer);}
  },
  send(obj,line){
    log(obj.cmd,line);
    if(!this.connected)return;
    let path='';
    if(obj.cmd==='move')path='/api/move?dir='+encodeURIComponent(obj.dir)+'&speed='+obj.speed;
    else if(obj.cmd==='text')path='/api/text?value='+encodeURIComponent(obj.text);
    if(path)fetch(this.base+path,{cache:'no-store'}).then(r=>{if(!r.ok)throw new Error(r.status);})
      .catch(()=>{this.connected=false;setStatus('off','Offline');$('#demoNote').classList.remove('hide');});
  }
};
function setStatus(s,txt){const d=$('#sdot');d.className='sdot'+(s==='on'?' on':s==='warn'?' warn':'');$('#sTxt').textContent=txt;}
function log(type,msg){
  const el=document.createElement('div');el.className='logline t-'+type;
  el.innerHTML=`<span class="ts">${new Date().toLocaleTimeString('en-GB')}</span><span class="ar">▸</span><span class="msg">${msg}</span>`;
  const con=$('#console');con.appendChild(el);con.scrollTop=con.scrollHeight;
  while(con.children.length>80)con.removeChild(con.firstChild);
}

/* ============ DRIVE ============ */
let speed=200,curDir=null;
function padEl(dir){return document.querySelector(`.pad-btn[data-dir="${dir}"]`);}
function move(dir){
  curDir=dir;
  document.querySelectorAll('.pad-btn').forEach(b=>b.classList.remove('active'));
  padEl(dir)?.classList.add('active');
  Comm.send({cmd:'move',dir,speed},`MOVE  ${dir.padEnd(8)} speed ${speed}`);
}
function stopMove(){
  curDir=null;document.querySelectorAll('.pad-btn').forEach(b=>b.classList.remove('active'));
  Comm.send({cmd:'move',dir:'stop',speed:0},'MOVE  stop');
}
document.querySelectorAll('.pad-btn').forEach(b=>{
  const dir=b.dataset.dir;
  if(dir==='stop'){b.addEventListener('pointerdown',e=>{e.preventDefault();stopMove();});return;}
  const start=e=>{e.preventDefault();move(dir);};
  const end=e=>{e.preventDefault();if(curDir===dir)stopMove();};
  b.addEventListener('pointerdown',start);
  b.addEventListener('pointerup',end);b.addEventListener('pointerleave',end);b.addEventListener('pointercancel',end);
});
const keymap={ArrowUp:'forward',KeyW:'forward',ArrowDown:'back',KeyS:'back',ArrowLeft:'left',KeyA:'left',ArrowRight:'right',KeyD:'right'};
window.addEventListener('keydown',e=>{
  if(e.target.tagName==='INPUT')return;
  if(e.code==='Space'){e.preventDefault();stopMove();return;}
  const d=keymap[e.code];if(d){e.preventDefault();if(curDir!==d)move(d);}
});
window.addEventListener('keyup',e=>{const d=keymap[e.code];if(d&&curDir===d){e.preventDefault();stopMove();}});

$('#speed').addEventListener('input',e=>{speed=+e.target.value;$('#spVal').textContent=speed;
  if(curDir)Comm.send({cmd:'move',dir:curDir,speed},`MOVE  ${curDir.padEnd(8)} speed ${speed}`);});

/* ============ BLOCK PROGRAMMER ============ */
const programSteps=[];
const blockNames={forward:'Move forward',backward:'Move backward',left:'Turn left',right:'Turn right',stop:'Stop'};
function renderProgram(){
  const list=$('#programList');list.innerHTML='';
  if(!programSteps.length){list.innerHTML='<div class="empty-program">No blocks yet</div>';return;}
  programSteps.forEach((step,index)=>{
    const row=document.createElement('div');row.className='code-block';
    row.innerHTML=`<div class="step-number">${index+1}</div><div class="block-name">${blockNames[step.type]}</div>
      <select data-step="${index}" data-field="speed" ${step.type==='stop'?'disabled':''}>
        ${[90,130,180,220,255].map(v=>`<option value="${v}" ${step.speed===v?'selected':''}>${v}</option>`).join('')}
      </select>
      <input data-step="${index}" data-field="time" type="number" min="0.1" max="10" step="0.1" value="${step.time}">
      <button class="remove-block" data-remove-step="${index}">×</button>`;
    list.appendChild(row);
  });
}
document.querySelectorAll('[data-add-block]').forEach(b=>b.addEventListener('click',()=>{
  if(programSteps.length>=12){$('#programStatus').textContent='Only 12 blocks are allowed';return;}
  const type=b.dataset.addBlock;programSteps.push({type,speed:type==='stop'?0:180,time:type==='stop'?0.5:1});renderProgram();
}));
$('#programList').addEventListener('change',e=>{
  const index=+e.target.dataset.step;if(Number.isNaN(index))return;
  if(e.target.dataset.field==='speed')programSteps[index].speed=Math.max(0,Math.min(255,+e.target.value||0));
  if(e.target.dataset.field==='time')programSteps[index].time=Math.max(.1,Math.min(10,+e.target.value||1));
});
$('#programList').addEventListener('click',e=>{
  const b=e.target.closest('[data-remove-step]');if(!b)return;programSteps.splice(+b.dataset.removeStep,1);renderProgram();
});
$('#clearProgramBtn').addEventListener('click',()=>{programSteps.length=0;renderProgram();$('#programStatus').textContent='Blocks cleared';});
$('#runProgramBtn').addEventListener('click',async()=>{
  if(!programSteps.length){$('#programStatus').textContent='Add blocks first';return;}
  if(!Comm.connected){$('#programStatus').textContent='Connect to the car first';return;}
  const body=programSteps.map(s=>`${s.type},${s.speed},${Math.round(s.time*1000)}`).join('\n');
  try{const r=await fetch(Comm.base+'/program/run',{method:'POST',headers:{'Content-Type':'text/plain'},body});
    $('#programStatus').textContent=await r.text();log('move','PROGRAM  started with '+programSteps.length+' blocks');}
  catch(e){$('#programStatus').textContent='Could not start program';}
});
$('#stopProgramBtn').addEventListener('click',async()=>{
  stopMove();if(Comm.connected)try{const r=await fetch(Comm.base+'/program/stop');$('#programStatus').textContent=await r.text();}catch(e){}
});
renderProgram();

/* ============ TEXT DISPLAY ============ */
function caption(main,sub){
  $('#caption').innerHTML=`${main}<small>${sub||"What the car's pixel display is showing"}</small>`;
}

function sendText(txt){
  txt=(txt||'').trim()||' ';
  D.mode='text';
  D.cols=textCols(txt.toUpperCase());
  D.off=0;
  D.last=performance.now();
  caption('“'+txt+'”','Scrolling text on the display');
  Comm.send({cmd:'text',text:txt},`TEXT  "${txt}"`);
}

$('#scrollBtn').addEventListener('click',()=>sendText($('#textInput').value));
$('#textInput').addEventListener('keydown',e=>{if(e.key==='Enter')sendText($('#textInput').value);});
$('#presets').addEventListener('click',e=>{
  const p=e.target.closest('.preset');if(!p)return;
  const t=p.textContent.trim();
  $('#textInput').value=t;
  sendText(t);
});

/* ============ INIT ============ */
$('#connectBtn').addEventListener('click',()=>Comm.connect($('#ip').value.trim()));
sendText($('#textInput').value);
log('system','ready — text mode. Enter a message or drive the rover.');
if(location.protocol==='http:'||location.protocol==='https:'){
  $('#ip').value=location.host;
  Comm.connect(location.host);
}
</script>
</body>
</html>
)HTMLPAGE";