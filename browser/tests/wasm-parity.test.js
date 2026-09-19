// Parity coverage for the WebAssembly core (js/wasm/stencilCore.js, compiled from core/). The other JS suites
// exercise the hand-written fallback path; this one loads the real wasm module and asserts that the compiled
// C++ agrees with the JS reference — so the fallback stays a faithful stand-in and the shipped .js is in sync
// — and that the js/core/stencilCore.js marshalling (strings, char codes, flat point arrays, output pointers,
// the RGBA pixel buffer) round-trips. Node loads the SINGLE_FILE ES module directly, with no emcc.
import { test, before } from 'node:test';
import assert from 'node:assert';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { core } from '../js/core/stencilCore.js';
import { distToSegment, parseHex } from '../js/utils.js';
import { FormulaEngine } from '../js/core/formulaEngine.js';
import { parseDuration } from '../js/core/durationParser.js';
import constants from '../js/config/constants.json' with { type: 'json' };

// js/wasm/stencilCore.js is a generated, gitignored artifact, present only after the Emscripten build, so the
// suite skips when it is missing: the other suites already cover the JS reference path it mirrors.
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

// Long formula strings are marshalled over the heap, never the fixed ~64KB wasm stack that an oversized
// cwrap('string') arg overflows; the depth cap rejects deep nesting, a long FLAT expression stays valid.
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
  const clampJs = s => Math.max(0.05, Math.min(32, s));
  for (const s of [99, 0.001, 1, 5, 32, 40, 0.05, -3]) {
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
  assert.strictEqual(fn(sq, { x: 2, y: 2 }, 4), true);   // ≥3 pts, within pointSize+8
  assert.strictEqual(fn(sq, { x: 50, y: 50 }, 4), false); // too far
  assert.strictEqual(fn(sq.slice(0, 2), { x: 0, y: 0 }, 4), false); // <3 pts
});
