#pragma once

// Live chart page served at "/". Self-contained: the board's access point has
// no internet, so no external fonts or scripts.
static const char webPage[] = R"HTML(<!doctype html>
<html lang="pl">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="light dark">
<title>Wibrometr na żywo</title>
<style>
:root {
  --desk: #e2e8e4;
  --paper: #f6f8f6;
  --edge: #d3ddd7;
  --grid: #e1e8e3;
  --grid-major: #c3d1c8;
  --ink: #17232b;
  --ink-soft: #566871;
  --x: #c42638;
  --y: #0d7a52;
  --z: #2645c2;
  --amber: #a86d00;
  --warn: #b0341c;
  --live: #0d7a52;
  --focus: #2645c2;
  --font: Bahnschrift, "DIN Alternate", "DIN 2014", "Roboto Condensed", "Arial Narrow", system-ui, sans-serif;
}
@media (prefers-color-scheme: dark) {
  :root {
    --desk: #0b1114;
    --paper: #111a1e;
    --edge: #1e2b31;
    --grid: #18242a;
    --grid-major: #2a3b43;
    --ink: #dbe5e1;
    --ink-soft: #8b9ea4;
    --x: #ff6b78;
    --y: #4ccf98;
    --z: #8299ff;
    --amber: #f0b43c;
    --warn: #ff7a5c;
    --live: #4ccf98;
    --focus: #8299ff;
  }
}
* { box-sizing: border-box; }
html { -webkit-text-size-adjust: 100%; }
body {
  margin: 0;
  background: var(--desk);
  color: var(--ink);
  font-family: var(--font);
  font-stretch: semi-condensed;
  font-size: 15px;
  line-height: 1.4;
  font-variant-numeric: tabular-nums;
}
.wrap { max-width: 1180px; margin: 0 auto; padding: 0 16px 40px; }

/* Instrument bar: name, link to the board, what the meter is doing. */
.bar {
  display: flex; flex-wrap: wrap; align-items: baseline; gap: 6px 20px;
  padding: 18px 0 14px;
}
.bar h1 { margin: 0; font-size: 1.6rem; font-weight: 700; letter-spacing: -0.01em; }
.bar .what { color: var(--ink-soft); }
.bar .status { margin-left: auto; display: flex; gap: 18px; align-items: baseline; }
.link { display: inline-flex; align-items: center; gap: 7px; }
.dot { width: 9px; height: 9px; border-radius: 50%; background: var(--warn); align-self: center; }
.link.on .dot { background: var(--live); box-shadow: 0 0 0 3px color-mix(in srgb, var(--live) 22%, transparent); }
.state { color: var(--ink-soft); }
.state.moving { color: var(--amber); font-weight: 600; }
.state.alert { color: var(--warn); }

.tools { display: flex; flex-wrap: wrap; gap: 10px 22px; align-items: center; padding: 0 0 12px; }
.tools .hint { color: var(--ink-soft); }
button {
  font: inherit; font-stretch: inherit; color: var(--ink); background: var(--paper);
  border: 1px solid var(--grid-major); border-radius: 4px; padding: 6px 12px; cursor: pointer;
}
button:hover { background: color-mix(in srgb, var(--paper) 80%, var(--grid-major)); }
button:focus-visible, input:focus-visible { outline: 2px solid var(--focus); outline-offset: 2px; }
label.check { display: inline-flex; gap: 8px; align-items: center; cursor: pointer; }
input[type=checkbox] { width: 16px; height: 16px; accent-color: var(--ink); margin: 0; }

/* Chart paper. */
.sheet { background: var(--paper); border: 1px solid var(--edge); border-radius: 3px; margin-bottom: 14px; }
.sheet > header { display: flex; align-items: baseline; gap: 10px; padding: 10px 14px 0; flex-wrap: wrap; }
.sheet h2 { margin: 0; font-size: 1.08rem; font-weight: 600; }
.unit { color: var(--ink-soft); }
.legend { margin-left: auto; display: flex; gap: 14px; color: var(--ink-soft); }
.legend i { display: inline-block; width: 14px; height: 3px; border-radius: 2px; vertical-align: middle; margin-right: 5px; }
canvas { display: block; width: 100%; }
#accel canvas { height: 250px; }
#gyro canvas { height: 170px; }

/* Last move. */
.move-body { display: grid; grid-template-columns: minmax(230px, 0.8fr) 1.3fr 1fr; gap: 0; }
.move-body > * { padding: 12px 14px 14px; }
.move-body > * + * { border-left: 1px solid var(--edge); }
.axis-row { display: grid; grid-template-columns: 26px 1fr; align-items: baseline; padding: 2px 0; }
.axis-row b { font-size: 1.4rem; font-weight: 700; }
.axis-row .num { font-size: 2.5rem; font-weight: 300; line-height: 1.1; letter-spacing: -0.02em; }
.axis-row .num small { font-size: 1rem; color: var(--ink-soft); margin-left: 4px; letter-spacing: 0; }
.axis-row .sum { grid-column: 2; color: var(--ink-soft); margin-top: -2px; }
.readout.live .num { color: var(--amber); }
.facts { display: grid; grid-template-columns: auto 1fr; gap: 2px 14px; margin: 12px 0 0; }
.facts dt { color: var(--ink-soft); }
.facts dd { margin: 0; }
.note { margin: 10px 0 0; color: var(--ink-soft); max-width: 34ch; }
.note.alert { color: var(--warn); }
.move-body figure { margin: 0; }
.move-body figcaption { color: var(--ink-soft); margin-bottom: 6px; }
#mvTime, #mvPath { height: 230px; }
.reset-row { display: flex; gap: 10px; align-items: center; margin-top: 14px; }
.reset-row span { color: var(--ink-soft); }

/* Line: side view of the roller line, tables as frames, the part as a car. */
#line .head { display: flex; flex-wrap: wrap; gap: 10px 24px; align-items: flex-end; padding: 14px 16px 4px; }
#line .where { font-size: 2.1rem; font-weight: 600; line-height: 1.1; letter-spacing: -0.01em; }
#line .where.moving { color: var(--amber); }
#line .sub { color: var(--ink-soft); min-height: 1.4em; }
#line .sub.fix { color: var(--live); }
#line .ctrl { margin-left: auto; display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
#line .ctrl label { color: var(--ink-soft); display: inline-flex; gap: 6px; align-items: center; }
select { font: inherit; font-stretch: inherit; color: var(--ink); background: var(--paper); border: 1px solid var(--grid-major); border-radius: 4px; padding: 5px 6px; }
select:focus-visible { outline: 2px solid var(--focus); outline-offset: 2px; }
button.primary { background: var(--ink); color: var(--paper); border-color: var(--ink); }
button.primary:hover { background: color-mix(in srgb, var(--ink) 85%, var(--paper)); }
button.quiet { border-color: transparent; color: var(--ink-soft); background: transparent; }
button.quiet:hover { color: var(--warn); background: transparent; }
.scene { overflow-x: auto; padding: 0 6px 6px; }
.scene svg { display: block; width: 100%; min-width: 560px; height: auto; font-family: var(--font); }
.scene text { font-stretch: semi-condensed; }
.st-frame { fill: var(--paper); stroke: var(--grid-major); stroke-width: 1.5; cursor: pointer; }
.st.here .st-frame { stroke: var(--ink); stroke-width: 2.5; fill: color-mix(in srgb, var(--paper) 88%, var(--y)); }
.st.sel .st-frame { stroke-dasharray: 5 3; stroke: var(--ink); }
.st.here.sel .st-frame { stroke-dasharray: none; }
.st-name { fill: var(--ink); font-size: 15px; font-weight: 600; }
.st.no-fp .st-bars { display: none; }
.st-empty { fill: var(--ink-soft); font-size: 11px; }
.st-bar { fill: var(--ink-soft); opacity: 0.55; }
.st.here .st-bar { fill: var(--ink); opacity: 0.85; }
.vib-bar { opacity: 0.9; }
.band { fill: var(--grid-major); }
.rollers { stroke: var(--ink-soft); stroke-width: 6; stroke-dasharray: 2 9; }
.dim { stroke: var(--ink-soft); stroke-width: 1; }
.dim-text { fill: var(--ink-soft); font-size: 13px; }
.dim-text.learned { fill: var(--ink); }
.match { fill: var(--grid-major); }
.match-fill { fill: var(--ink-soft); }
.match-fill.best { fill: var(--live); }
.car-body { fill: var(--ink); }
.car.moving .car-body { fill: var(--amber); }
.car-glass { fill: var(--paper); opacity: 0.85; }
.wheel { fill: var(--ink); stroke: var(--paper); stroke-width: 1.5; }
.hub { stroke: var(--paper); stroke-width: 1.5; }
.ring-bg { fill: none; stroke: var(--grid-major); stroke-width: 3; }
.ring { fill: none; stroke: var(--ink); stroke-width: 3; stroke-linecap: round; }
.ring-text { fill: var(--ink); font-size: 11px; font-weight: 600; }
.compass circle { fill: var(--paper); stroke: var(--ink-soft); stroke-width: 1.2; }
.compass path { fill: var(--ink); }
.compass.turning circle { stroke: var(--amber); stroke-width: 2; }
.compass.turning path { fill: var(--amber); }
.wifi path { fill: none; stroke: var(--live); stroke-width: 2.4; stroke-linecap: round; }
.wifi.on path { animation: wave 1.2s ease-in-out infinite; }
.wifi.on path:nth-child(2) { animation-delay: 0.15s; }
.wifi.on path:nth-child(3) { animation-delay: 0.3s; }
@keyframes wave { 0%, 100% { opacity: 0.25; } 50% { opacity: 1; } }

.fp-wrap { display: grid; grid-template-columns: 1.4fr 1fr; border-top: 1px solid var(--edge); }
.fp-wrap > div { padding: 12px 16px 14px; }
.fp-wrap > div + div { border-left: 1px solid var(--edge); }
.fp-wrap h3 { margin: 0 0 8px; font-size: 1rem; font-weight: 600; }
.fp-list { display: grid; grid-template-columns: minmax(90px, 14ch) 1fr 8ch; gap: 4px 10px; align-items: center; }
.scene-key { color: var(--ink-soft); padding: 0 16px 12px; margin: 0; }
.fp-name { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; color: var(--ink); }
.fp-name.mac { color: var(--ink-soft); }
.fp-track { position: relative; height: 10px; background: var(--grid); border-radius: 2px; }
.fp-fill { position: absolute; left: 0; top: 0; bottom: 0; background: var(--ink-soft); border-radius: 2px; }
.fp-now { position: absolute; top: -3px; bottom: -3px; width: 3px; margin-left: -1.5px; background: var(--amber); border-radius: 1px; }
.fp-db { text-align: right; color: var(--ink-soft); }
.fp-key { display: flex; gap: 16px; color: var(--ink-soft); margin-top: 10px; flex-wrap: wrap; }
.fp-key i { display: inline-block; width: 14px; height: 8px; border-radius: 2px; vertical-align: middle; margin-right: 6px; background: var(--ink-soft); }
.fp-key i.now { width: 3px; height: 12px; background: var(--amber); }
.facts.wide { grid-template-columns: auto 1fr; margin-top: 0; }
.gap-top { margin-top: 16px !important; }
.meter { position: relative; height: 12px; background: var(--grid); border-radius: 3px; margin: 4px 0 8px; }
.meter-fill { position: absolute; left: 0; top: 0; bottom: 0; width: 0; background: var(--ink-soft); border-radius: 3px; transition: width 0.15s linear; }
.meter-fill.over { background: var(--amber); }
.meter-tick { position: absolute; top: -4px; bottom: -4px; width: 2px; margin-left: -1px; background: var(--ink); }
.meter-text { margin: 0; color: var(--ink-soft); max-width: 52ch; }
@media (prefers-reduced-motion: reduce) { .meter-fill { transition: none; } }
.empty { color: var(--ink-soft); margin: 0; max-width: 46ch; }

footer { color: var(--ink-soft); padding-top: 6px; }

@media (max-width: 900px) {
  .move-body { grid-template-columns: 1fr 1fr; }
  .move-body > .readout { grid-column: 1 / -1; }
  .move-body > * + * { border-left: 0; border-top: 1px solid var(--edge); }
  .move-body > figure + figure { border-left: 1px solid var(--edge); }
}
@media (max-width: 760px) {
  .fp-wrap { grid-template-columns: 1fr; }
  .fp-wrap > div + div { border-left: 0; border-top: 1px solid var(--edge); }
  #line .ctrl { margin-left: 0; }
}
@media (prefers-reduced-motion: reduce) {
  .wifi.on path { animation: none; opacity: 1; }
}
@media (max-width: 600px) {
  .bar .status { margin-left: 0; width: 100%; }
  #line .where { font-size: 1.7rem; }
  .move-body { grid-template-columns: 1fr; }
  .move-body > figure + figure { border-left: 0; }
  #accel canvas { height: 200px; }
  #gyro canvas { height: 150px; }
  .legend { margin-left: 0; width: 100%; }
}
</style>
</head>
<body>
<div class="wrap">
  <div class="bar">
    <h1>Wibrometr</h1>
    <span class="what">czujnik MPU6050 na detalu, linia rolkowa</span>
    <div class="status">
      <span class="link" id="link"><span class="dot"></span><span id="linkText">Łączenie z płytką…</span></span>
      <span class="state" id="state"></span>
    </div>
  </div>

  <div class="sub" id="networkStatus" aria-live="polite"></div>

  <section class="sheet" id="line">
    <div class="head">
      <div>
        <div class="where" id="where">Łączenie…</div>
        <div class="sub" id="whereSub"></div>
      </div>
      <div class="ctrl">
        <button class="primary" id="home">Detal jest na stole 1</button>
        <button class="quiet" id="forget">Zapomnij odciski Wi-Fi</button>
      </div>
    </div>
    <div class="scene"><svg id="scene" role="img" aria-label="Linia: stoły i pozycja detalu"></svg></div>
    <p class="scene-key">W stole: odcisk Wi-Fi, sieci ułożone wg stołu, przy którym są najsilniejsze. Pod stołem: podobieństwo ostatniego skanu do tego odcisku. Kliknij stół, aby zobaczyć jego sieci.</p>
    <div class="fp-wrap">
      <div>
        <h3 id="fpTitle">Odcisk Wi-Fi stołu</h3>
        <div class="fp-list" id="fpList"></div>
        <div class="fp-key"><span><i></i>wzorzec stołu</span><span><i class="now"></i>ostatni skan</span></div>
      </div>
      <div>
        <h3>Jak płytka wyznacza pozycję</h3>
        <dl class="facts wide" id="lineFacts"></dl>
        <h3 class="gap-top">Filtr drgań konstrukcji</h3>
        <div class="meter" aria-hidden="true"><span class="meter-fill" id="actFill"></span><span class="meter-tick" id="actTick"></span></div>
        <p class="meter-text" id="actText">Czekam na dane z płytki.</p>
      </div>
    </div>
  </section>

  <div class="tools">
    <span class="hint" id="rideInfo">Wykresy pokażą ostatni przejazd, od ruszenia do zatrzymania.</span>
    <label class="check"><input type="checkbox" id="dc"> Pokaż same drgania (bez grawitacji i offsetu)</label>
  </div>

  <section class="sheet" id="accel">
    <header>
      <h2>Przyspieszenie</h2><span class="unit">m/s²</span>
      <span class="legend"><span><i style="background:var(--x)"></i>X</span><span><i style="background:var(--y)"></i>Y</span><span><i style="background:var(--z)"></i>Z</span></span>
    </header>
    <canvas></canvas>
  </section>

  <section class="sheet" id="gyro">
    <header>
      <h2>Prędkość kątowa</h2><span class="unit">°/s, offset zera odjęty</span>
    </header>
    <canvas></canvas>
  </section>

  <section class="sheet" id="move">
    <header><h2>Ostatni ruch</h2><span class="unit" id="moveNo"></span></header>
    <div class="move-body">
      <div class="readout" id="readout">
        <div class="axis-row"><b style="color:var(--x)">X</b><span class="num" id="dx">–</span><span class="sum" id="sx"></span></div>
        <div class="axis-row"><b style="color:var(--y)">Y</b><span class="num" id="dy">–</span><span class="sum" id="sy"></span></div>
        <div class="axis-row"><b style="color:var(--z)">Z</b><span class="num" id="dz">–</span><span class="sum" id="sz"></span></div>
        <dl class="facts" id="facts" hidden>
          <dt>Odległość</dt><dd id="fd"></dd>
          <dt>Czas</dt><dd id="ft"></dd>
          <dt>Maks. prędkość</dt><dd id="fv"></dd>
          <dt>Obrót</dt><dd id="fr"></dd>
        </dl>
        <p class="note" id="note">Połóż czujnik nieruchomo, przesuń go i znów odłóż. Wynik pojawi się po zatrzymaniu.</p>
        <div class="reset-row"><button id="reset">Wyzeruj sumę</button><span id="resetMsg"></span></div>
      </div>
      <figure><figcaption>Położenie w czasie, cm</figcaption><canvas id="mvTime"></canvas></figure>
      <figure><figcaption id="pathCap">Tor ruchu</figcaption><canvas id="mvPath"></canvas></figure>
    </div>
  </section>

  <footer>Wyniki ruchu w osiach czujnika z chwili jego początku. Strona działa tylko w sieci Wi-Fi płytki; podczas skanu Wi-Fi (ok. 5 s) dane mogą chwilę się zatrzymać.</footer>
</div>

<script>
"use strict";
const css = n => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
let C = {};
function readColors() {
  for (const k of ["paper", "grid", "grid-major", "ink", "ink-soft", "x", "y", "z", "amber"]) C[k] = css("--" + k);
  C.font = css("--font");
}
readColors();
matchMedia("(prefers-color-scheme: dark)").addEventListener("change", () => { readColors(); drawMove(false); });
const reduceMotion = matchMedia("(prefers-reduced-motion: reduce)").matches;
const AX = ["x", "y", "z"];

// Smallest 1-2-5 step that is at least v.
function nice(v) {
  const p = Math.pow(10, Math.floor(Math.log10(v)));
  for (const k of [1, 2, 5, 10]) if (k * p >= v - 1e-9) return k * p;
  return 10 * p;
}
// Signed, Polish decimal comma, true minus sign, no "−0,0".
function fmt(v, d) {
  const s = Math.abs(v).toFixed(d);
  if (+s === 0) return s.replace(".", ",");
  return (v > 0 ? "+" : "−") + s.replace(".", ",");
}
// Axis label: as many decimals as the grid step needs.
const tick = (v, step) => (Math.abs(v) < step / 2 ? "0" : fmt(v, Math.max(0, -Math.floor(Math.log10(step) + 1e-9))).replace("+", ""));

function setupCanvas(cv) {
  const r = cv.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
  const w = Math.max(1, Math.round(r.width * dpr)), h = Math.max(1, Math.round(r.height * dpr));
  if (cv.width !== w || cv.height !== h) { cv.width = w; cv.height = h; }
  const ctx = cv.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  return { ctx, w: r.width, h: r.height };
}

// Rolling chart: newest at the right, each axis ends in a pen with its value.
const CAP = 200 * 80; // 80 s at 200 Hz: the longest ride (60 s) with room before it.
class Strip {
  constructor(el, decimals, minSpan) {
    this.cv = el.querySelector("canvas");
    this.d = decimals; this.minSpan = minSpan;
    this.t = new Float64Array(CAP);
    this.v = [new Float32Array(CAP), new Float32Array(CAP), new Float32Array(CAP)];
    this.head = 0; this.n = 0;
    this.lo = -1; this.hi = 1;
  }
  push(t, a, b, c) {
    const i = this.head;
    this.t[i] = t; this.v[0][i] = a; this.v[1][i] = b; this.v[2][i] = c;
    this.head = (i + 1) % CAP; if (this.n < CAP) this.n++;
  }
  clear() { this.n = 0; }
  // Index range [first, last] of samples with t0 <= t <= t1, or null if the
  // buffer does not reach back to t0.
  range(t0, t1) {
    if (!this.n) return null;
    const oldest = (this.head - this.n + CAP) % CAP;
    if (this.t[oldest] > t0) return null;
    const out = [];
    for (let k = 0; k < this.n; k++) {
      const i = (oldest + k) % CAP;
      if (this.t[i] >= t0 && this.t[i] <= t1) out.push(i);
    }
    return out;
  }
  // Samples from t0 to t1 across the whole width (the ride, from its start).
  draw(t0, t1, removeDc, empty) {
    const tSpan = Math.max(t1 - t0, 0.5);
    const { ctx, w, h } = setupCanvas(this.cv);
    const padL = 44, padR = 86, padT = 10, padB = 22;
    const pw = w - padL - padR, ph = h - padT - padB;
    ctx.clearRect(0, 0, w, h);
    // Visible samples, newest first.
    const idx = [];
    for (let k = 0; k < this.n; k++) {
      const i = (this.head - 1 - k + CAP) % CAP;
      if (this.t[i] > t1) continue;
      if (this.t[i] < t0) break;
      idx.push(i);
    }
    const mean = [0, 0, 0];
    if (removeDc && idx.length) for (let a = 0; a < 3; a++) { let s = 0; for (const i of idx) s += this.v[a][i]; mean[a] = s / idx.length; }
    // Scale follows the data, eased so it does not jump.
    let lo = Infinity, hi = -Infinity;
    for (let a = 0; a < 3; a++) for (const i of idx) { const v = this.v[a][i] - mean[a]; if (v < lo) lo = v; if (v > hi) hi = v; }
    if (!idx.length) { lo = -this.minSpan / 2; hi = this.minSpan / 2; }
    const mid = (lo + hi) / 2, span = Math.max(hi - lo, this.minSpan) * 1.15;
    const tLo = mid - span / 2, tHi = mid + span / 2;
    // Widen at once (nothing gets clipped), narrow slowly (no jumping).
    this.lo = tLo < this.lo ? tLo : this.lo + (tLo - this.lo) * 0.08;
    this.hi = tHi > this.hi ? tHi : this.hi + (tHi - this.hi) * 0.08;
    const y = v => padT + (this.hi - v) / (this.hi - this.lo) * ph;
    const x = t => padL + (t - t0) / tSpan * pw;
    // Grid: values at a 1-2-5 step, time from the start of the ride.
    ctx.font = `13px ${C.font}`; ctx.textBaseline = "middle"; ctx.lineWidth = 1;
    const step = nice((this.hi - this.lo) / 5);
    ctx.fillStyle = C["ink-soft"]; ctx.textAlign = "right";
    for (let v = Math.ceil(this.lo / step) * step; v <= this.hi; v += step) {
      const yy = Math.round(y(v)) + 0.5;
      ctx.strokeStyle = Math.abs(v) < step / 2 ? C["grid-major"] : C.grid;
      ctx.beginPath(); ctx.moveTo(padL, yy); ctx.lineTo(padL + pw, yy); ctx.stroke();
      ctx.fillText(tick(v, step), padL - 6, yy);
    }
    const tStep = [0.5, 1, 2, 5, 10, 20].find(s => s * pw / tSpan >= 52) || 20; // Labels at least 52 px apart.
    ctx.textAlign = "center"; ctx.textBaseline = "alphabetic";
    for (let s = 0; s <= tSpan + 1e-9; s += tStep) {
      const xx = Math.round(padL + s / tSpan * pw) + 0.5;
      ctx.strokeStyle = C.grid;
      ctx.beginPath(); ctx.moveTo(xx, padT); ctx.lineTo(xx, padT + ph); ctx.stroke();
      ctx.fillText(`${num(s)} s`, xx, h - 6);
    }
    if (!idx.length) {
      ctx.fillStyle = C["ink-soft"]; ctx.textAlign = "center"; ctx.textBaseline = "middle";
      ctx.fillText(empty || "Czekam na próbki z płytki", padL + pw / 2, padT + ph / 2);
      return;
    }
    // Traces: per pixel column min/max, so 6000 samples stay cheap.
    const pens = [];
    ctx.save();
    ctx.beginPath(); ctx.rect(padL, padT, pw, ph); ctx.clip();
    ctx.lineWidth = 1.5; ctx.lineJoin = "round";
    for (let a = 0; a < 3; a++) {
      ctx.strokeStyle = C[AX[a]];
      ctx.beginPath();
      let col = null, mn = 0, mx = 0, first = true;
      const flush = () => {
        if (col === null) return;
        if (first) { ctx.moveTo(col, y(mx)); first = false; } else ctx.lineTo(col, y(mx));
        ctx.lineTo(col, y(mn));
      };
      for (let j = idx.length - 1; j >= 0; j--) {
        const i = idx[j], v = this.v[a][i] - mean[a];
        const c = Math.round(x(this.t[i]) * 2) / 2;
        if (c !== col) { flush(); col = c; mn = mx = v; } else { if (v < mn) mn = v; if (v > mx) mx = v; }
      }
      flush();
      ctx.stroke();
      const last = this.v[a][idx[0]] - mean[a];
      pens.push({ a, v: last, raw: this.v[a][idx[0]], y: y(last) });
    }
    ctx.restore();
    // Pens: a nib at the trace end and the value beside it, pushed apart.
    pens.sort((p, q) => p.y - q.y);
    const gap = 19, top = padT + 8, bottom = padT + ph - 8;
    for (let i = 0; i < pens.length; i++) pens[i].ly = Math.min(Math.max(pens[i].y, top + i * gap), bottom - (pens.length - 1 - i) * gap);
    for (let i = 1; i < pens.length; i++) if (pens[i].ly - pens[i - 1].ly < gap) pens[i].ly = pens[i - 1].ly + gap;
    const xe = padL + pw;
    ctx.font = `600 15px ${C.font}`; ctx.textAlign = "left"; ctx.textBaseline = "middle";
    for (const p of pens) {
      const col = C[AX[p.a]];
      const py = Math.min(Math.max(p.y, padT), padT + ph);
      ctx.strokeStyle = col; ctx.fillStyle = col; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(xe, py); ctx.lineTo(xe + 10, p.ly); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(xe - 1, py); ctx.lineTo(xe + 6, py - 4); ctx.lineTo(xe + 6, py + 4); ctx.closePath(); ctx.fill();
      ctx.fillText(fmt(removeDc ? p.v : p.raw, this.d), xe + 13, p.ly);
    }
  }
}

const accel = new Strip(document.getElementById("accel"), 2, 0.5);
const gyro = new Strip(document.getElementById("gyro"), 1, 4);
let removeDc = false, latestT = 0;
document.getElementById("dc").addEventListener("change", e => { removeDc = e.target.checked; });

// The last ride: from its start (the tracker counts 1.5 s of pre-roll into
// it) while it lasts, then frozen from the start to the stop (the start of
// the calm that ended it). A ride the page saw only partly starts where its
// samples do.
const PRE_ROLL_S = 1.5;
let ride = null; // { t0, t1 (null while riding), from }
function followRide(ln) {
  if (!ln) return;
  const moving = ln.ph === "move";
  const riding = moving && !ln.arr;
  if (riding && (!ride || (ride.t1 !== null && !ride.sameMove))) {
    ride = { t0: Math.max(sampleT - PRE_ROLL_S, sampleT - (CAP - 1) / 200), t1: null, from: ln.cur, sameMove: true };
  } else if (riding && ride && ride.t1 !== null) {
    ride.t1 = null; // Moved again before the stop was confirmed: the same ride goes on.
  } else if (ride && ride.t1 === null && !riding) {
    ride.t1 = Math.max(ride.t0 + 0.5, sampleT - Math.max(0, ln.t || 0)); // Seconds since the calm began.
  }
  if (ride) ride.sameMove = moving; // Left Moving: the next ride is a new one.
  const info = $("rideInfo");
  if (!ride) return;
  const dur = (ride.t1 ?? sampleT) - ride.t0;
  info.textContent = ride.t1 === null ? `Przejazd w toku od stołu ${tableName(ride.from)}, ${num(dur)} s`
    : `Ostatni przejazd od stołu ${tableName(ride.from)}: ${num(dur)} s, od ruszenia do zatrzymania`;
}

// Connection.
let ws = null, lastMsg = 0, sampleT = 0;
const linkEl = document.getElementById("link"), linkText = document.getElementById("linkText");
const stateEl = document.getElementById("state");
function connect() {
  ws = new WebSocket(`ws://${location.host}/ws`);
  ws.onmessage = ev => {
    lastMsg = performance.now();
    const m = JSON.parse(ev.data);
    if (m.t === "s") onSamples(m);
    else if (m.t === "m") onMove(m);
    else if (m.t === "ln") onLine(m);
  };
  ws.onclose = () => { setLink(false, "Brak połączenia z płytką, ponawiam…"); setTimeout(connect, 1500); };
  ws.onerror = () => ws.close();
}
function setLink(on, text) { linkEl.classList.toggle("on", on); linkText.textContent = text; }
setInterval(() => {
  if (ws && ws.readyState === 1 && performance.now() - lastMsg > 1500) setLink(false, "Płytka nie wysyła danych");
}, 500);

let meterState = "settling", liveCm = [0, 0, 0], totals = [0, 0, 0];
function onSamples(m) {
  if (m.network) {
    const n = m.network;
    document.getElementById("networkStatus").textContent =
      `Sieć: ${n.ssid || "—"} · ${n.message}${n.error_line ? ` (wiersz ${n.error_line})` : ""} · IP: ${n.ip || "—"} · ${n.hostname}.local · Zegar: ${n.clock_ready ? "NTP OK" : "brak czasu NTP"}`;
  }
  if (!m.live) { setLink(false, "Płytka nie widzi czujnika"); }
  else setLink(true, `Na żywo, ${Math.round(m.hz)} Hz`);
  for (const r of m.r) {
    sampleT += (r[0] > 0 && r[0] < 100000 ? r[0] : 5000) / 1e6;
    accel.push(sampleT, r[1], r[2], r[3]);
    gyro.push(sampleT, r[4], r[5], r[6]);
  }
  latestT = sampleT;
  followRide(m.ln);
  meterState = m.st;
  // The bar shows the line: standing, riding or turning.
  const ln = m.ln;
  const riding = ln && ln.ph === "move" && !ln.arr;
  stateEl.textContent = !ln ? "" : ln.turn ? "Obrót" : riding && ln.carry ? "Przenoszony" : riding ? "Jazda"
    : ln.ph === "settle" ? "Uczę się drgań" : !ln.k ? "Szukam stołu" : "Postój";
  stateEl.className = "state " + (riding || (ln && ln.turn) ? "moving" : "");
  liveCm = m.lv; totals = m.tot;
  showReadout();
  if (m.ln) onLineState(m.ln);
}

// Last move.
let move = null, drawProgress = 1, animStart = 0;
const $ = id => document.getElementById(id);
const num = v => v.toFixed(1).replace(".", ",").replace("-", "−");
function showReadout() {
  const live = meterState === "moving";
  $("readout").classList.toggle("live", live);
  const vals = live ? liveCm : move ? move.d : null;
  ["dx", "dy", "dz"].forEach((id, a) => {
    $(id).innerHTML = vals ? `${fmt(vals[a], 1).replace(".", ",")}<small>cm</small>` : "–";
    $("s" + AX[a]).textContent = `suma ${fmt(totals[a], 1).replace(".", ",")} cm`;
  });
}
function onMove(m) {
  const isNew = !move || m.n !== move.n;
  move = m;
  $("moveNo").textContent = `numer ${m.n} od zerowania`;
  $("facts").hidden = false;
  $("fd").textContent = `${num(Math.hypot(...m.d))} cm`;
  $("ft").textContent = `${num(m.dur)} s`;
  $("fv").textContent = `${m.v.toFixed(2).replace(".", ",")} m/s`;
  $("fr").textContent = `${Math.round(m.rot)}°`;
  const note = $("note");
  note.classList.toggle("alert", !!m.tilt);
  note.textContent = m.tilt
    ? "Grawitacja po ruchu nie zgadza się z obrotem z żyroskopu. Ten wynik jest niepewny."
    : "Przesuń czujnik i odłóż go, aby zmierzyć kolejny ruch.";
  showReadout();
  drawMove(isNew && !reduceMotion);
}
function drawMove(animate) {
  if (animate) { animStart = performance.now(); drawProgress = 0; } else drawProgress = 1;
  paintMove();
}
function emptyPlot(cv, text) {
  const { ctx, w, h } = setupCanvas(cv);
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = C["ink-soft"]; ctx.font = `14px ${C.font}`; ctx.textAlign = "center"; ctx.textBaseline = "middle";
  ctx.fillText(text, w / 2, h / 2);
}
function paintMove() {
  if (!move || move.tr.length < 2) {
    emptyPlot($("mvTime"), "Jeszcze nie było ruchu");
    emptyPlot($("mvPath"), "Jeszcze nie było ruchu");
    return;
  }
  const tr = move.tr, n = tr.length, shown = Math.max(2, Math.round(n * drawProgress));
  // Position over time.
  {
    const { ctx, w, h } = setupCanvas($("mvTime"));
    const padL = 40, padR = 10, padT = 8, padB = 22, pw = w - padL - padR, ph = h - padT - padB;
    ctx.clearRect(0, 0, w, h);
    let peak = 0.5;
    for (const p of tr) for (const v of p) peak = Math.max(peak, Math.abs(v));
    const step = nice(peak / 3), range = Math.ceil(peak / step - 1e-9) * step;
    const y = v => padT + (range - v) / (2 * range) * ph, x = k => padL + k / (n - 1) * pw;
    ctx.font = `13px ${C.font}`; ctx.lineWidth = 1; ctx.fillStyle = C["ink-soft"];
    ctx.textAlign = "right"; ctx.textBaseline = "middle";
    for (let v = -range; v <= range + 1e-9; v += step) {
      const yy = Math.round(y(v)) + 0.5;
      ctx.strokeStyle = Math.abs(v) < 1e-9 ? C["grid-major"] : C.grid;
      ctx.beginPath(); ctx.moveTo(padL, yy); ctx.lineTo(padL + pw, yy); ctx.stroke();
      ctx.fillText(tick(v, step), padL - 6, yy);
    }
    ctx.textAlign = "left"; ctx.textBaseline = "alphabetic";
    ctx.fillText("0 s", padL, h - 6);
    ctx.textAlign = "right"; ctx.fillText(`${num(move.dur)} s`, padL + pw, h - 6);
    ctx.lineWidth = 2; ctx.lineJoin = "round";
    for (let a = 0; a < 3; a++) {
      ctx.strokeStyle = C[AX[a]]; ctx.beginPath();
      for (let k = 0; k < shown; k++) { const xx = x(k), yy = y(tr[k][a]); k ? ctx.lineTo(xx, yy) : ctx.moveTo(xx, yy); }
      ctx.stroke();
    }
  }
  // Path in the plane of the two axes it spans most, same scale on both.
  {
    const { ctx, w, h } = setupCanvas($("mvPath"));
    ctx.clearRect(0, 0, w, h);
    const lo = [0, 0, 0], hi = [0, 0, 0];
    for (const p of tr) for (let a = 0; a < 3; a++) { lo[a] = Math.min(lo[a], p[a]); hi[a] = Math.max(hi[a], p[a]); }
    const span = hi.map((v, a) => v - lo[a]);
    const small = span.indexOf(Math.min(...span));
    const ha = small === 0 ? 1 : 0, va = small === 2 ? 1 : 2;
    const pad = 22, aw = w - 2 * pad, ah = h - 2 * pad;
    const s = 0.9 * Math.min(aw / Math.max(span[ha], 1), ah / Math.max(span[va], 1)); // px per cm
    const cx = w / 2, cy = h / 2, mh = (lo[ha] + hi[ha]) / 2, mv = (lo[va] + hi[va]) / 2;
    const px = v => cx + (v - mh) * s, py = v => cy - (v - mv) * s;
    const g = nice(24 / s);
    ctx.lineWidth = 1;
    for (let i = -60; i <= 60; i++) {
      const xx = Math.round(px(i * g)) + 0.5, yy = Math.round(py(i * g)) + 0.5;
      ctx.strokeStyle = i ? C.grid : C["grid-major"];
      if (xx > 0 && xx < w) { ctx.beginPath(); ctx.moveTo(xx, 0); ctx.lineTo(xx, h); ctx.stroke(); }
      if (yy > 0 && yy < h) { ctx.beginPath(); ctx.moveTo(0, yy); ctx.lineTo(w, yy); ctx.stroke(); }
    }
    ctx.strokeStyle = C.ink; ctx.lineWidth = 2; ctx.lineJoin = "round"; ctx.beginPath();
    for (let k = 0; k < shown; k++) { const xx = px(tr[k][ha]), yy = py(tr[k][va]); k ? ctx.lineTo(xx, yy) : ctx.moveTo(xx, yy); }
    ctx.stroke();
    ctx.lineWidth = 2; ctx.strokeStyle = C.ink; ctx.fillStyle = C.paper;
    ctx.beginPath(); ctx.arc(px(0), py(0), 5, 0, 7); ctx.fill(); ctx.stroke();
    const e = tr[shown - 1];
    ctx.fillStyle = C.amber; ctx.beginPath(); ctx.arc(px(e[ha]), py(e[va]), 5, 0, 7); ctx.fill();
    ctx.font = `600 14px ${C.font}`; ctx.textBaseline = "alphabetic";
    ctx.fillStyle = C[AX[ha]]; ctx.textAlign = "right"; ctx.fillText(`${AX[ha].toUpperCase()} →`, w - 6, h - 6);
    ctx.fillStyle = C[AX[va]]; ctx.textAlign = "left"; ctx.fillText(`${AX[va].toUpperCase()} ↑`, 6, 16);
    $("pathCap").textContent = `Tor ruchu w płaszczyźnie ${AX[ha].toUpperCase()}–${AX[va].toUpperCase()}, kratka ${String(g).replace(".", ",")} cm, kółko to start`;
  }
}

$("reset").addEventListener("click", () => {
  if (ws && ws.readyState === 1) { ws.send("reset"); $("resetMsg").textContent = "Suma wyzerowana"; }
  else $("resetMsg").textContent = "Brak połączenia, nie wyzerowano";
  setTimeout(() => { $("resetMsg").textContent = ""; }, 2500);
});

// ---- Line: tables, the car, Wi-Fi fingerprints ----
const SVGNS = "http://www.w3.org/2000/svg";
// Station positions in METRES, station 0 at the origin (no pixel scale
// applied here — the caller scales for drawing). `gaps[i]` is the
// distance (m) from station i to i+1, `gdirDeg[i]` its floor direction in
// degrees CCW (0 = +X), or null when not yet known — treated as 0 deg,
// which reproduces today's straight horizontal line exactly (no special
// fallback branch: CSV-authoritative and auto-learned gdir use this same
// formula).
function stationPositions(gaps, gdirDeg) {
  const pos = [{ x: 0, y: 0 }];
  for (let i = 0; i < gaps.length; i++) {
    const deg = gdirDeg[i] ?? 0;
    const rad = deg * Math.PI / 180;
    const prev = pos[i];
    pos.push({
      x: prev.x + gaps[i] * Math.cos(rad),
      y: prev.y + gaps[i] * Math.sin(rad), // CCW-positive => +Y in this metres-space (Task 2 flips for SVG's y-down screen).
    });
  }
  return pos;
}
function el(tag, attrs, parent) {
  const e = document.createElementNS(SVGNS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  if (parent) parent.appendChild(e);
  return e;
}
const esc = t => String(t).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const macShort = b => b.slice(6).match(/../g).join(":");
const mmss = s => `${Math.floor(s / 60)}:${String(Math.floor(s % 60)).padStart(2, "0")}`;
const similarity = dB => (dB >= 0 ? Math.max(0, Math.min(1, 1 - dB / 25)) : 0); // Mean |ΔRSSI| to 0..1.
const M = 80, TABLE_H = 128, TABLE_W = 92; // Table box size, px; unchanged from today's frame.
const SCALE = 14; // px per metre; a short gap is drawn longer, see TABLE_GAP.
const TABLE_GAP = 96; // px at least between neighbouring table frames (room for the gap's label).
let stationPos = [{ x: 0, y: 0 }]; // Recomputed in buildScene() from lineInfo.
function pos(s) { return stationPos[Math.max(0, Math.min(stationPos.length - 1, s))]; }
// Fractional station index -> 2D point, for the car's live position.
function posAt(t) {
  const a = pos(Math.floor(t)), b = pos(Math.ceil(t)), f = t - Math.floor(t);
  return { x: a.x + (b.x - a.x) * f, y: a.y + (b.y - a.y) * f };
}
// Name from config.csv when there is one, else the table's number.
function tableName(s) { return (lineInfo && lineInfo.ids && lineInfo.ids[s]) || `Stół ${s + 1}`; }
function segDeg(s) { // Floor direction (deg) of the segment ahead of station s, for car rotation.
  const gdir = (lineInfo && lineInfo.gdir && lineInfo.gdir[s]) ?? 0;
  return gdir;
}
const scene = $("scene");
let lineInfo = null, lst = null, selected = -1, sceneN = 0, refs = {};
let carX = null, carDir = 1, wheelAngle = 0, rollerShift = 0, lastFrameTs = 0;

function buildScene(n) {
  sceneN = n;
  scene.innerHTML = "";
  // Normalize to exactly n-1 entries regardless of what lineInfo carries —
  // the onLineState() emergency-fallback path (see onLineState() below)
  // builds a line-info object with gaps:[] and no gdir field at all, so a
  // plain .slice() here would leave both arrays too short (or gdir
  // undefined) and crash/misrender once n > 1.
  const need = Math.max(0, n - 1);
  const gaps = Array.from({ length: need }, (_, i) => (lineInfo && lineInfo.gaps && lineInfo.gaps[i]) || 0);
  const gdir = Array.from({ length: need }, (_, i) => (lineInfo && lineInfo.gdir && lineInfo.gdir[i]) ?? null);
  // A lift shaft has no floor direction: in this top view it is drawn as a
  // short piece going on the way the line came (or X at the start).
  const gz = Array.from({ length: need }, (_, i) => (lineInfo && lineInfo.gz && lineInfo.gz[i]) || 0);
  // A turntable between positions (its gap is an angle): the turned
  // position is drawn beside the line, left of it for a left turn.
  const gr = Array.from({ length: need }, (_, i) => (lineInfo && lineInfo.gr && lineInfo.gr[i]) || 0);
  const drawDir = [];
  let along = 0; // Direction the line came from, for shafts and turntables.
  for (let i = 0; i < need; i++) {
    if (gz[i]) drawDir.push(along);
    else if (gr[i]) drawDir.push(along + (gr[i] > 0 ? 90 : -90));
    else { drawDir.push(gdir[i]); along = gdir[i] ?? along; }
  }
  const metresPos = stationPositions(gaps.map((g, i) => gz[i] || gr[i] ? 7 : g > 0 ? g : 6.5), drawDir);
  // IMPORTANT: Y is negated here (M - ...), not added — stationPositions()
  // returns standard math CCW metres (same convention as arrow()/needle
  // rotation), but SVG y grows downward, so the sign must flip here or the
  // whole layout renders mirrored vs. the car's needle/compass.
  // Each gap to scale, but never so short that two frames come closer than
  // TABLE_GAP (along X the frame's width counts, along Y its height): a
  // schematic, not a map, wherever the line has short gaps.
  stationPos = [{ x: M, y: M }];
  for (let i = 0; i < need; i++) {
    const a = metresPos[i], b = metresPos[i + 1], m = Math.hypot(b.x - a.x, b.y - a.y);
    const ux = m > 1e-6 ? (b.x - a.x) / m : 1, uy = m > 1e-6 ? (b.y - a.y) / m : 0;
    const len = Math.max(m * SCALE, Math.abs(ux) * TABLE_W + Math.abs(uy) * TABLE_H + TABLE_GAP);
    const p = stationPos[i];
    stationPos.push({ x: p.x + ux * len, y: p.y - uy * len }); // SVG y grows downward.
  }
  const xs = stationPos.map(p => p.x), ys = stationPos.map(p => p.y);
  const pad = Math.max(TABLE_W, TABLE_H) / 2 + 50; // Room for table frames and the car sprite above them.
  const minX = Math.min(...xs) - pad, maxX = Math.max(...xs) + pad;
  const minY = Math.min(...ys) - pad, maxY = Math.max(...ys) + pad;
  scene.setAttribute("viewBox", `${minX} ${minY} ${maxX - minX} ${maxY - minY}`);
  refs = { st: [], dims: [], match: [], rollers: [] };
  // Tables: frames the line runs through; each carries its Wi-Fi barcode.
  for (let s = 0; s < n; s++) {
    const p = stationPos[s];
    const g = el("g", { class: "st" }, scene);
    el("rect", { class: "st-frame", x: p.x - TABLE_W / 2, y: p.y - TABLE_H / 2, width: TABLE_W, height: TABLE_H, rx: 4 }, g);
    el("text", { class: "st-name", x: p.x - TABLE_W / 2 + 8, y: p.y - TABLE_H / 2 + 20 }, g).textContent = tableName(s);
    const bars = el("g", { class: "st-bars" }, g);
    const empty = el("text", { class: "st-empty", x: p.x, y: p.y + 12, "text-anchor": "middle" }, g);
    empty.textContent = "bez odcisku";
    g.addEventListener("click", () => { selected = selected === s ? -1 : s; renderFingerprint(); markTables(); });
    refs.st.push({ g, bars, empty, p });
  }
  // Roller conveyor: one short band + dimension line per segment, each
  // rotated to that segment's floor direction (today's single straight
  // band only worked because the line was always drawn straight).
  for (let s = 0; s < n - 1; s++) {
    const a = stationPos[s], b = stationPos[s + 1];
    const deg = -Math.atan2(b.y - a.y, b.x - a.x) * 180 / Math.PI; // As drawn (shafts, turntables too).
    refs.rollers.push(el("line", { class: "rollers", x1: a.x, y1: a.y, x2: b.x, y2: b.y }, scene));
    el("rect", { class: "band", x: a.x - 5, y: a.y - 5, width: Math.hypot(b.x - a.x, b.y - a.y) + 10, height: 10,
                 rx: 5, transform: `rotate(${-deg} ${a.x} ${a.y})` }, scene);
    const mid = { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
    refs.dims.push(el("text", { class: "dim-text", x: mid.x, y: mid.y - 10, "text-anchor": "middle",
                                transform: `rotate(${-deg} ${mid.x} ${mid.y})` }, scene));
  }
  // How much the last scan looks like each table.
  for (let s = 0; s < n; s++) {
    const p = stationPos[s];
    el("rect", { class: "match", x: p.x - 30, y: p.y + 40, width: 60, height: 6, rx: 3 }, scene);
    refs.match.push(el("rect", { class: "match-fill", x: p.x - 30, y: p.y + 40, width: 0, height: 6, rx: 3 }, scene));
  }
  // The part: a small car on the rollers; above it the stop timer or Wi-Fi.
  const car = el("g", { class: "car" }, scene);
  const body = el("g", {}, car);
  el("path", { class: "car-body", d: "M-32,-9 L-32,-17 Q-31,-21 -26,-21 L-15,-22 L-7,-33 Q-5,-35 -1,-35 L13,-35 Q17,-35 20,-30 L25,-22 Q32,-21 33,-15 L33,-9 Z" }, body);
  el("path", { class: "car-glass", d: "M-11,-23 L-5,-31 L4,-31 L4,-23 Z M8,-23 L8,-31 L15,-31 Q17,-31 18,-29 L21,-23 Z" }, body);
  const wheels = [];
  for (const wx of [-19, 19]) {
    const w = el("g", { transform: `translate(${wx},-7)` }, car);
    el("circle", { class: "wheel", r: 7.5 }, w);
    const hub = el("g", {}, w);
    el("line", { class: "hub", x1: -4, y1: 0, x2: 4, y2: 0 }, hub);
    el("line", { class: "hub", x1: 0, y1: -4, x2: 0, y2: 4 }, hub);
    wheels.push(hub);
  }
  const ring = el("g", { transform: "translate(28,-50)" }, car); // Right of the table name.
  el("circle", { class: "ring-bg", r: 11 }, ring);
  const arc = el("circle", { class: "ring", r: 11, transform: "rotate(-90)", "stroke-dasharray": "0 70" }, ring);
  const rtext = el("text", { class: "ring-text", "text-anchor": "middle", y: 4 }, ring);
  const wifi = el("g", { class: "wifi", transform: "translate(26,-42)" }, car);
  el("path", { d: "M-4,-2 A6,6 0 0 1 4,-2" }, wifi);
  el("path", { d: "M-9,-7 A12,12 0 0 1 9,-7" }, wifi);
  el("path", { d: "M-14,-12 A18,18 0 0 1 14,-12" }, wifi);
  el("circle", { r: 2.2, cy: 2, fill: "var(--live)" }, wifi);
  // Heading of the body on the floor (top view): the arrow points the way
  // the body faced at table 1 when the heading is 0; turntables turn it.
  const compass = el("g", { class: "compass", transform: "translate(-24,-42)" }, car);
  el("circle", { r: 10 }, compass);
  const needle = el("path", { d: "M8,0 L-4,-5 L-1.5,0 L-4,5 Z" }, compass);
  Object.assign(refs, { car, body, wheels, ring, arc, rtext, wifi, compass, needle });
  carX = null;
  renderTables();
}

// Every access point heard, ordered by the table where it is strongest, so
// the barcodes drift along the line the way the Wi-Fi does.
function apOrder() {
  const best = new Map();
  lineInfo.fp.forEach((fp, s) => fp.forEach(([b, r]) => {
    const o = best.get(b);
    if (!o || r > o.r) best.set(b, { s, r });
  }));
  return [...best.entries()].sort((a, b) => a[1].s - b[1].s || b[1].r - a[1].r).map(e => e[0]);
}
// Arrow for a floor direction (deg, CCW, 0 = as the body faced at table 1).
function arrow(d) {
  if (d === null || d === undefined) return "";
  return ["→", "↗", "↑", "↖", "←", "↙", "↓", "↘"][((Math.round(d / 45) % 8) + 8) % 8];
}
const turnWords = d => `${d > 0 ? "+" : "−"}${Math.abs(Math.round(d))}° (${d > 0 ? "w lewo" : "w prawo"})`;
function renderTables() {
  if (!lineInfo || !refs.st) return;
  const order = apOrder();
  refs.st.forEach((r, s) => {
    const fp = lineInfo.fp[s] || [];
    r.g.classList.toggle("no-fp", !fp.length);
    r.empty.style.display = fp.length ? "none" : "";
    r.bars.innerHTML = "";
    if (fp.length) {
      const { p } = r;
      const map = new Map(fp), x0 = p.x - 40, bw = 80 / Math.max(order.length, 1);
      order.forEach((b, i) => {
        const v = map.get(b);
        if (v === undefined) return;
        const h = Math.max(1.5, (v + 95) / 65 * 32);
        // y relative to the station's own p.y (not an absolute scene
        // coordinate) so the bars stay inside their tile on non-horizontal
        // segments too; 72 keeps the bars at the same spot in the tile as
        // before on a horizontal line, where p.y was always M (=80).
        el("rect", { class: "st-bar", x: x0 + i * bw + 0.3, y: p.y + 72 - h, width: Math.max(1, bw - 0.6), height: h }, r.bars);
      });
    }
    // Vibration bars + tile tint are independent of the Wi-Fi fingerprint
    // (fp) — they must render whenever vib data exists, even on stations
    // that have no fingerprint yet (see finding 10).
    const VIB_SCALE = 2.0; // m/s2 RMS at "full bar" — placeholder ceiling, retune from real recordings.
    // ponytail: VIB_SCALE is a hardcoded constant, not computed from data —
    // the spec explicitly says thresholds aren't established yet; retune this
    // one number once real transit RMS values are in from real hardware runs,
    // no code change needed elsewhere since every tile reads the same constant.
    const vib = (lineInfo.vib && lineInfo.vib[s]) || [0, 0, 0];
    const vibColors = ["var(--x)", "var(--y)", "var(--z)"];
    const { p } = r; // Per-table position stored on refs.st[s].p since Task 2.
    const bw2 = 6, gap2 = 2, x0b = p.x - (bw2 * 3 + gap2 * 2) / 2;
    vib.forEach((v, i) => {
      const h = Math.max(1, Math.min(28, (v / VIB_SCALE) * 28));
      el("rect", { class: "vib-bar", x: x0b + i * (bw2 + gap2), y: p.y + 30 - h, width: bw2, height: h, fill: vibColors[i] }, r.bars);
    });
    const maxVib = Math.max(...vib) / VIB_SCALE;
    r.g.querySelector(".st-frame").style.fill =
      `color-mix(in srgb, var(--paper) ${Math.round(100 - Math.min(1, maxVib) * 70)}%, var(--warn))`;
  });
  refs.dims.forEach((t, i) => {
    const g = lineInfo.gaps[i] || 0;
    const z = (lineInfo.gz || [])[i] || 0, r = (lineInfo.gr || [])[i] || 0;
    const a = arrow((lineInfo.gdir || [])[i]);
    if (r) t.textContent = `obrót ${r > 0 ? "↺" : "↻"} ${Math.round(g)}°`;
    else if (z) t.textContent = `winda ${z > 0 ? "↑" : "↓"} ${num(g)} m`;
    else t.textContent = (g > 0 ? `${num(g)} m` : "≈ 6,5 m") + (a ? ` ${a}` : "");
    t.classList.toggle("learned", g > 0);
  });
  const sc = lineInfo.score || [];
  let best = -1;
  sc.forEach((v, s) => { if (v >= 0 && (best < 0 || v < sc[best])) best = s; });
  refs.match.forEach((r, s) => {
    r.setAttribute("width", 60 * similarity(sc[s] ?? -1));
    r.classList.toggle("best", s === best && (lineInfo.scan || []).length > 0);
  });
  markTables();
}
function currentTable() {
  if (!lst || !lst.k || (lst.ph === "move" && !lst.arr)) return -1;
  return Math.round(lst.pos);
}
function markTables() {
  if (!refs.st) return;
  const here = currentTable();
  refs.st.forEach((r, s) => {
    r.g.classList.toggle("here", s === here);
    r.g.classList.toggle("sel", s === selected);
  });
}
function renderFingerprint() {
  if (!lineInfo) return;
  const here = currentTable();
  const s = selected >= 0 ? selected : here >= 0 ? here : Math.max(0, lineInfo.cur);
  const fp = lineInfo.fp[s] || [], scan = new Map(lineInfo.scan || []);
  $("fpTitle").textContent = `Odcisk Wi-Fi stołu ${s + 1}${selected < 0 ? "" : " (wybrany)"}`;
  const list = $("fpList");
  if (!fp.length) {
    list.innerHTML = `<p class="empty" style="grid-column:1/-1">Ten stół nie ma jeszcze odcisku. Powstanie po 15 s postoju detalu na nim (jeśli w config.csv ma wifi_scan = true). Kliknij inny stół, aby zobaczyć jego odcisk.</p>`;
    return;
  }
  const pct = v => Math.max(0, Math.min(100, (v + 95) / 65 * 100));
  list.innerHTML = fp.slice(0, 10).map(([b, r]) => {
    const name = lineInfo.names[b], now = scan.get(b);
    return `<span class="fp-name${name ? "" : " mac"}" title="${b}">${esc(name || macShort(b))}</span>` +
      `<span class="fp-track"><span class="fp-fill" style="width:${pct(r)}%"></span>` +
      (now !== undefined ? `<span class="fp-now" style="left:${pct(now)}%"></span>` : "") +
      `</span><span class="fp-db">${fmt(Math.round(r), 0)} dBm</span>`;
  }).join("");
}
function renderFacts() {
  const n = lineInfo.n, sc = lineInfo.score || [];
  const fpN = lineInfo.fp.filter(f => f.length).length, gapN = lineInfo.gaps.filter(g => g > 0).length;
  let best = -1;
  sc.forEach((v, s) => { if (v >= 0 && (best < 0 || v < sc[best])) best = s; });
  const scan = lineInfo.scan || [];
  const rows = [
    ["Stół liczony", "z ruchu: przejechana odległość względem odstępów z config.csv; obroty, windy i ruchy w Z to punkty odniesienia"],
    ["Odciski Wi-Fi", `${fpN} z ${n} stołów`],
    ["Odstępy z config.csv", `${gapN} z ${n - 1}`],
    ["Ostatni skan", scan.length ? (best >= 0 ? `${scan.length} sieci, najbliżej stołu ${best + 1} (zgodność ${Math.round(similarity(sc[best]) * 100)}%)` : `${scan.length} sieci`) : "jeszcze nie było"],
    ["Stół z Wi-Fi", lineInfo.fix ? "ustalony przez Wi-Fi przy ostatnim postoju" : "nie (Wi-Fi szuka stołu tylko, gdy jest nieznany)"],
    ["Kierunki odcinków", (lineInfo.gdir || []).map(d => arrow(d) || "–").join(" ") || "–"],
  ];
  $("lineFacts").innerHTML = rows.map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`).join("");
}
// Shallow array-equality check (treats missing/undefined as empty), used
// to detect gaps/gdir changes that need a scene rebuild (see onLine()).
function arrChanged(a, b) {
  a = a || []; b = b || [];
  if (a.length !== b.length) return true;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return true;
  return false;
}
function onLine(m) {
  const prevGaps = lineInfo && lineInfo.gaps, prevGdir = lineInfo && lineInfo.gdir;
  const prevGz = lineInfo && lineInfo.gz, prevGr = lineInfo && lineInfo.gr, prevIds = lineInfo && lineInfo.ids;
  lineInfo = m;
  if (sceneN !== m.n || arrChanged(prevGaps, m.gaps) || arrChanged(prevGdir, m.gdir) || arrChanged(prevGz, m.gz) || arrChanged(prevGr, m.gr) ||
      arrChanged(prevIds, m.ids)) buildScene(m.n); else renderTables();
  if (selected >= m.n) selected = -1;
  renderFingerprint();
  renderFacts();
  renderWhere();
}
function onLineState(ln) {
  const tableBefore = currentTable();
  lst = ln;
  if (!lineInfo) onLine({ n: ln.n, cur: ln.cur, k: ln.k, fix: 0, gaps: [], fp: [], scan: [], score: [], names: {} });
  renderWhere();
  renderDetector();
  if (currentTable() !== tableBefore) { markTables(); renderFingerprint(); }
}
// Ride detector: how much ride-like motion there is now against the
// threshold that follows the shaking of the structure.
function renderDetector() {
  if (lst.thr === undefined) return;
  const scale = Math.max(2 * lst.thr, lst.act, 0.01);
  const fill = $("actFill");
  fill.style.width = `${Math.min(100, lst.act / scale * 100)}%`;
  fill.classList.toggle("over", lst.act > lst.thr);
  $("actTick").style.left = `${lst.thr / scale * 100}%`;
  const c = (v, d) => v.toFixed(d).replace(".", ",");
  $("actText").textContent = `Ruch rozpoznawany powyżej ${c(lst.thr, 3)} m/s, teraz ${c(lst.act, 3)} m/s. ` +
    `Drgania w postoju ${c(lst.shk, 3)} m/s², próg zatrzymania ${c(lst.sthr, 3)} m/s². Progi dopasowują się same do drgań konstrukcji.`;
}
function renderWhere() {
  if (!lst) return;
  const where = $("where"), sub = $("whereSub");
  let main, text = "", moving = false;
  if (lineInfo && lineInfo.cfg === 0) { // No valid config.csv: nothing is tracked.
    main = "Brak prawidłowej konfiguracji";
    text = `config.csv${lineInfo.cfgLine ? `, wiersz ${lineInfo.cfgLine}` : ""}: ${lineInfo.cfgErr}. ` +
      "Wgraj poprawny plik na dysk USB WIBROMTR i naciśnij RESET na płytce.";
  } else if (lst.carry && !lst.arr) {
    moving = true;
    main = "Przenoszony";
    text = "Ruch nie wygląda na jazdę po rolkach (przechył, obrót). Po odstawieniu stół rozpozna Wi-Fi.";
  } else if (!lst.k) {
    // Why the table is not known: 1 after power-up, 2 samples lost, 3 carried.
    main = lst.lost === 3 ? "Przeniesiony, szukam stołu" : "Pozycja nieznana";
    const why = lst.lost === 3 ? "Detal przeniesiono w inne miejsce. " : lst.lost === 2 ? "W trakcie jazdy zgubiono próbki. " : "";
    text = why + (lst.scan ? "Skanuję Wi-Fi, aby rozpoznać stół…"
      : lst.wait >= 0 ? `Odcisk Wi-Fi za ${Math.ceil(lst.wait)} s rozpozna stół.`
      : lst.ph === "park" ? "Odcisk Wi-Fi nie wskazał stołu jednoznacznie, ponowię skan za chwilę. Stół ustali też punkt odniesienia (obrót, winda, ruch w Z jedynej takiej stacji) albo przycisk stołu 1."
      : "Po postoju odcisk Wi-Fi rozpozna stół. Stół ustali też punkt odniesienia (obrót, winda, ruch w Z jedynej takiej stacji) albo przycisk stołu 1.");
  } else if (lst.ph === "move" && !lst.arr) {
    moving = true;
    main = `Jedzie ze stołu ${lst.cur + 1} na ${lst.cur + 1 + lst.dir}`;
    text = `${num(lst.d)} m od stołu ${lst.cur + 1}`;
  } else if (lst.ph === "settle") {
    main = `${tableName(lst.cur)} (${lst.cur + 1} z ${lst.n})`;
    text = "Czekam, aż detal stanie nieruchomo";
  } else {
    main = `${tableName(Math.round(lst.pos))} (${Math.round(lst.pos) + 1} z ${lst.n})`;
    if (lst.turn) text = `Obraca się na stole: ${turnWords(lst.tn)}`;
    else if (lst.ltS >= 0 && lst.ltS < 30) text = `Obrócony o ${turnWords(lst.lt)}` + (lst.wait >= 0 ? `, odcisk Wi-Fi za ${Math.ceil(lst.wait)} s` : "");
    else if (lst.scan) text = "Skanuję Wi-Fi, aby zapisać i porównać odcisk stołu…";
    else if (lst.wait >= 0) text = `Postój ${mmss(lst.t)}, odcisk Wi-Fi za ${Math.ceil(lst.wait)} s`;
    else text = `Postój ${mmss(lst.t)}` + (lineInfo && lineInfo.fix ? ", stół ustalony przez Wi-Fi" : "");
  }
  where.textContent = main;
  where.classList.toggle("moving", moving);
  sub.textContent = text;
  sub.classList.toggle("fix", !!(lineInfo && lineInfo.fix && !moving && lst.wait < 0 && !lst.scan));
}
function animateLine(ts) {
  const dt = lastFrameTs ? Math.min(0.1, (ts - lastFrameTs) / 1000) : 0;
  lastFrameTs = ts;
  if (!refs.car || !lst) return;
  const target = posAt(Math.max(0, Math.min(sceneN - 1, lst.pos)));
  const before = carX ?? target;
  const k = Math.min(1, dt * 4);
  carX = carX === null || reduceMotion ? target : { x: before.x + (target.x - before.x) * k, y: before.y + (target.y - before.y) * k };
  const dx = Math.hypot(carX.x - before.x, carX.y - before.y), moving = lst.ph === "move" && !lst.arr;
  if (moving && lst.dir) carDir = lst.dir;
  wheelAngle += dx / 7.5 * 57.3;
  rollerShift -= dx;
  const floorDeg = segDeg(Math.floor(lst.pos));
  refs.car.setAttribute("transform", `translate(${carX.x.toFixed(1)},${carX.y.toFixed(1)}) rotate(${-floorDeg})`);
  refs.body.setAttribute("transform", carDir < 0 ? "scale(-1,1)" : "");
  refs.wheels.forEach(h => h.setAttribute("transform", `rotate(${wheelAngle.toFixed(1)})`));
  refs.rollers.forEach(r => r.style.strokeDashoffset = rollerShift.toFixed(1));
  refs.car.classList.toggle("moving", moving);
  refs.car.style.opacity = lst.k ? 1 : 0.3;
  const waiting = lst.wait >= 0 && !lst.scan;
  refs.ring.style.display = waiting ? "" : "none";
  if (waiting) {
    refs.arc.setAttribute("stroke-dasharray", `${(1 - lst.wait / 15) * 69.1} 70`);
    refs.rtext.textContent = Math.ceil(lst.wait);
  }
  refs.compass.style.display = lst.hk ? "" : "none";
  // refs.compass is a child of refs.car, which is already rotated by
  // -floorDeg above; lst.hd is an ABSOLUTE heading (0 = as the body faced
  // at table 1, see the comment on the compass's construction in
  // buildScene()), so rotating the needle by -hd here double-applies the
  // segment direction (total would be -floorDeg + -hd instead of -hd).
  // Subtract floorDeg so the two rotations compose back to the absolute
  // heading: -floorDeg + -(hd - floorDeg) = -hd.
  refs.needle.setAttribute("transform", `rotate(${(-(lst.hd - floorDeg)).toFixed(1)})`); // CCW on the floor, SVG y points down.
  refs.compass.classList.toggle("turning", !!lst.turn);
  refs.wifi.style.display = lst.scan ? "" : "none";
  refs.wifi.classList.toggle("on", !!lst.scan);
}
$("home").addEventListener("click", () => { if (ws && ws.readyState === 1) ws.send("home"); });
$("forget").addEventListener("click", () => {
  if (confirm("Usunąć odciski Wi-Fi wszystkich stołów? Powstaną od nowa przy kolejnych postojach.") && ws && ws.readyState === 1) ws.send("forgetwifi");
});

function frame(ts) {
  animateLine(ts);
  if (ride) {
    const t1 = ride.t1 ?? latestT;
    accel.draw(ride.t0, t1, removeDc);
    gyro.draw(ride.t0, t1, removeDc);
  } else {
    accel.draw(latestT - 1, latestT, removeDc, "Jeszcze nie było przejazdu");
    gyro.draw(latestT - 1, latestT, removeDc, "Jeszcze nie było przejazdu");
  }
  if (drawProgress < 1) { drawProgress = Math.min(1, (ts - animStart) / 700); paintMove(); }
  requestAnimationFrame(frame);
}
window.addEventListener("resize", () => { paintMove(); });
paintMove();
connect();
requestAnimationFrame(frame);
</script>
</body>
</html>
)HTML";
