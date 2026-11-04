# Dashboard: geometria trasy + mapa drgań — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the dashboard's fixed straight-line layout with a 2D map
driven by the line's real geometry (segment directions/distances, already
streamed as `gaps[]`/`gdir[]`), and add per-table vibration mini-bars and
background-color intensity. Remove the transit-history table and the live
accel/gyro charts — they are not needed once the map shows vibration.

**Architecture:** Almost entirely frontend (`src/WebPage.h`'s inline JS):
a pure geometry function replaces the fixed-grid `sx(s)` math, consuming
fields the backend already sends. The one backend addition is a new `vib`
array in `WebLink.cpp`'s existing `publishLine()` JSON, backed by a new
`float stationVibMax[maxStations][3]` field on `line::Tracker`, filled
from vibration data `finish()` already computes (RMS/peak) but does not
yet keep per-station.

**Tech Stack:** Same as Plan A — C++17 firmware, inline vanilla JS/SVG
dashboard (`src/WebPage.h`), no build step, no new dependencies, `node`
(already in the PlatformIO toolchain) for the one geometry test.

**Spec:** `docs/superpowers/specs/2026-10-28-skid-dashboard-geometry-design.md`

**Depends on:** Plan A Task 3 only for the *meaning* of `gaps[]`/`gdir[]`
being CSV-authoritative; this plan's code reads those two arrays exactly
as already wired in `publishLine()` (`src/WebLink.cpp:178-186`) and works
identically whether the values come from CSV or auto-learning. This plan
can be implemented and tested independently of Plan A landing first.

## Global Constraints

- No new JS dependencies, no build step — `src/WebPage.h` stays a single
  self-contained `R"HTML(...)"` string, per the existing house rule
  ("Strona jest w `src/WebPage.h` ... działa offline").
- `gdir[s] == null` (not yet learned / no CSV) must produce the exact same
  straight horizontal line the dashboard shows today — one formula, no
  separate fallback branch.
- Vibration color/bar scale is one shared constant across the whole
  dashboard (not per-table normalized), per the spec's comparability
  requirement.
- `.here` / `.sel` (current table / clicked table) stay outline/ring
  styling; vibration intensity is background-color only — the two must
  never be the same visual channel.
- Remove, don't just hide: the transit-history table and the live
  accel/gyro chart sections are deleted from the HTML/CSS/JS, not
  `display: none`'d — this is a scope cut, not a toggle.

---

### Task 1: Pure geometry function + its test

**Files:**
- Modify: `src/WebPage.h` (add `stationPositions()` near the existing
  `const SVGNS = ...` / `sx` definitions, `src/WebPage.h:689-701`)
- Create: `test/scene_geometry.mjs`

**Interfaces:**
- Produces: `function stationPositions(gaps, gdirDeg)` returning
  `[{x,y}, ...]` (one entry per station, station 0 at the origin) —
  consumed by Task 2 (`buildScene`/`renderTables`/car placement) and
  directly tested here via Node, independent of the SVG/DOM code around it.

- [ ] **Step 1: Write the failing test**

```js
// test/scene_geometry.mjs
// node test/scene_geometry.mjs
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

// Extract stationPositions() out of the page's inline <script> by source
// text (no DOM/bundler available for this file) rather than re-typing the
// function here — a copy would drift from the real implementation.
const page = readFileSync(new URL("../src/WebPage.h", import.meta.url), "utf8");
const match = page.match(/function stationPositions\([^]*?\n\}/);
assert.ok(match, "stationPositions() must exist in src/WebPage.h");
const stationPositions = new Function(`${match[0]}; return stationPositions;`)();

// Straight line, all gaps in +X (gdir null -> treated as 0 deg): same as
// today's fixed layout, just computed instead of hardcoded.
{
  const pos = stationPositions([6, 7], [null, null]);
  assert.equal(pos.length, 3);
  assert.deepEqual(pos[0], { x: 0, y: 0 });
  assert.ok(Math.abs(pos[1].x - 6) < 1e-6 && Math.abs(pos[1].y) < 1e-6);
  assert.ok(Math.abs(pos[2].x - 13) < 1e-6 && Math.abs(pos[2].y) < 1e-6);
}

// A turn: segment 0 goes +X (0 deg), segment 1 goes +Y (90 deg, CCW).
{
  const pos = stationPositions([6, 7], [0, 90]);
  assert.ok(Math.abs(pos[1].x - 6) < 1e-6 && Math.abs(pos[1].y) < 1e-6);
  assert.ok(Math.abs(pos[2].x - 6) < 1e-6 && Math.abs(pos[2].y - 7) < 1e-6);
}

// A single station (no gaps) must not throw and must sit at the origin.
{
  const pos = stationPositions([], []);
  assert.equal(pos.length, 1);
  assert.deepEqual(pos[0], { x: 0, y: 0 });
}

console.log("all ok");
```

- [ ] **Step 2: Run test to verify it fails**

Run: `node test/scene_geometry.mjs`
Expected: assertion failure — `stationPositions() must exist in
src/WebPage.h`.

- [ ] **Step 3: Write the implementation**

In `src/WebPage.h`, right after `const SVGNS = "http://www.w3.org/2000/svg";`
(line 689) and before the existing `const U = 140, M = 80, H = 214, ROAD = 104;`
(line 700, which Task 2 removes), add:

```js
const SCALE = 14; // px per metre.
// Station positions in metres->px, station 0 at the origin. `gaps[i]` is the
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
      x: prev.x + gaps[i] * SCALE * Math.cos(rad),
      y: prev.y - gaps[i] * SCALE * Math.sin(rad), // SVG y grows downward; CCW-positive stays CCW on screen.
    });
  }
  return pos;
}
```

Note: the test's assertions use metres directly (not `*SCALE` scaled) by
calling `stationPositions([6, 7], ...)` and checking `pos[1].x - 6`; fix
this by having the test multiply expected values by `SCALE`, OR — simpler,
taken here — keep `stationPositions` taking gaps already in metres and
returning **metres**, and apply `SCALE` only where positions are drawn
(Task 2). Remove `* SCALE` from the function above; Task 2's `pos()`
wrapper applies the SVG scale. (This keeps the tested function unit-free
and the test's plain-metre assertions correct as written.)

- [ ] **Step 4: Run test to verify it passes**

Run: `node test/scene_geometry.mjs`
Expected: `all ok`.

- [ ] **Step 5: Commit**

```bash
git add src/WebPage.h test/scene_geometry.mjs
git commit -m "feat: pure geometry function for station positions from gaps+gdir"
```

---

### Task 2: Replace the fixed-grid scene with the geometry map

**Files:**
- Modify: `src/WebPage.h` (`buildScene()` and `renderTables()`,
  `src/WebPage.h:700-831` per the current file; exact line numbers will
  have shifted slightly after Task 1's insert — search for the same
  function names)

**Interfaces:**
- Consumes: `stationPositions(gaps, gdirDeg)` from Task 1.
- Produces: `pos(s)` (2D point for station `s`, in SVG px, used by car
  placement in this task and by Task 3's vibration tile rendering),
  reusing the existing `refs.st[]`/`refs.dims[]` structures so
  `renderTables()`'s existing Wi-Fi-fingerprint code (lines 788-819,
  untouched by this plan) keeps working unchanged.

- [ ] **Step 1: Replace `sx`/`U`/`M`/`H`/`ROAD` with geometry-driven layout**

Remove `const U = 140, M = 80, H = 214, ROAD = 104;` and `const sx = s => M + s * U;`
(lines 700-701). Replace with:

```js
const M = 80, TABLE_H = 128, TABLE_W = 92; // Table box size, px; unchanged from today's frame.
let stationPos = [{ x: 0, y: 0 }]; // Recomputed in buildScene() from lineInfo.
function pos(s) { return stationPos[Math.max(0, Math.min(stationPos.length - 1, s))]; }
// Fractional station index -> 2D point, for the car's live position.
function posAt(t) {
  const a = pos(Math.floor(t)), b = pos(Math.ceil(t)), f = t - Math.floor(t);
  return { x: a.x + (b.x - a.x) * f, y: a.y + (b.y - a.y) * f };
}
function segDeg(s) { // Floor direction (deg) of the segment ahead of station s, for car rotation.
  const gdir = (lineInfo && lineInfo.gdir && lineInfo.gdir[s]) ?? 0;
  return gdir;
}
```

- [ ] **Step 2: Rewrite `buildScene(n)` to lay out from geometry, not a grid**

Replace the body of `buildScene(n)` (`src/WebPage.h:706-770` in the
current file) with:

```js
function buildScene(n) {
  sceneN = n;
  scene.innerHTML = "";
  const gaps = lineInfo ? lineInfo.gaps.slice(0, n - 1) : new Array(Math.max(0, n - 1)).fill(0);
  const gdir = lineInfo ? lineInfo.gdir.slice(0, n - 1) : new Array(Math.max(0, n - 1)).fill(null);
  const metresPos = stationPositions(gaps.map(g => g > 0 ? g : 6.5), gdir);
  stationPos = metresPos.map(p => ({ x: M + p.x * SCALE, y: M + p.y * SCALE })); // metres -> SVG px.
  const xs = stationPos.map(p => p.x), ys = stationPos.map(p => p.y);
  const pad = Math.max(TABLE_W, TABLE_H) / 2 + 50; // Room for table frames and the car sprite above them.
  const minX = Math.min(...xs) - pad, maxX = Math.max(...xs) + pad;
  const minY = Math.min(...ys) - pad, maxY = Math.max(...ys) + pad;
  scene.setAttribute("viewBox", `${minX} ${minY} ${maxX - minX} ${maxY - minY}`);
  refs = { st: [], dims: [], match: [] };
  // Tables: frames the line runs through; each carries its Wi-Fi barcode
  // and (Task 3) its vibration mini-bars and background tint.
  for (let s = 0; s < n; s++) {
    const p = stationPos[s];
    const g = el("g", { class: "st" }, scene);
    el("rect", { class: "st-frame", x: p.x - TABLE_W / 2, y: p.y - TABLE_H / 2, width: TABLE_W, height: TABLE_H, rx: 4 }, g);
    el("text", { class: "st-name", x: p.x - TABLE_W / 2 + 8, y: p.y - TABLE_H / 2 + 20 }, g).textContent = `Stół ${s + 1}`;
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
    const deg = segDeg(s);
    el("line", { class: "rollers", x1: a.x, y1: a.y, x2: b.x, y2: b.y }, scene);
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
  const ring = el("g", { transform: "translate(28,-50)" }, car);
  el("circle", { class: "ring-bg", r: 11 }, ring);
  const arc = el("circle", { class: "ring", r: 11, transform: "rotate(-90)", "stroke-dasharray": "0 70" }, ring);
  const rtext = el("text", { class: "ring-text", "text-anchor": "middle", y: 4 }, ring);
  const wifi = el("g", { class: "wifi", transform: "translate(26,-42)" }, car);
  el("path", { d: "M-4,-2 A6,6 0 0 1 4,-2" }, wifi);
  el("path", { d: "M-9,-7 A12,12 0 0 1 9,-7" }, wifi);
  el("path", { d: "M-14,-12 A18,18 0 0 1 14,-12" }, wifi);
  el("circle", { r: 2.2, cy: 2, fill: "var(--live)" }, wifi);
  const compass = el("g", { class: "compass", transform: "translate(-24,-42)" }, car);
  el("circle", { r: 10 }, compass);
  const needle = el("path", { d: "M8,0 L-4,-5 L-1.5,0 L-4,5 Z" }, compass);
  Object.assign(refs, { car, body, wheels, ring, arc, rtext, wifi, compass, needle });
  carX = null;
  renderTables();
}
```

(`car.wheels`/`ring`/`wifi`/`compass` markup is copied unchanged from the
existing `buildScene` — Task 2 only changes layout math and frame/band
placement, not the car sprite itself.)

- [ ] **Step 3: Update the car-placement code to use `posAt`/rotation**

Find the existing animation code that reads `carX`/sets the car's
`transform` from `sx(pos)` (search `src/WebPage.h` for `carX =` and
`car.setAttribute("transform"` — it is in the per-frame render function
near `onLine(m)`, `src/WebPage.h:868` onward). Replace the position
calculation:

```js
// Before: const targetX = sx(lst.pos); ... car.setAttribute("transform", `translate(${carX},${ROAD - 5})`);
// After:
const target = posAt(lst.pos);
// ...smoothing logic stays the same shape, just on {x,y} instead of a single carX...
car.setAttribute("transform", `translate(${carX.x},${carX.y}) rotate(${-segDeg(Math.floor(lst.pos))})`);
```

Apply the same smoothing (whatever easing/lerp the existing code used for
`carX`, e.g. `carX += (targetX - carX) * k`) independently to `carX.x` and
`carX.y`. Keep wheel-spin and roller-shift animation logic as is — those
depend on distance travelled, not on 2D position.

- [ ] **Step 4: Update `renderTables()`'s dimension-line text to use the new `refs.dims`**

`renderTables()` (lines 788-819) already writes `t.textContent` into
`refs.dims[i]` — unchanged, since Step 2 still pushes one dim-text element
per segment in the same order. No code change needed here beyond what
Step 2 already did when rebuilding `refs.dims`.

- [ ] **Step 5: Manual check (no automated DOM test — see note)**

Flash the firmware (or, faster, open `src/WebPage.h`'s HTML in a browser
with a hand-edited `lineInfo`/`lst` stub to skip needing hardware), and
visually confirm:
1. With no `config.csv` and no learned `gdir` yet: straight horizontal
   line, identical to today's screenshot in `docs/accel-screens.png`'s
   sibling line view.
2. With synthetic `gdir = [0, 90]`: an L-shaped line, car turns the corner.
3. Resize the browser window: `viewBox` scales, nothing clips.

No automated check here — Task 1's `scene_geometry.mjs` already covers
the one piece of real logic (the trig loop); the rest of this task is
DOM/SVG wiring with no branches worth a unit test (ladder rung 1: this is
glue code, not logic).

- [ ] **Step 6: Commit**

```bash
git add src/WebPage.h
git commit -m "feat: dashboard draws real line geometry instead of a fixed grid"
```

---

### Task 3: Per-station vibration — backend field + dashboard mini-bars

**Files:**
- Modify: `include/LineTracker.h` (new `stationVibMax_` field, updated in
  `finish()`)
- Modify: `src/WebLink.cpp:178-219` (`publishLine()`: add `vib` to the JSON)
- Modify: `src/WebPage.h` (render mini-bars + tile background)
- Test: extend `test/line_checks.cpp`

**Interfaces:**
- Produces: `float Tracker::stationVib(int s, int axis) const` (consumed
  by `WebLink.cpp`), `lineInfo.vib[s] = [x,y,z]` (consumed by the
  frontend render code in this task).

- [ ] **Step 1: Write the failing test**

Add to `test/line_checks.cpp`:

```cpp
// --- Per-station vibration is recorded on arrival ---
{
  line::Tracker t;
  t.setStations(2);
  t.home();
  check(t.stationVib(0, 0) == 0 && t.stationVib(1, 0) == 0, "no vibration recorded before any ride");
  ride(t, gaps[0], dirs[0] * deg, hz); // existing helper, used by the scenarios above this one
  check(t.stationVib(1, 0) >= 0, "arriving at a table records a non-negative X vibration RMS");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `g++ -std=c++17 -O2 -Iinclude test/line_checks.cpp -o lc`
Expected: compile error, `stationVib` does not exist.

- [ ] **Step 3: Add the field and update it in `finish()`**

In `include/LineTracker.h`'s `private:` section, next to `learned.fp` /
`score_` style per-station arrays:

```cpp
  float stationVibMax_[maxStations][3] = {};
```

Public accessor, next to `float gap(int i) const`:

```cpp
  // Largest RMS vibration (m/s2, high-passed) recorded on any ride that
  // arrived at station `s`, per axis (0=X,1=Y,2=Z). 0 until a ride arrives.
  float stationVib(int s, int axis) const {
    return s >= 0 && s < maxStations && axis >= 0 && axis < 3 ? stationVibMax_[s][axis] : 0;
  }
```

In `finish()`, right after the `if (ride || carried) { ... Transit &t = ...}`
block finishes writing `t.rms[i]`/`t.peak[i]` (after line 826's loop, still
inside the `if (ride || carried)` block, before its closing brace on line
842-ish — same block that already has `for (int i = 0; i < 3; ++i) { t.rms[i] = ...; }`):

```cpp
      if (known_) for (int i = 0; i < 3; ++i) stationVibMax_[station_][i] = std::max(stationVibMax_[station_][i], t.rms[i]);
```

- [ ] **Step 4: Run test to verify it passes**

Run: `g++ -std=c++17 -O2 -Iinclude test/line_checks.cpp -o lc && ./lc`
Expected: `all ok`.

- [ ] **Step 5: Add `vib` to the `publishLine()` JSON**

In `src/WebLink.cpp`, in `publishLine()`, right after the `gdir` array is
written (after line 186's closing `n += snprintf(lineText + n, room - n, "],\"fp\":[");`
— insert the new array just before that line instead, so it reads
`...gdir":[...],"vib":[...],"fp":[...`):

```cpp
  n += snprintf(lineText + n, room - n, "],\"vib\":[");
  for (int s = 0; s < stations; ++s)
    n += snprintf(lineText + n, room - n, "%s[%.3f,%.3f,%.3f]", s ? "," : "",
                  t.stationVib(s, 0), t.stationVib(s, 1), t.stationVib(s, 2));
  n += snprintf(lineText + n, room - n, "],\"fp\":[");
```

(Delete the old standalone `n += snprintf(lineText + n, room - n, "],\"fp\":[");`
line once the combined one above is in place — there must be exactly one.)

- [ ] **Step 6: Render mini-bars and tile background in `src/WebPage.h`**

In `renderTables()` (the function Task 2 Step 4 left otherwise unchanged),
add after the existing Wi-Fi-barcode drawing loop (right before the
closing of the `refs.st.forEach((r, s) => { ... })` block that draws
`r.bars`):

```js
const VIB_SCALE = 2.0; // m/s2 RMS at "full bar" — placeholder ceiling, retune from real recordings.
const vib = (lineInfo.vib && lineInfo.vib[s]) || [0, 0, 0];
const vibColors = ["var(--x)", "var(--y)", "var(--z)"];
const bw = 6, gap = 2, x0 = p.x - (bw * 3 + gap * 2) / 2;
vib.forEach((v, i) => {
  const h = Math.max(1, Math.min(28, (v / VIB_SCALE) * 28));
  el("rect", { class: "vib-bar", x: x0 + i * (bw + gap), y: p.y + 30 - h, width: bw, height: h, fill: vibColors[i] }, r.bars);
});
const maxVib = Math.max(...vib) / VIB_SCALE;
r.g.querySelector(".st-frame").setAttribute("fill",
  `color-mix(in srgb, var(--paper) ${Math.round(100 - Math.min(1, maxVib) * 70)}%, var(--warn))`);
```

(`p` here is the per-table position stored on `refs.st[s].p` from Task 2
Step 2 — destructure it at the top of the `forEach` as `const { p } = r;`.)
`# ponytail: VIB_SCALE is a hardcoded constant, not computed from data —
the spec explicitly says thresholds aren't established yet; retune this
one number once real transit RMS values are in from Plan A's
distances_last_run.csv / the existing transit history, no code change
needed elsewhere since every tile reads the same constant.`

Add the one CSS rule this needs (`src/WebPage.h`'s `<style>`, next to the
existing `.st-bar` rules):

```css
.vib-bar { opacity: 0.9; }
```

- [ ] **Step 7: Commit**

```bash
git add include/LineTracker.h src/WebLink.cpp src/WebPage.h test/line_checks.cpp
git commit -m "feat: per-station vibration mini-bars and tile background tint"
```

---

### Task 4: Remove transit history and live accel/gyro charts

**Files:**
- Modify: `src/WebPage.h` (delete HTML sections, CSS rules, JS functions)

**Interfaces:** None — pure deletion, no other task depends on anything
removed here (Task 1-3 do not touch `#accel`, `#gyro`, `.tr-table`, or
`#trChart`).

- [ ] **Step 1: Delete the HTML sections**

In `src/WebPage.h`'s body, delete the two `<section class="sheet" id="accel">`
and `<section class="sheet" id="gyro">` blocks in full (search for
`id="accel"` and `id="gyro"` — each is one `<section>...</section>` with a
`<canvas>` inside, matching the `#accel canvas`/`#gyro canvas` CSS rules
at lines 100-101). Delete the transit-history section in full: search for
`class="tr-table"` and delete its enclosing `<section>` (it contains the
`<table>` with `id="trBody"`'s `<tbody>` and the `<canvas id="trChart">`
detail view below it).

- [ ] **Step 2: Delete the matching CSS**

Remove these rule blocks from `<style>`: `#accel canvas`/`#gyro canvas`
(lines 100-101 and their `@media (max-width: 600px)` overrides at lines
241-242), and the whole "Transits." block (lines 201-218: `.tr-table`,
`.tr-scroll`, `.tr-detail`, `#trChart`, `.xv`/`.yv`/`.zv`).

- [ ] **Step 3: Delete the JS that feeds those sections**

Search for and delete: the accel/gyro live-chart drawing code (the chart
objects constructed from `$("accel")`/`$("gyro")` — look for where
`new` or a chart-builder function is called with those ids, and the
`onLine`/`onBatch`-style handler code that feeds samples into them), the
`$("trBody").innerHTML = ...` block and its row-click/sparkline helpers
(`publishTransit` handling on the frontend — the JS counterpart of
`weblink::publishTransit()` which Task 1-3 do not touch on the backend;
leaving `publishTransit()` itself in `WebLink.cpp` is fine, the frontend
simply stops listening for `"t":"tr"` messages — find and delete that
`case`/`if` branch in the WebSocket message handler too).

- [ ] **Step 4: Manual check**

Open the dashboard (or the stub HTML from Task 2 Step 5): confirm the page
loads with no console errors (a dangling `$("accel")` reference after
deleting its section throws — if it does, grep for the id across the
whole `<script>` block and remove the remaining reference), and that the
map + Wi-Fi fingerprint + facts sections still render.

No automated check — this task is pure deletion of dead UI, nothing new to
assert (ladder rung 1: removing code needs no new test, only confirmation
nothing else referenced it).

- [ ] **Step 5: Commit**

```bash
git add src/WebPage.h
git commit -m "chore: remove transit history table and live accel/gyro charts from dashboard"
```
