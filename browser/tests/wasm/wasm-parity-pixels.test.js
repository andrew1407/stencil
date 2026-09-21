// Parity for the wasm core's PIXEL ops (js/wasm/stencilCore.js against the JS reference): the
// RGBA buffer marshalling and the pinned integer Sobel. Split from wasm-parity.test.js.
import { test, before } from 'node:test';
import assert from 'node:assert';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { core } from '../../js/core/abi/stencilCore.js';
import { applyContourRGBA } from '../../js/core/image/contourFilter.js';

// js/wasm/stencilCore.js is a generated, gitignored artifact, present only after the Emscripten build, so the
// suite skips when it is missing: the other suites already cover the JS reference path it mirrors.
const MODULE_BUILT = existsSync(fileURLToPath(new URL('../../js/wasm/stencilCore.js', import.meta.url)));
const wtest = MODULE_BUILT ? test : test.skip;

before(async () => {
  if (!MODULE_BUILT) return; // nothing to load; wtest already skipped the suite
  const ok = await core.init();
  assert.strictEqual(ok, true, 'wasm core must load in Node (SINGLE_FILE ES module)');
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
