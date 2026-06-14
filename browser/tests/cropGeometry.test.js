import { test } from 'node:test';
import assert from 'node:assert';
import {
  isAlbumOrientationJS, cropAspectJS, centeredCropJS, resizeCropFromCornerJS,
  moveCropClampedJS, cropResizeScaleJS, cropChangeJS, scaleLinePoints
} from '../js/core/cropGeometry.js';

// A3 page in cm (aspect ≈ √2), matching desktop/tests/cropGeometry.test.cpp.
const A3W = 29.7;
const A3H = 42.0;
const approx = (a, b, eps = 1e-9) => assert.ok(Math.abs(a - b) <= eps, `${a} ≈ ${b}`);

test('isAlbumOrientation is wider-than-tall', () => {
  assert.strictEqual(isAlbumOrientationJS(200, 100), true);
  assert.strictEqual(isAlbumOrientationJS(100, 200), false);
  assert.strictEqual(isAlbumOrientationJS(100, 100), false);
});

test('cropAspect picks width/height for the orientation', () => {
  approx(cropAspectJS(A3W, A3H, false), 29.7 / 42.0);
  approx(cropAspectJS(A3W, A3H, true), 42.0 / 29.7);
  approx(cropAspectJS(A3H, A3W, true), 42.0 / 29.7); // page order ignored
  approx(cropAspectJS(0, 0, true), 1);
});

test('centeredCrop cuts surplus height of a tall portrait image (100x200 → 100x141)', () => {
  const r = centeredCropJS(100, 200, cropAspectJS(A3W, A3H, false));
  approx(r.width, 100);
  approx(r.height, 100 * 42.0 / 29.7);
  approx(r.x, 0);
  approx(r.y, (200 - r.height) / 2);
});

test('centeredCrop cuts surplus width of a wide album image (200x100 → 141x100)', () => {
  const r = centeredCropJS(200, 100, cropAspectJS(A3W, A3H, true));
  approx(r.height, 100);
  approx(r.width, 100 * 42.0 / 29.7);
  approx(r.y, 0);
  approx(r.x, (200 - r.width) / 2);
});

test('resizeCropFromCorner keeps aspect and anchors the opposite corner', () => {
  const aspect = cropAspectJS(A3W, A3H, false);
  const cur = { x: 0, y: 0, width: 100, height: 100 / aspect };
  const r = resizeCropFromCornerJS(cur, 2, 80, 999, aspect, 1000, 1000);
  approx(r.x, 0);
  approx(r.y, 0);
  approx(r.width / r.height, aspect);
});

test('resizeCropFromCorner clamps to image bounds', () => {
  const aspect = cropAspectJS(A3W, A3H, true);
  const cur = { x: 10, y: 10, width: 100, height: 100 / aspect };
  const r = resizeCropFromCornerJS(cur, 2, 5000, 5000, aspect, 200, 200);
  assert.ok(r.x + r.width <= 200 + 1e-9);
  assert.ok(r.y + r.height <= 200 + 1e-9);
  approx(r.width / r.height, aspect);
});

test('resizeCropFromCorner anchors a different corner (top-left drag)', () => {
  const cur = { x: 100, y: 100, width: 100, height: 100 };
  const r = resizeCropFromCornerJS(cur, 0, 150, 150, 1, 1000, 1000);
  approx(r.x + r.width, 200);
  approx(r.y + r.height, 200);
  approx(r.width, 50);
  approx(r.height, 50);
});

test('moveCropClamped keeps the rectangle inside the image', () => {
  const cur = { x: 10, y: 10, width: 100, height: 80 };
  approx(moveCropClampedJS(cur, 20, 30, 1000, 1000).x, 30);
  approx(moveCropClampedJS(cur, -999, -999, 1000, 1000).x, 0);
  approx(moveCropClampedJS(cur, -999, -999, 1000, 1000).y, 0);
  approx(moveCropClampedJS(cur, 9999, 0, 500, 500).x, 400);
});

test('cropResizeScale is the width ratio, guarded against zero', () => {
  approx(cropResizeScaleJS(100, 200), 2);
  approx(cropResizeScaleJS(200, 100), 0.5);
  approx(cropResizeScaleJS(0, 100), 1);
});

test('cropChange flags orientation flip vs reports the resize scale', () => {
  const portrait = { x: 0, y: 0, width: 100, height: 141 };
  const bigger = { x: 0, y: 0, width: 200, height: 282 };
  const album = { x: 0, y: 0, width: 141, height: 100 };

  const resized = cropChangeJS(portrait, bigger);
  assert.strictEqual(resized.orientationChanged, false);
  approx(resized.scale, 2);

  const flipped = cropChangeJS(portrait, album);
  assert.strictEqual(flipped.orientationChanged, true);
  approx(flipped.scale, 1);
});

test('scaleLinePoints multiplies every point in place', () => {
  const lines = [
    { points: [{ x: 10, y: 20 }, { x: 30, y: 40 }] },
    { points: [{ x: 1, y: 2 }] }
  ];
  scaleLinePoints(lines, 1.5);
  approx(lines[0].points[0].x, 15);
  approx(lines[0].points[0].y, 30);
  approx(lines[0].points[1].x, 45);
  approx(lines[1].points[0].x, 1.5);
  approx(lines[1].points[0].y, 3);
});
