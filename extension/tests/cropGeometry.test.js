import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  cropAspect, centeredCrop, resizeCropFromCorner, moveCropClamped,
  roundRect, isAlbumOrientation, pageDims, PAGE_SIZES
} from '../src/lib/cropGeometry.js';

test('cropAspect: A3 album vs portrait, invalid → 1', () => {
  assert.ok(Math.abs(cropAspect(29.7, 42, true) - 42 / 29.7) < 1e-9);
  assert.ok(Math.abs(cropAspect(29.7, 42, false) - 29.7 / 42) < 1e-9);
  assert.equal(cropAspect(0, 42, true), 1);
});

test('centeredCrop: centered, inside image, correct aspect', () => {
  const a = 42 / 29.7;
  const c = centeredCrop(1600, 900, a);
  assert.ok(c.width <= 1600 && c.height <= 900);
  assert.ok(Math.abs(c.width / c.height - a) < 1e-9);
  // centered
  assert.ok(Math.abs((1600 - c.width) / 2 - c.x) < 1e-9);
  assert.ok(Math.abs((900 - c.height) / 2 - c.y) < 1e-9);
  assert.deepEqual(centeredCrop(0, 0, a), { x: 0, y: 0, width: 0, height: 0 });
});

test('resizeCropFromCorner: keeps aspect and stays within the image', () => {
  const a = 42 / 29.7;
  const start = centeredCrop(2000, 1200, a);
  const r = resizeCropFromCorner(start, 2, 1500, 1100, a, 2000, 1200);
  assert.ok(Math.abs(r.width / r.height - a) < 1e-6);
  assert.ok(r.x >= 0 && r.y >= 0 && r.x + r.width <= 2000 + 1e-6 && r.y + r.height <= 1200 + 1e-6);
});

test('moveCropClamped: clamps to image bounds', () => {
  const cur = { x: 100, y: 100, width: 400, height: 300 };
  assert.deepEqual(moveCropClamped(cur, -1000, -1000, 2000, 2000), { x: 0, y: 0, width: 400, height: 300 });
  assert.deepEqual(moveCropClamped(cur, 5000, 5000, 2000, 2000), { x: 1600, y: 1700, width: 400, height: 300 });
});

test('roundRect: integers, clamped inside the image', () => {
  assert.deepEqual(roundRect({ x: 10.6, y: 5.2, width: 99.4, height: 70.8 }, 200, 200), { x: 11, y: 5, width: 99, height: 71 });
  const r = roundRect({ x: -5, y: -5, width: 5000, height: 5000 }, 300, 200);
  assert.deepEqual(r, { x: 0, y: 0, width: 300, height: 200 });
});

test('isAlbumOrientation + pageDims (incl. custom)', () => {
  assert.equal(isAlbumOrientation(100, 50), true);
  assert.equal(isAlbumOrientation(50, 100), false);
  assert.deepEqual(pageDims('A4'), PAGE_SIZES.A4);
  assert.deepEqual(pageDims('custom', 30, 40), { width: 30, height: 40 });
});
