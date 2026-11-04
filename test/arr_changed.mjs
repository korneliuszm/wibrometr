// test/arr_changed.mjs
// node test/arr_changed.mjs
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

// Pull arrChanged() out of the page's inline <script> by source text, same
// approach as scene_geometry.mjs, so the test can't drift from the real fn.
const page = readFileSync(new URL("../src/WebPage.h", import.meta.url), "utf8");
const match = page.match(/function arrChanged\([^]*?\n\}/);
assert.ok(match, "arrChanged() must exist in src/WebPage.h");
const arrChanged = new Function(`${match[0]}; return arrChanged;`)();

assert.equal(arrChanged(null, null), false, "null vs null (both empty) is unchanged");
assert.equal(arrChanged(undefined, []), false, "undefined vs [] is unchanged");
assert.equal(arrChanged([1, 2], [1, 2]), false, "equal arrays are unchanged");
assert.equal(arrChanged([1, 2], [1, 3]), true, "differing element is a change");
assert.equal(arrChanged([1, 2], [1, 2, 3]), true, "differing length is a change");
assert.equal(arrChanged([null, 90], [null, 90]), false, "equal gdir arrays (incl. null entries) are unchanged");
assert.equal(arrChanged([null, 90], [0, 90]), true, "null vs 0 is a change (not learned vs learned)");

console.log("all ok");
