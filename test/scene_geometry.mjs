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
// today's fixed layout, just computed instead of hardcoded. Units are
// METRES (no pixel scale inside this function).
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
