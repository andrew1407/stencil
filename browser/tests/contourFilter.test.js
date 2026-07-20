import { test } from 'node:test';
import assert from 'node:assert/strict';
import { applyContourRGBA } from '../js/core/contourFilter.js';

// JS reference for the core's contour (Sobel edge) filter. These cases mirror
// core/tests/imageFilter.test.cpp's applyContourRGBA suite — the two must stay
// byte-identical (the pinned integer-only math).

test('applyContourRGBA maps a uniform image to all white, keeping alpha', () => {
  // 3x2 of one color: every Sobel gradient is 0, so mag 0 → 255 everywhere.
  const buf = new Uint8ClampedArray(6 * 4);
  for (let i = 0; i < 6; i++) buf.set([100, 150, 200, 40 + i], i * 4);
  applyContourRGBA(buf, 3, 2);
  for (let i = 0; i < 6; i++) {
    assert.deepEqual([...buf.slice(i * 4, i * 4 + 3)], [255, 255, 255], `pixel ${i} white`);
    assert.equal(buf[i * 4 + 3], 40 + i, `pixel ${i} alpha preserved`);
  }
});

test('applyContourRGBA marks a hard vertical edge (hand-computed Sobel)', () => {
  // 4x1: black, black, white, white. Luma L = [0, 0, 255, 255]; with the row
  // clamped vertically, gy = 0 and gx = 4 * (l(x+1) - l(x-1)):
  //   x=0: 4*(  0 -   0) =    0 → 255 - 0   = 255
  //   x=1: 4*(255 -   0) = 1020 → mag clamps to 255 → 0
  //   x=2: 4*(255 -   0) = 1020 → 0
  //   x=3: 4*(255 - 255) =    0 → 255  (x+1 clamps to the last column)
  const buf = new Uint8ClampedArray([0, 0, 0, 10, 0, 0, 0, 20, 255, 255, 255, 30, 255, 255, 255, 40]);
  applyContourRGBA(buf, 4, 1);
  assert.deepEqual([...buf], [255, 255, 255, 10, 0, 0, 0, 20, 0, 0, 0, 30, 255, 255, 255, 40]);
});

test('applyContourRGBA computes exact non-saturating magnitudes', () => {
  // 3x1 gray ramp 0, 10, 20: L equals the gray value, gy = 0,
  // gx = 4 * (l(x+1) - l(x-1)) with clamped columns:
  //   x=0: 4*(10 -  0) = 40 → 215
  //   x=1: 4*(20 -  0) = 80 → 175
  //   x=2: 4*(20 - 10) = 40 → 215
  const buf = new Uint8ClampedArray([0, 0, 0, 1, 10, 10, 10, 2, 20, 20, 20, 3]);
  applyContourRGBA(buf, 3, 1);
  assert.deepEqual([...buf], [215, 215, 215, 1, 175, 175, 175, 2, 215, 215, 215, 3]);
});

test('applyContourRGBA tolerates a null buffer / degenerate dimensions', () => {
  applyContourRGBA(null, 3, 3);   // must not crash
  const buf = new Uint8ClampedArray([1, 2, 3, 4]);
  applyContourRGBA(buf, 0, 1);
  applyContourRGBA(buf, 1, -1);
  assert.deepEqual([...buf], [1, 2, 3, 4]);   // degenerate dims → nothing written
});

test('applyContourRGBA turns a 1x1 image white', () => {
  // All clamped neighbors are the pixel itself, so gx = gy = 0 → 255.
  const buf = new Uint8ClampedArray([5, 200, 30, 77]);
  applyContourRGBA(buf, 1, 1);
  assert.deepEqual([...buf], [255, 255, 255, 77]);
});

test('applyContourRGBA luma uses truncating division (integer parity with C++)', () => {
  // 2x1 of r=g=b=1 vs r=g=b=2: L = trunc(10000*c/10000) = c, so
  // gx = 4*(l(1) - l(0)) = 4 with the row clamped → 251 on both pixels.
  const buf = new Uint8ClampedArray([1, 1, 1, 9, 2, 2, 2, 9]);
  applyContourRGBA(buf, 2, 1);
  assert.deepEqual([...buf], [251, 251, 251, 9, 251, 251, 251, 9]);
});
