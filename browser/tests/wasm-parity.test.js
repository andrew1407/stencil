// Parity coverage for the WebAssembly core (js/wasm/stencilCore.js, compiled
// from core/). The other JS suites exercise the hand-written fallback
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
import { parseDuration } from '../js/core/durationParser.js';
import { applyContourRGBA } from '../js/core/contourFilter.js';
import constants from '../js/config/constants.json' with { type: 'json' };
import {
  cropAspectJS, centeredCropJS, resizeCropFromCornerJS, moveCropClampedJS,
  cropResizeScaleJS, cropChangeJS, isAlbumOrientationJS, rotateCropRectQuarterJS
} from '../js/core/cropGeometry.js';

// js/wasm/stencilCore.js is a generated artifact (gitignored) — present only after
// the Emscripten build (CI's WASM job, or a local build per core/WASM.md). When
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
  duration: ['days 23', 'fortnight', 'month', '3 weeks', 'off', 'banana', 'days 0', 'days 100000000', 'days 200000000'],
};
const jsRef = {
  dist: CASES.dist.map(c => distToSegment(c.px, c.py, c.a, c.b)),
  formulaApply: CASES.formula.map(([e, v, x]) => fe.apply(e, v, x, true)),
  formulaValidate: CASES.formula.map(([e, v]) => fe.validate(e, v)),
  hex: CASES.hex.map(h => parseHex(h)),
  // Captured at module-eval time (no wasm installed yet), so this is the JS fallback.
  duration: CASES.duration.map(s => parseDuration(s)),
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

// Adversarial / large formula strings must be marshalled over the heap, not the
// fixed ~64KB wasm stack — an oversized cwrap('string') arg used to overflow the
// stack and corrupt the module (crash on the next call). The parser's depth cap
// rejects deep nesting; a long *flat* expression stays valid. Both must agree with
// the JS fallback op-for-op, and none may crash the wasm instance.
wtest('formula: long/adversarial strings marshal over the heap without corrupting wasm', () => {
  const apply = core.op('formulaApply');
  const valid = core.op('formulaValidate');
  const deep = '('.repeat(200000);                                   // past the depth cap
  const balanced = '('.repeat(5000) + 'x' + ')'.repeat(5000);        // deeply nested, balanced
  const flat = '0' + '+1'.repeat(20000);                             // ~40KB but only linear

  // Deep nesting → invalid (identity), matching fe.validate/apply in the JS fallback.
  assert.strictEqual(valid(deep, 'x'), fe.validate(deep, 'x'));
  assert.strictEqual(valid(balanced, 'x'), fe.validate(balanced, 'x'));
  assert.strictEqual(apply(balanced, 'x', 42, true), fe.apply(balanced, 'x', 42, true));
  // A long flat expression is valid on both sides and evaluates to the same number.
  assert.strictEqual(valid(flat, 'x'), fe.validate(flat, 'x'));
  assert.ok(Math.abs(apply(flat, 'x', 0, true) - fe.apply(flat, 'x', 0, true)) < 1e-9);

  // The instance is still healthy after the oversized inputs (no heap corruption).
  assert.strictEqual(valid('x*2+1', 'x'), true);
  assert.strictEqual(apply('x*2+1', 'x', 10, true), 21);
});

wtest('parseDuration: wasm matches JS reference (ms, 0 for off, null for invalid)', () => {
  const fn = core.op('parseDuration');
  CASES.duration.forEach((s, i) => {
    assert.strictEqual(fn(s), jsRef.duration[i], `duration ${i}: ${s}`);
  });
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

wtest('pageFormats: wasm name list equals the PAGE_SIZES table keys (set equality)', () => {
  const names = core.op('pageFormats')().split(' ');
  const keys = Object.keys(constants.PAGE_SIZES);
  assert.deepStrictEqual([...names].sort(), [...keys].sort());
});

wtest('pageDimensions: every named format matches PAGE_SIZES (portrait + landscape swap)', () => {
  const pageDimensions = core.op('pageDimensions');
  for (const [name, ps] of Object.entries(constants.PAGE_SIZES)) {
    assert.deepStrictEqual(pageDimensions(name, 100, 200, 0, 0), { width: ps.width, height: ps.height }, `${name} portrait`);
    assert.deepStrictEqual(pageDimensions(name, 200, 100, 0, 0), { width: ps.height, height: ps.width }, `${name} landscape`);
  }
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

wtest('crop geometry: wasm matches JS reference (CropRect out-pointer marshalling)', () => {
  const isAlbum = core.op('isAlbumOrientation');
  const cropAspect = core.op('cropAspect');
  const centeredCrop = core.op('centeredCrop');
  const resizeCorner = core.op('resizeCropFromCorner');
  const moveCrop = core.op('moveCropClamped');
  const resizeScale = core.op('cropResizeScale');
  const cropChange = core.op('cropChange');
  const A3W = 29.7, A3H = 42.0;
  const rectClose = (a, b) => ['x', 'y', 'width', 'height'].forEach(k =>
    assert.ok(Math.abs(a[k] - b[k]) < 1e-9, `${k}: ${a[k]} ≈ ${b[k]}`));

  assert.strictEqual(isAlbum(200, 100), isAlbumOrientationJS(200, 100));
  assert.ok(Math.abs(cropAspect(A3W, A3H, true) - cropAspectJS(A3W, A3H, true)) < 1e-9);
  rectClose(centeredCrop(100, 200, cropAspectJS(A3W, A3H, false)), centeredCropJS(100, 200, cropAspectJS(A3W, A3H, false)));

  const cur = { x: 10, y: 10, width: 100, height: 70 };
  const aspect = cropAspectJS(A3W, A3H, true);
  rectClose(resizeCorner(cur, 2, 5000, 5000, aspect, 200, 200, 16), resizeCropFromCornerJS(cur, 2, 5000, 5000, aspect, 200, 200, 16));
  rectClose(moveCrop(cur, 9999, 0, 500, 500), moveCropClampedJS(cur, 9999, 0, 500, 500));
  assert.ok(Math.abs(resizeScale(100, 250) - cropResizeScaleJS(100, 250)) < 1e-9);

  const portrait = { x: 0, y: 0, width: 100, height: 141 };
  const album = { x: 0, y: 0, width: 141, height: 100 };
  assert.deepStrictEqual(cropChange(portrait, album), cropChangeJS(portrait, album));

  const rotateCrop = core.op('rotateCropRectQuarter');
  const r = { x: 10, y: 20, width: 80, height: 40 };
  rectClose(rotateCrop(r, 200, 100, true), rotateCropRectQuarterJS(r, 200, 100, true));
  rectClose(rotateCrop(r, 200, 100, false), rotateCropRectQuarterJS(r, 200, 100, false));
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

wtest('applyFilterRGBA invert: flips every channel, alpha preserved', () => {
  const fn = core.op('applyFilterRGBA');
  const data = new Uint8ClampedArray([10, 20, 30, 200, 255, 0, 128, 128]);
  fn('invert', data, 2, 0, 0, 0);   // tint ignored for invert
  assert.deepStrictEqual([...data], [245, 235, 225, 200, 0, 255, 127, 128]);
});

wtest('applyContourRGBA: wasm matches the JS fallback byte-for-byte on a gradient', () => {
  // Small deterministic fixture with distinct horizontal/vertical/diagonal ramps
  // and per-pixel alphas — the pinned integer Sobel must agree exactly.
  const w = 8, h = 6;
  const fixture = () => {
    const d = new Uint8ClampedArray(w * h * 4);
    for (let y = 0; y < h; y++) {
      for (let x = 0; x < w; x++) {
        const p = (y * w + x) * 4;
        d[p] = x * 30;              // horizontal ramp
        d[p + 1] = y * 40;          // vertical ramp
        d[p + 2] = (x * y * 7) % 256;
        d[p + 3] = 100 + x + y;     // distinct alphas (must survive untouched)
      }
    }
    return d;
  };
  const wasmBuf = fixture();
  const jsBuf = fixture();
  core.op('applyContourRGBA')(wasmBuf, w, h);
  applyContourRGBA(jsBuf, w, h);
  assert.deepStrictEqual([...wasmBuf], [...jsBuf]);
  // Sanity: the filter actually ran (a gradient produces some non-255 output).
  assert.ok([...wasmBuf].some((v, i) => i % 4 !== 3 && v !== 255));
});
