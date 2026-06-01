// Parity coverage for the WebAssembly core (js/wasm/stencilCore.js, compiled
// from desktop/core). The other JS suites exercise the hand-written fallback
// path (no wasm ops installed); this one loads the real wasm module and asserts:
//   1. the compiled C++ agrees with the JS reference (so the fallback the other
//      suites test stays a faithful stand-in, and the shipped .js is in sync), and
//   2. the js/core/stencilCore.js marshalling (strings, char codes, flat point
//      arrays, output pointers, the RGBA pixel buffer) round-trips correctly.
// Node loads the SINGLE_FILE ES module directly — no browser, no emcc needed.
import { test, before } from 'node:test';
import assert from 'node:assert';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { core } from '../js/core/stencilCore.js';
import { distToSegment, parseHex } from '../js/utils.js';
import { FormulaEngine } from '../js/core/formulaEngine.js';

// js/wasm/stencilCore.js is a generated artifact (gitignored) — present only after
// the Emscripten build (CI's WASM job, or a local build per desktop/WASM.md). When
// it is missing, skip this whole suite rather than fail the build: the other suites
// already cover the JS reference path that the wasm core mirrors. core.init() does
// the dynamic import internally, so it only touches the artifact when built.
const MODULE_BUILT = existsSync(fileURLToPath(new URL('../js/wasm/stencilCore.js', import.meta.url)));
// Register as skipped (not failed) when the artifact is absent.
const wtest = MODULE_BUILT ? test : test.skip;

// Capture JS-reference results BEFORE wasm is installed (no ops installed, so
// these run the hand-written fallback), then compare against the wasm wrappers.
const fe = new FormulaEngine();
const seg = (px, py, a, b) => ({ px, py, a, b });
const CASES = {
  dist: [seg(5, 3, { x: 0, y: 0 }, { x: 10, y: 0 }), seg(-3, 0, { x: 0, y: 0 }, { x: 10, y: 0 }), seg(14, 7, { x: 2, y: 2 }, { x: 9, y: 5 })],
  formula: [['x+9', 'x', 3], ['2**x', 'x', 3], ['(x-1)*4/2', 'x', 7], ['', 'x', 5], ['x +', 'x', 2]],
  hex: ['#7c3aed', '#000000', '#ffffff', '#0a1b2c', 'nope'],
};
const jsRef = {
  dist: CASES.dist.map(c => distToSegment(c.px, c.py, c.a, c.b)),
  formulaApply: CASES.formula.map(([e, v, x]) => fe.apply(e, v, x, true)),
  formulaValidate: CASES.formula.map(([e, v]) => fe.validate(e, v)),
  hex: CASES.hex.map(h => parseHex(h)),
};

before(async () => {
  if (!MODULE_BUILT) return; // nothing to load; wtest already skipped the suite
  // core.init() dynamically imports the gitignored artifact, so importing the
  // `core` singleton statically (above) is safe even when it hasn't been built.
  const ok = await core.init();
  assert.strictEqual(ok, true, 'wasm core must load in Node (SINGLE_FILE ES module)');
});

wtest('wasm loaded and every core op is installed', () => {
  for (const name of core.opNames) {
    assert.strictEqual(typeof core.op(name), 'function', `core.op('${name}') should be a wasm wrapper`);
  }
});

wtest('distToSegment: wasm matches JS reference', () => {
  const fn = core.op('distToSegment');
  CASES.dist.forEach((c, i) => {
    assert.ok(Math.abs(fn(c.px, c.py, c.a, c.b) - jsRef.dist[i]) < 1e-9, `case ${i}`);
  });
});

wtest('formula apply/validate: wasm matches JS reference (char-code marshalling)', () => {
  const apply = core.op('formulaApply');
  const valid = core.op('formulaValidate');
  CASES.formula.forEach(([e, v, x], i) => {
    assert.ok(Math.abs(apply(e, v, x, true) - jsRef.formulaApply[i]) < 1e-9, `apply ${i}: ${e}`);
    assert.strictEqual(valid(e, v), jsRef.formulaValidate[i], `validate ${i}: ${e}`);
  });
  // allowFormulas=false is identity, exactly like the JS fallback.
  assert.strictEqual(apply('x*2', 'x', 5, false), 5);
});

wtest('parseHex: wasm matches JS reference, invalid yields null (output-pointer marshalling)', () => {
  const fn = core.op('parseHex');
  CASES.hex.forEach((h, i) => {
    const w = fn(h);
    if (h === 'nope') {
      assert.strictEqual(w, null, 'invalid hex → null so utils.parseHex falls back');
    } else {
      assert.deepStrictEqual(w, jsRef.hex[i], `hex ${h}`);
    }
  });
});

wtest('clampScale: wasm matches the JS zoom bound', () => {
  const fn = core.op('clampScale');
  const clampJs = s => Math.max(0.05, Math.min(5, s));
  for (const s of [99, 0.001, 1, 5, 0.05, -3]) {
    assert.strictEqual(fn(s), clampJs(s), `scale ${s}`);
  }
});

wtest('pageDimensions + pixelToPageRaw: wasm matches JS (landscape swap, scaling)', () => {
  const pageDimensions = core.op('pageDimensions');
  const pixelToPageRaw = core.op('pixelToPageRaw');
  // A4 portrait image keeps {21,29.7}; wider-than-tall swaps to landscape.
  assert.deepStrictEqual(pageDimensions('A4', 100, 200, 0, 0), { width: 21, height: 29.7 });
  assert.deepStrictEqual(pageDimensions('A4', 200, 100, 0, 0), { width: 29.7, height: 21 });
  assert.deepStrictEqual(pageDimensions('custom', 100, 100, 12.5, 8), { width: 12.5, height: 8 });
  const dims = { width: 21, height: 29.7 };
  assert.deepStrictEqual(pixelToPageRaw(50, 25, dims, 100, 50), { x: 10.5, y: 14.85 });
});

wtest('shouldCloseShape: wasm matches the JS close gate (flat point array)', () => {
  const fn = core.op('shouldCloseShape');
  const sq = [{ x: 0, y: 0 }, { x: 10, y: 0 }, { x: 10, y: 10 }];
  assert.strictEqual(fn(sq, { x: 2, y: 2 }, 4), true);   // ≥3 pts, within markerSize+8
  assert.strictEqual(fn(sq, { x: 50, y: 50 }, 4), false); // too far
  assert.strictEqual(fn(sq.slice(0, 2), { x: 0, y: 0 }, 4), false); // <3 pts
});

wtest('rotatePoints + boundingBoxCenter: wasm matches JS rotation (in/out array marshalling)', () => {
  const boundingBoxCenter = core.op('boundingBoxCenter');
  const rotatePoints = core.op('rotatePoints');
  const pts = [{ x: 0, y: 0 }, { x: 10, y: 0 }, { x: 10, y: 10 }, { x: 0, y: 10 }];
  assert.deepStrictEqual(boundingBoxCenter(pts), { x: 5, y: 5 });
  // JS reference rotation about (5,5) by 90°.
  const ang = Math.PI / 2;
  const cos = Math.cos(ang), sin = Math.sin(ang);
  const expect = pts.map(p => ({ x: 5 + (p.x - 5) * cos - (p.y - 5) * sin, y: 5 + (p.x - 5) * sin + (p.y - 5) * cos }));
  const got = pts.map(p => ({ ...p }));
  rotatePoints(got, 5, 5, ang);
  got.forEach((p, i) => {
    assert.ok(Math.abs(p.x - expect[i].x) < 1e-9 && Math.abs(p.y - expect[i].y) < 1e-9, `point ${i}`);
  });
});

wtest('applyFilterRGBA custom: grayscale+tint in one pass, alpha preserved (pixel buffer marshalling)', () => {
  const fn = core.op('applyFilterRGBA');
  const data = new Uint8ClampedArray([0, 0, 0, 200, 255, 255, 255, 128]); // black α200, white α128
  fn('custom', data, 2, 124, 58, 237);
  // black (luma 0) → tint color exactly; white (luma 254) → ≈white; alpha untouched.
  assert.deepStrictEqual([data[0], data[1], data[2]], [124, 58, 237]);
  assert.strictEqual(data[3], 200);
  assert.ok(data[4] >= 253 && data[5] >= 253 && data[6] >= 254);
  assert.strictEqual(data[7], 128);
});
