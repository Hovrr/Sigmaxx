#pragma once
// Embedded benchmark page for the Phase 0 WebView2 shell spike.
// Written to disk next to the user-data folder at startup and served through a
// virtual host name, so the shipped artifact never depends on loose asset files.

namespace sxspike {

inline constexpr char kIndexHtml[] = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Sigmaxx Phase 0 - WebView2 Spike</title>
<style>
  :root { color-scheme: dark; }
  html,body { margin:0; height:100%; overflow:hidden; background:#0b0e14; color:#e6e9f0;
              font-family:"Segoe UI",system-ui,sans-serif; }
  #glcanvas { position:fixed; inset:0; width:100%; height:100%; display:block; }
  .hud { position:fixed; backdrop-filter:blur(6px); background:rgba(10,13,20,.72);
         border:1px solid rgba(255,255,255,.08); border-radius:12px; padding:14px 16px; }
  #topbar   { top:14px; left:14px; right:14px; display:flex; align-items:center; gap:14px; }
  #brand    { font-weight:700; letter-spacing:.4px; white-space:nowrap; }
  #phase    { font-weight:600; color:#8fd3ff; white-space:nowrap; }
  #progressWrap { flex:1; height:6px; border-radius:3px; background:rgba(255,255,255,.09); overflow:hidden; }
  #progressBar  { height:100%; width:0%; border-radius:3px;
                  background:linear-gradient(90deg,#38bdf8,#a78bfa); transition:width .25s linear; }
  #stats    { bottom:14px; left:14px; font:12px/1.6 Consolas,monospace; white-space:pre; min-width:270px; }
  .overlay  { position:fixed; inset:0; z-index:10; display:flex; align-items:center;
              justify-content:center; background:rgba(5,7,11,.72); }
  .panel    { max-width:660px; width:min(94vw,660px); }
  .panel > div { position:static; }
  table     { border-collapse:collapse; width:100%; font-size:13px; }
  td        { padding:7px 8px; border-bottom:1px solid rgba(255,255,255,.07); }
  td:last-child { text-align:right; }
  .chip     { display:inline-block; padding:2px 10px; border-radius:99px; font-weight:700; font-size:12px; }
  .pass     { background:#123c26; color:#4ade80; border:1px solid #14532d; }
  .fail     { background:#45161d; color:#f87171; border:1px solid #7f1d1d; }
  #overall  { font-size:22px; font-weight:800; text-align:center; margin:14px 0 4px; }
  #overall.pass { color:#4ade80; } #overall.fail { color:#f87171; }
  .note     { color:#94a3b8; font-size:12px; line-height:1.5; }
  button    { background:#2563eb; border:none; color:white; padding:8px 16px; border-radius:8px;
              font-weight:600; cursor:pointer; margin-right:8px; }
  button.sec { background:#334155; }
  #savePath { font:11px Consolas,monospace; color:#8fd3ff; word-break:break-all; }
  #cursor   { position:fixed; width:18px; height:18px; margin:-9px 0 0 -9px;
              border:2px solid rgba(255,255,255,.85); border-radius:50%;
              pointer-events:none; z-index:5; display:none;
              box-shadow:0 0 8px rgba(143,211,255,.6); }
</style>
</head>
<body>
<canvas id="glcanvas"></canvas>
<div id="cursor"></div>

<div id="topbar" class="hud">
  <span id="brand">SIGMAXX · P0 SPIKE [WebView2]</span>
  <span id="phase">Booting…</span>
  <div id="progressWrap"><div id="progressBar"></div></div>
</div>

<div id="stats" class="hud">waiting…</div>

<!-- Welcome / instructions -->
<div id="welcome" class="overlay"><div class="panel"><div class="hud">
  <div style="font-size:18px;font-weight:700;margin-bottom:6px">Automated benchmark starting</div>
  <div class="note">During <b>Phase B (~15 s)</b> this test moves your mouse cursor automatically
  along a Lissajous path to measure real input-to-paint latency.<br><br>
  Please do <b>not</b> touch the mouse or keyboard until the results panel appears.</div>
</div></div></div>

<!-- Results -->
<div id="results" class="overlay" style="display:none"><div class="panel"><div class="hud">
  <table id="tbl"></table>
  <div id="overall"></div>
  <div class="note">PASS = blueprint gate met. Stretch goal for p95 latency is ≤ 8 ms.</div>
  <div id="savePath"></div>
  <div style="margin-top:10px">
    <button id="againBtn">Run again (R)</button>
    <button id="copyBtn" class="sec">Copy results JSON</button>
  </div>
</div></div></div>

<script>
"use strict";
/* ============================== helpers ============================== */
const $ = id => document.getElementById(id);
const sleep = ms => new Promise(r => setTimeout(r, ms));
const clamp = (v,a,b)=>Math.min(b,Math.max(a,v));
const median = arr => arr.length ? [...arr].sort((a,b)=>a-b)[arr.length>>1] : NaN;
const pct = (arr,p)=>{ if(!arr.length) return NaN; const s=[...arr].sort((a,b)=>a-b);
                       return s[clamp(Math.floor(p/100*s.length),0,s.length-1)]; };
const avg = arr => arr.length ? arr.reduce((a,b)=>a+b,0)/arr.length : NaN;
const host = window.chrome && window.chrome.webview ? window.chrome.webview : null;
const post = o => { if(host) host.postMessage(JSON.stringify(o)); };
const sx_eal_version = () => "bridge-v0";

let coldStartMs = 0, peakMemMb = 0;

/* ============================== WebGL scene ========================== */
const N = 10000;
const canvas = $("glcanvas");
const gl = canvas.getContext("webgl2", { antialias:true, alpha:false });
if(!gl){ document.body.innerHTML =
  "<p style='padding:40px;font-family:sans-serif'>WebGL2 unavailable - spike cannot run.</p>";
  throw new Error("no webgl2"); }

function sh(type,src){ const s=gl.createShader(type); gl.shaderSource(s,src); gl.compileShader(s);
  if(!gl.getShaderParameter(s,gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s)); return s; }
const prog = gl.createProgram();
gl.attachShader(prog, sh(gl.VERTEX_SHADER,
  "#version 300 es\nlayout(location=0) in vec2 aCorner;" +
  "layout(location=1) in vec2 iPos;layout(location=2) in float iScale;" +
  "layout(location=3) in float iRot;layout(location=4) in float iHue;" +
  "uniform vec3 uCam;uniform vec2 uHalf;out float vHue;out vec2 vLocal;" +
  "void main(){float c=cos(iRot),s=sin(iRot);" +
  "vec2 p=vec2(aCorner.x*c-aCorner.y*s,aCorner.x*s+aCorner.y*c)*iScale+iPos;" +
  "vec2 rel=(p-uCam.xy)*uCam.z;gl_Position=vec4(rel.x/uHalf.x,-rel.y/uHalf.y,0.,1.);vHue=iHue;vLocal=aCorner;}"));
gl.attachShader(prog, sh(gl.FRAGMENT_SHADER,
  "#version 300 es\nprecision mediump float;in float vHue;in vec2 vLocal;out vec4 frag;" +
  "vec3 hsv(float h){vec3 k=mod(vec3(5.,3.,1.)+h*6.,6.);return 1.-max(min(min(k,4.-k),2.),0.);}" +
  "void main(){float d=length(vLocal);float a=smoothstep(1.,.72,d);" +
  "frag=vec4(hsv(vHue)*.85+.15,a*.95);}"));
gl.linkProgram(prog);
if(!gl.getProgramParameter(prog,gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(prog));
gl.useProgram(prog);

const quad = new Float32Array([-1,-1, 1,-1, -1,1,   1,-1, 1,1, -1,1]);
gl.bindBuffer(gl.ARRAY_BUFFER, gl.createBuffer());
gl.bufferData(gl.ARRAY_BUFFER, quad, gl.STATIC_DRAW);
gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0,2,gl.FLOAT,false,0,0);

const instBuf = new Float32Array(N*5);
const iBuf = gl.createBuffer();
gl.bindBuffer(gl.ARRAY_BUFFER, iBuf);
gl.bufferData(gl.ARRAY_BUFFER, instBuf.byteLength, gl.DYNAMIC_DRAW);
[[1,2,0],[2,1,8],[3,1,12],[4,1,16]].forEach(([loc,size,off])=>{
  gl.enableVertexAttribArray(loc);
  gl.vertexAttribPointer(loc,size,gl.FLOAT,false,20,off);
  gl.vertexAttribDivisor(loc,1);
});

const cx=new Float32Array(N), cy=new Float32Array(N), rad=new Float32Array(N),
      scl=new Float32Array(N), spd=new Float32Array(N), hue=new Float32Array(N),
      phase=new Float32Array(N);
for(let i=0;i<N;i++){
  cx[i]=(Math.random()*2-1)*430; cy[i]=(Math.random()*2-1)*300;
  rad[i]=24+Math.random()*140;   scl[i]=7+Math.random()*16;
  spd[i]=.25+Math.random()*.95;  hue[i]=(i*0.61803398875)%1; phase[i]=Math.random()*6.283;
}

function resize(){
  const dpr = clamp(window.devicePixelRatio||1,1,2);
  canvas.width  = Math.floor(innerWidth*dpr); canvas.height = Math.floor(innerHeight*dpr);
  gl.viewport(0,0,canvas.width,canvas.height);
}
addEventListener("resize",resize); resize();

/* ============================ metric rings =========================== */
let frameDeltas=[], latSamples=[], fpsHist=[], lastFpsT=performance.now(),
    fpsCount=0, lastInputT=-1, hadInput=false;

// Record the event's OWN timeStamp (same monotonic clock as performance.now()).
// Measuring later against the rAF callback's vsync `now` would go NEGATIVE
// for events that arrive between frame start and callback execution - which
// silently discarded every injected sample (QA round 8).
addEventListener("pointermove", e=>{
  lastInputT = (e.timeStamp && e.timeStamp > 0) ? e.timeStamp : performance.now();
  hadInput = true;
  const c=$("cursor"); c.style.display="block";
  c.style.left=e.clientX+"px"; c.style.top=e.clientY+"px";
}, {passive:true});
addEventListener("wheel", e=>{
  lastInputT = (e.timeStamp && e.timeStamp > 0) ? e.timeStamp : performance.now();
  hadInput = true; e.preventDefault();
}, {passive:false});
canvas.addEventListener("contextmenu", e=>e.preventDefault());

/* ============================= render loop =========================== */
let prevT=performance.now(), simT=0;
function frame(now){
  const dt = now-prevT; prevT = now; simT += dt/1000;
  frameDeltas.push(dt); if(frameDeltas.length>3000) frameDeltas.shift();
  // Input→paint latency: measure against performance.now() AT CALLBACK TIME,
  // never the rAF vsync timestamp (it can predate the event and go negative).
  if(hadInput){
    const l = performance.now() - lastInputT;
    if(l>=0 && l<120) latSamples.push(l);
    hadInput=false;
  }
  if(latSamples.length>4000) latSamples.splice(0,latSamples.length-4000);
  fpsCount++;
  if(now-lastFpsT>=1000){ fpsHist.push(fpsCount*1000/(now-lastFpsT)); if(fpsHist.length>240)fpsHist.shift();
                          fpsCount=0; lastFpsT=now; hudTick(); }

  // camera drift (pan + zoom viewport stress)
  const camX=Math.sin(simT*.21)*260, camY=Math.cos(simT*.17)*200, zoomV=1+.55*Math.sin(simT*.11);
  // CPU-side mutation of every instance each frame (dirty-scene proxy)
  let o=0;
  for(let i=0;i<N;i++){
    const ph = phase[i]+=spd[i]*dt/16.6;
    instBuf[o++]=cx[i]+rad[i]*Math.sin(ph); instBuf[o++]=cy[i]+rad[i]*Math.cos(ph*1.31);
    instBuf[o++]=scl[i]; instBuf[o++]=ph*.5; instBuf[o++]=hue[i];
  }
  gl.bindBuffer(gl.ARRAY_BUFFER,iBuf);
  gl.bufferSubData(gl.ARRAY_BUFFER,0,instBuf);

  gl.clearColor(.043,.055,.078,1); gl.clear(gl.COLOR_BUFFER_BIT);
  gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA,gl.ONE_MINUS_SRC_ALPHA);
  gl.uniform3f(gl.getUniformLocation(prog,"uCam"),camX,camY,zoomV);
  gl.uniform2f(gl.getUniformLocation(prog,"uHalf"),canvas.width/2,canvas.height/2);
  gl.drawArraysInstanced(gl.TRIANGLES,0,6,N);

  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);

/* ========================= benchmark timeline ======================== */
let running=false, currentPhase="idle";
function setPhase(name,detail,frac){
  currentPhase=name; $("phase").textContent=name+(detail?" — "+detail:"");
  if(frac!==undefined) $("progressBar").style.width=(frac*100)+"%";
}
function hudTick(){
  const ft=frameDeltas.slice(-120), med=median(ft)||16.7;
  let jank=0; for(const d of ft) if(d>med*2) jank++;
  $("stats").textContent =
    "phase      "+currentPhase+"\n"+
    "fps        "+(avg(fpsHist.slice(-3))||0).toFixed(1)+"\n"+
    "frame p95  "+(pct(ft,95)||0).toFixed(1)+" ms\n"+
    "jank       "+(ft.length?100*jank/ft.length:0).toFixed(2)+" %\n"+
    "lat p95    "+(pct(latSamples,95)||0).toFixed(1)+" ms  (n="+latSamples.length+")\n"+
    "objects    "+N+"\n"+
    "inj        "+(window.__injectStats ? (window.__injectStats.ticks+" ticks / "+window.__injectStats.moves+" moves") : "-")+"\n"+
    "mem peak   "+peakMemMb+" MB\n"+
    "coldstart  "+coldStartMs+" ms";
}

async function throughputPhase(ms){
  const arr=new Float64Array(1<<16); for(let i=0;i<arr.length;i++) arr[i]=Math.random();
  const start=performance.now(); let ops=0, sink=0;
  while(performance.now()-start<ms){
    const fStart=performance.now();
    while(performance.now()-fStart<4){
      for(let k=0;k<2000;k++){ sink+=arr[(k*2654435761)>>>0 & 65535]*.5; arr[k&65535]=sink; }
      ops+=2000;
    }
    await new Promise(r=>requestAnimationFrame(r));
  }
  if(!isFinite(sink)) console.warn("sink");
  return Math.round(ops/((performance.now()-start)/1000));
}

function buildResults(opsPerSec){
  const ft=frameDeltas, med=median(ft)||16.7;
  let jank=0; for(const d of ft) if(d>med*2) jank++;
  const jankPct=ft.length?100*jank/ft.length:100;
  const p50=pct(latSamples,50), p95=pct(latSamples,95), p99=pct(latSamples,99);
  const fpsAvg=avg(fpsHist.slice(-45))||0;
  const refreshEst=Math.round(1000/(median(frameDeltas.filter(d=>d>0&&d<40))||16.7));
  const fpsGate=Math.max(50,Math.floor(refreshEst*.9));
  const rows=[
    {gate:"Pointer→paint p95",        target:"≤16 ms (stretch ≤8)", value:isNaN(p95)?"n/a":p95.toFixed(1)+" ms", pass:!isNaN(p95)&&p95<=16},
    {gate:"Average FPS",              target:"≥ "+fpsGate,          value:fpsAvg.toFixed(1),                     pass:fpsAvg>=fpsGate},
    {gate:"Jank frames (>2× median)", target:"< 1%",                value:jankPct.toFixed(2)+" %",               pass:jankPct<1},
    {gate:"Edit-command throughput",  target:"≥ 5,000 ops/s",       value:opsPerSec.toLocaleString()+" ops/s",   pass:opsPerSec>=5000},
    {gate:"Cold start",               target:"≤ 3,000 ms",          value:coldStartMs>0?coldStartMs+" ms":"n/a", pass:coldStartMs>0&&coldStartMs<=3000},
    {gate:"Working-set peak",         target:"≤ 700 MB",            value:peakMemMb>0?peakMemMb+" MB":"n/a",     pass:peakMemMb>0&&peakMemMb<=700},
  ];
  return {
    meta:{ when:new Date().toISOString(), ua:navigator.userAgent,
           screen:{w:innerWidth,h:innerHeight,dpr:window.devicePixelRatio||1},
           refreshEstimateHz:refreshEst, objects:N, suite:"phase0-webview2-spike",
           protocol:sx_eal_version(),
           injection:window.__injectStats||null },
    metrics:{ inputLatencyMs:{p50,p95,p99,n:latSamples.length}, fpsAvg,
              frameMedianMs:med, jankPct, throughputOpsPerSec:opsPerSec,
              coldStartMs, peakWorkingSetMb:peakMemMb },
    verdicts:rows, overallPass:rows.every(r=>r.pass) };
}

function finishReport(){
  const r=buildResults(window.__opsPerSec||0); window.__lastResults=r;
  const tbl=$("tbl"); tbl.innerHTML="";
  for(const v of r.verdicts){
    const tr=document.createElement("tr");
    tr.innerHTML="<td>"+v.gate+"</td><td class='note'>"+v.target+"</td><td>"+v.value+
                 "</td><td><span class='chip "+(v.pass?"pass":"fail")+"'>"+(v.pass?"PASS":"FAIL")+"</span></td>";
    tbl.appendChild(tr);
  }
  const ov=$("overall"); ov.textContent=r.overallPass?"VERDICT: PASS":"VERDICT: FAIL";
  ov.className=r.overallPass?"pass":"fail";
  $("results").style.display="flex";
  $("welcome").style.display="none";
  setPhase(r.overallPass?"Done · PASS":"Done · FAIL","see report",1);
  post({type:"save", filename:"sigmaxx-phase0-webview2-results.json",
        content:JSON.stringify(r,null,2)});
  try{ localStorage.setItem("sigmaxx-p0-results",JSON.stringify(r)); }catch(e){}
}

async function runAll(){
  if(running) return; running=true;
  $("welcome").style.display="none"; $("results").style.display="none";
  setPhase("Welcome","hands off mouse/keyboard",0); await sleep(3000);
  latSamples=[]; frameDeltas=[]; fpsHist=[];
  setPhase("A · Idle baseline","measuring refresh & pacing",.15);
  await sleep(6000);
  setPhase("B · Input storm","native SendInput active",.35);
  post({type:"inject-start"}); await sleep(15000); post({type:"inject-stop"});
  setPhase("C · Command throughput","saturating main-thread edit path",.7);
  window.__opsPerSec = await throughputPhase(5000);
  setPhase("D · Compiling report","",.92); await sleep(400);
  finishReport();
}
window.__runAll=runAll;
addEventListener("keydown",e=>{ if(!running&&(e.key==="r"||e.key==="R")) runAll(); });

/* ======================= host message plumbing ======================= */
let began=false;
function beginOnce(){ if(began)return; began=true; setTimeout(runAll,800); }
// Host pushes arrive as ALREADY-PARSED objects via PostWebMessageAsJson
// (strings only when we sent postMessage(string)). Accept both shapes -
// JSON.parse(object) throws and would silently drop every host message.
function absorbHost(m){
  if(!m || typeof m!=="object") return;
  if(m.type==="begin")          beginOnce();
  else if(m.type==="hostinfo")  coldStartMs=m.coldStartMs|0;
  else if(m.type==="mem")       peakMemMb=Math.max(peakMemMb,m.mb|0);
  else if(m.type==="inject-stats") window.__injectStats={ticks:m.ticks,moves:m.moves};
  else if(m.type==="saved")     $("savePath").textContent="Results saved: "+m.path;
}
if(host){
  host.addEventListener("message", ev=>{
    let d=ev.data;
    if(typeof d==="string"){ try{ d=JSON.parse(d); }catch(err){ return; } }
    try{ absorbHost(d); }catch(err){}
  });
  setTimeout(beginOnce,12000);
  post({type:"ready"});   // pull-model initial sync: request hostinfo + mem
}else{
  setTimeout(beginOnce,1200);
}
$("copyBtn").onclick =()=>navigator.clipboard.writeText(JSON.stringify(window.__lastResults||{},null,2)).then(
  ()=>{$("copyBtn").textContent="Copied ✓";}, ()=>{});
$("againBtn").onclick=()=>location.reload();
</script>
</body>
</html>)html";

} // namespace sxspike
