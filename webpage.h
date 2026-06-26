#pragma once

#ifdef ARDUINO
#include <pgmspace.h>
#else
#define PROGMEM
#endif
 
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width,initial-scale=1" />
<title>Flight Black-Box Motion Recorder System</title>
<style>
  :root{
    --page:#f6f9ff; --panel:#ffffff; --panel2:#f8fbff;
    --text:#0a2742; --muted:#647e97; --border:#e3ecf7; --grid:#e9eef6;
    --blue:#2b76ff; --vio:#7b61ff; --teal:#15b89a; --orange:#ff8c3a;
    --shadow: 0 8px 22px rgba(21,44,88,.10); --radius: 14px;
    --kpi-h: 86px; --plane-h: 160px; --chart-h: 100px; --gutter: 12px; --pad: 10px; --title: 12px; --val: 22px;
  }
  *{box-sizing:border-box}
  html,body{height:100%}
  body{
    margin:0;color:var(--text);
    font-family: Inter, system-ui, -apple-system, "Segoe UI", Roboto, Ubuntu, Helvetica, Arial;
    background:
      radial-gradient(1100px 700px at 10% -30%, #eaf2ff 0%, transparent 55%),
      radial-gradient(800px 500px at 120% 30%, #eef2ff 0%, transparent 60%),
      linear-gradient(180deg,#f8fbff, var(--page));
  }
  header{
    position:sticky;top:0;z-index:10; backdrop-filter:saturate(1.1) blur(6px);
    background:linear-gradient(90deg,rgba(255,255,255,.9),rgba(255,255,255,.9));
    border-bottom:1px solid var(--border); padding:6px 12px; display:flex; align-items:center; gap:8px;
  }
  header .logo{ width:22px;height:22px;border-radius:6px; background: conic-gradient(from 180deg, var(--blue), var(--vio), var(--teal), var(--blue)); box-shadow: inset 0 0 3px rgba(255,255,255,.7), 0 0 0 1px #dfe8fa; animation: spin 10s linear infinite;}
  @keyframes spin{to{transform:rotate(360deg)}}
  header h1{margin:0;font-size:14px;letter-spacing:.2px;font-weight:800}
  header .pill{margin-left:auto;font-size:11px;color:var(--muted); padding:4px 8px;border-radius:999px;border:1px solid var(--border); background: var(--panel); box-shadow: var(--shadow);}
 
  main{ padding:12px; display:grid; gap:var(--gutter); grid-template-columns:repeat(12,1fr); max-width:1180px; margin:0 auto; }
  .card{ grid-column:span 12; position:relative; overflow:hidden; border-radius:var(--radius); padding:var(--pad); border:1px solid var(--border); background: linear-gradient(180deg, var(--panel), var(--panel2)); box-shadow: var(--shadow); }
  .row{display:grid; gap:var(--gutter); grid-template-columns:repeat(12,1fr)}
  .span-3{grid-column:span 3}.span-4{grid-column:span 4}.span-5{grid-column:span 5}.span-6{grid-column:span 6}.span-7{grid-column:span 7}.span-12{grid-column:span 12}
 
  .kpi{ border:1px solid var(--border); border-radius:12px; padding:8px; min-height:var(--kpi-h); background: linear-gradient(180deg,#ffffff,#f8fbff); }
  .kpi h3{ margin:0 0 4px; color:var(--muted); font-size:var(--title); font-weight:800; letter-spacing:.2px }
  .kpi .val{ font-size:var(--val); font-weight:900; letter-spacing:.2px }
  .kpi .sub{ color:var(--muted); font-size:10px; margin-left:6px }
 
  .chart{ height:var(--chart-h); border-radius:12px; border:1px solid var(--border); background: linear-gradient(180deg,#ffffff,#f7faff); position:relative; overflow:hidden }
  .chart canvas{ width:100%; height:100%; display:block }
  .chart .shine{ position:absolute; inset:0; background:linear-gradient(90deg, transparent, rgba(0,0,0,.04), transparent); transform:translateX(-100%); animation: sweep 7s ease-in-out infinite }
  @keyframes sweep{50%{transform:translateX(100%)}100%{transform:translateX(100%)}}
 
  /* Bullet key-value rows (no numbers) */
  .kv{ display:grid; grid-template-columns: 12px 1fr auto; align-items:center; gap:6px 10px; }
  .bullet{ width:8px; height:8px; border-radius:50%; background:#0a2742; box-shadow: 0 1px 3px rgba(0,0,0,.15); }
  .label{ font-size:12px; color:var(--muted) }
  .val{ font-weight:800; font-size:13px; color:var(--text) }
 
  /* Attitude */
  .plane-wrap{ position:relative; height:var(--plane-h); border-radius:12px; border:1px solid var(--border); background: linear-gradient(180deg,#ffffff,#f6f9ff); overflow:hidden }
  .sky{ position:absolute; inset:0; opacity:.45; background:
    radial-gradient(3px 3px at 10% 30%, #b0c2e3, transparent 60%),
    radial-gradient(2px 2px at 40% 60%, #b0c2e3, transparent 60%),
    radial-gradient(2px 2px at 70% 20%, #b0c2e3, transparent 60%),
    radial-gradient(3px 3px at 85% 75%, #b0c2e3, transparent 60%),
    radial-gradient(2px 2px at 25% 80%, #b0c2e3, transparent 60%); animation: twinkle 6s ease-in-out infinite alternate }
  @keyframes twinkle{from{opacity:.25}to{opacity:.55}}
  .horizon{position:absolute; left:0; right:0; top:50%; height:1px; background:#d8e2f2; transform-origin:50% 50%}
  .plane{ position:absolute; left:50%; top:50%; width:96px; height:96px; transform:translate(-50%,-50%); display:grid; place-items:center; filter: drop-shadow(0 6px 18px rgba(20,40,90,.22)); transition: transform 120ms linear }
  .plane svg{ width:96px; height:96px }
  .plane path{ fill:url(#fuselageDark); stroke:#324b77; stroke-width:.8 }
  .bank-glow{ position:absolute; left:50%; top:50%; transform:translate(-50%,-50%); width:180px; height:180px; border-radius:50%; background:radial-gradient(closest-side, rgba(30,70,140,.14), transparent 70%) }
 
  h3[accent]{ font-weight:800; font-size:var(--title); letter-spacing:.2px; margin:0 0 8px; color:var(--muted) }
  h3 .dot{ display:inline-block; width:7px; height:7px; border-radius:50%; margin-right:6px; vertical-align:middle }
  .blue .dot{ background:var(--blue) } .vio .dot{ background:var(--vio) } .teal .dot{ background:var(--teal) } .orange .dot{ background:var(--orange) }
 
  @media (max-width: 980px){ .span-7,.span-5{grid-column:span 12} :root{ --plane-h: 180px } }
</style>
</head>
<body>
  <header>
    <div class="logo" aria-hidden="true"></div>
    <h1>Flight Black-Box Motion Recorder System</h1>
    <div class="pill" id="uptime">—</div>
  </header>
 
  <main>
    <!-- KPI Row -->
    <section class="card span-12" style="padding:8px;">
      <div class="row">
        <div class="span-3">
          <div class="kpi">
            <h3 class="blue" accent><span class="dot"></span>Now |a| (m/s²)</h3>
            <div class="val" id="nowA_ms2">—</div>
            <span class="sub" id="addr">I²C: —</span>
          </div>
        </div>
        <div class="span-3">
          <div class="kpi">
            <h3 class="vio" accent><span class="dot"></span>|ω| (dps)</h3>
            <div class="val" id="gyroAbs">—</div>
            <span class="sub" id="hz">Rate: — Hz</span>
          </div>
        </div>
        <div class="span-3">
          <div class="kpi">
            <h3 class="teal" accent><span class="dot"></span>Vibration (m/s²)</h3>
            <div class="val" id="vib_ms2">—</div>
            <span class="sub" id="vibRms">RMS: —</span>
          </div>
        </div>
        <div class="span-3">
          <div class="kpi">
            <h3 class="orange" accent><span class="dot"></span>Roll / Pitch</h3>
            <div class="val"><span id="roll">—</span>° / <span id="pitch">—</span>°</div>
            <span class="sub" id="lastEvent">Last: —</span>
          </div>
        </div>
      </div>
    </section>
 
    <!-- Visualizer (7) + Diagnostics (5) -->
    <section class="card span-12" style="padding:8px;">
      <div class="row">
        <div class="span-7">
          <h3 class="blue" accent><span class="dot"></span>Attitude Visualizer</h3>
          <div class="plane-wrap">
            <div class="sky"></div>
            <div class="horizon" id="horizon"></div>
            <div class="bank-glow" id="bankGlow"></div>
            <div class="plane" id="plane">
              <svg viewBox="0 0 100 100" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="plane">
                <defs>
                  <linearGradient id="fuselageDark" x1="0" x2="1" y1="0" y2="1">
                    <stop offset="0%" stop-color="#2a4a8a"/>
                    <stop offset="55%" stop-color="#1f3b73"/>
                    <stop offset="100%" stop-color="#16325f"/>
                  </linearGradient>
                </defs>
                <path d="M50 12 L57 28 L92 40 L92 48 L57 48 L50 86 L43 48 L8 48 L8 40 L43 28 Z"/>
                <path d="M46 30 Q50 26 54 30 L50 40 Z" fill="#dce6f8" opacity=".6" stroke="none"/>
              </svg>
            </div>
          </div>
        </div>
 
        <!-- Diagnostics: ax/ay/az (left column), gx/gy/gz (right column) -->
        <div class="span-5">
          <h3 class="vio" accent><span class="dot"></span>Diagnostics (axes)</h3>
          <div class="row">
            <div class="span-6">
              <div class="kv"><div class="bullet"></div><div class="label">ax (m/s²)</div><div class="val" id="ax_ms2">—</div></div>
              <div class="kv"><div class="bullet"></div><div class="label">ay (m/s²)</div><div class="val" id="ay_ms2">—</div></div>
              <div class="kv"><div class="bullet"></div><div class="label">az (m/s²)</div><div class="val" id="az_ms2">—</div></div>
            </div>
            <div class="span-6">
              <div class="kv"><div class="bullet"></div><div class="label">gx (dps)</div><div class="val" id="gx">—</div></div>
              <div class="kv"><div class="bullet"></div><div class="label">gy (dps)</div><div class="val" id="gy">—</div></div>
              <div class="kv"><div class="bullet"></div><div class="label">gz (dps)</div><div class="val" id="gz">—</div></div>
            </div>
          </div>
        </div>
      </div>
    </section>
 
    <!-- Peaks & Events (bulleted, tidy) -->
    <section class="card span-12" style="padding:8px;">
      <div class="row">
        <div class="span-6">
          <h3 class="teal" accent><span class="dot"></span>Peaks (m/s² / dps)</h3>
          <div class="row">
            <div class="span-6">
              <div class="kv"><div class="bullet"></div><div class="label">Max |a|</div><div class="val" id="maxA_ms2">—</div></div>
              <div class="kv"><div class="bullet"></div><div class="label">Max |ω|</div><div class="val" id="maxDPS">—</div></div>
              <div class="kv"><div class="bullet"></div><div class="label">Max Vib</div><div class="val" id="maxVib_ms2">—</div></div>
            </div>
            <div class="span-6">
              <div class="kv"><div class="bullet"></div><div class="label">Min |a|</div><div class="val" id="minA_ms2">—</div></div>
              <div class="kv"><div class="bullet"></div><div class="label">Min |ω|</div><div class="val" id="minDPS">—</div></div>
              <div class="kv"><div class="bullet"></div><div class="label">Last Impact</div><div class="val" id="lastImpact_ms2">—</div></div>
            </div>
          </div>
        </div>
        <div class="span-6">
          <h3 class="orange" accent><span class="dot"></span>Events</h3>
          <div class="kv"><div class="bullet"></div><div class="label">Free-falls</div><div class="val" id="freefalls">—</div></div>
          <div class="kv"><div class="bullet"></div><div class="label">Impacts</div><div class="val" id="impacts">—</div></div>
        </div>
      </div>
    </section>
 
    <!-- Charts Row -->
    <section class="card span-12" style="padding:8px;">
      <div class="row">
        <div class="span-4">
          <h3 class="blue" accent><span class="dot"></span>Accel |a|-1g (m/s²)</h3>
          <div class="chart"><span class="shine"></span><canvas id="cAccel"></canvas></div>
        </div>
        <div class="span-4">
          <h3 class="vio" accent><span class="dot"></span>Gyro |ω| (dps)</h3>
          <div class="chart"><span class="shine"></span><canvas id="cGyro"></canvas></div>
        </div>
        <div class="span-4">
          <h3 class="teal" accent><span class="dot"></span>Vibration RMS (m/s²)</h3>
          <div class="chart"><span class="shine"></span><canvas id="cVib"></canvas></div>
        </div>
      </div>
    </section>
  </main>
 
<script>
(() => {
  const el = id => document.getElementById(id);
  const G = 9.81; // m/s² per g
 
  function makeChart(canvasId, ymax, colorTop, colorBottom) {
    const c = el(canvasId), ctx = c.getContext('2d');
    const W = c.width = c.clientWidth || 360;
    const H = c.height = c.clientHeight || 100;
    const N = 200;
    const data = new Array(N).fill(0);
    function draw() {
      ctx.clearRect(0,0,W,H);
      ctx.strokeStyle = "#e9eef6"; ctx.lineWidth = 1;
      for (let y=0;y<H;y+=20){ ctx.beginPath(); ctx.moveTo(0,y+0.5); ctx.lineTo(W,y+0.5); ctx.stroke(); }
      const g = ctx.createLinearGradient(0,0,0,H);
      g.addColorStop(0, colorTop); g.addColorStop(1, colorBottom);
      ctx.lineWidth = 2; ctx.strokeStyle = g;
      ctx.beginPath();
      for (let i=0;i<N;i++){
        const vx = (i/(N-1))*W;
        const v  = Math.min(ymax, Math.max(0, data[i]));
        const vy = H - (v/ymax)*(H-8) - 4;
        if(i===0) ctx.moveTo(vx,vy); else ctx.lineTo(vx,vy);
      }
      ctx.stroke();
    }
    function push(v){ data.push(v); data.shift(); draw(); }
    draw(); return { push };
  }
 
  const chartAccel = makeChart('cAccel', 2*G, 'rgba(43,118,255,0.95)','rgba(43,118,255,0.20)');
  const chartGyro  = makeChart('cGyro', 1200.0, 'rgba(123,97,255,0.95)','rgba(123,97,255,0.20)');
  const chartVib   = makeChart('cVib', 2*G, 'rgba(21,184,154,0.95)','rgba(21,184,154,0.20)');
 
  function fmtMs(ms){ if(!ms) return 'None'; if(ms<1000) return ms+' ms'; const s=Math.floor(ms/1000); return s<60? s+' s' : Math.floor(s/60)+' min'; }
  function clamp(v,lo,hi){ return v<lo?lo:v>hi?hi:v; }
 
  function updatePlane(roll, pitch){
    const p = el('plane'), h = el('horizon'), glow = el('bankGlow');
    const r = clamp(roll,-70,70), pch = clamp(pitch,-35,35);
    p.style.transform = `translate(-50%,-50%) rotate(${r}deg) translateY(${pch*-0.8}px)`;
    h.style.transform = `translateY(-50%) rotate(${-r*0.6}deg) translateY(${pch*1.1}px)`;
    glow.style.background = `radial-gradient(closest-side, rgba(30,70,140,${Math.min(0.08 + Math.abs(r)/180, 0.18)}), transparent 70%)`;
  }
 
  async function tick(){
    try{
      const r = await fetch('/data',{cache:'no-store'});
      if(!r.ok) throw new Error('HTTP '+r.status);
      const d = await r.json();
 
      // g -> m/s² conversions
      const nowA_ms2 = d.nowG*G, vib_ms2 = d.vibRms*G;
      const ax_ms2 = d.ax*G, ay_ms2 = d.ay*G, az_ms2 = d.az*G;
      const maxA_ms2 = d.peaks.maxG*G, minA_ms2 = d.peaks.minG*G;
      const lastImp_ms2 = d.peaks.lastImpactG*G, maxVib_ms2 = d.peaks.maxVib*G;
 
      // KPIs
      el('nowA_ms2').textContent = nowA_ms2.toFixed(2);
      el('gyroAbs').textContent  = Math.round(d.gyroAbs);
      el('vib_ms2').textContent  = vib_ms2.toFixed(2);
      el('vibRms').textContent   = `RMS: ${vib_ms2.toFixed(2)} m/s²`;
      el('hz').textContent       = `Rate: ${Math.round(d.hz)} Hz`;
      el('roll').textContent     = d.roll.toFixed(1);
      el('pitch').textContent    = d.pitch.toFixed(1);
      el('addr').textContent     = `I²C: 0x${d.addr.toString(16)}`;
      el('lastEvent').textContent= fmtMs(d.lastEventAgeMs);
      el('uptime').textContent   = d.uptime;
 
      // Diagnostics
      el('ax_ms2').textContent = ax_ms2.toFixed(2);
      el('ay_ms2').textContent = ay_ms2.toFixed(2);
      el('az_ms2').textContent = az_ms2.toFixed(2);
      el('gx').textContent = Math.round(d.gx);
      el('gy').textContent = Math.round(d.gy);
      el('gz').textContent = Math.round(d.gz);
 
      // Peaks & events
      el('maxA_ms2').textContent     = maxA_ms2.toFixed(2);
      el('minA_ms2').textContent     = minA_ms2.toFixed(2);
      el('maxDPS').textContent       = Math.round(d.peaks.maxDPS);
      el('minDPS').textContent       = Math.round(d.peaks.minDPS);
      el('maxVib_ms2').textContent   = maxVib_ms2.toFixed(2);
      el('lastImpact_ms2').textContent= lastImp_ms2.toFixed(2);
      el('freefalls').textContent    = d.counts.freefalls;
      el('impacts').textContent      = d.counts.impacts;
 
      // Charts
      chartAccel.push(Math.min(2*G, Math.abs(d.nowG - 1.0)*G));
      chartGyro.push(Math.min(1200, d.gyroAbs));
      chartVib.push(Math.min(2*G, vib_ms2));
 
      updatePlane(d.roll, d.pitch);
    }catch(e){
      // silent retry
    }finally{
      setTimeout(tick, 120);
    }
  }
  tick();
})();
</script>
</body>
</html>
)rawliteral";