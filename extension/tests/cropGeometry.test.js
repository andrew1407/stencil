import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  cropAspect, centeredCrop, resizeCropFromCorner, moveCropClamped, scaleCropCentered,
  roundRect, isAlbumOrientation, pageDims, pageSizeLabel, pageSizeOptions, PAGE_SIZES
} from '../src/lib/cropGeometry.js';
// The editor's table, straight from the same repo — the drift guard below pins the
// extension's hand-copied PAGE_SIZES to it.
import browserConstants from '../../browser/js/config/constants.json' with { type: 'json' };
// The editor's own scaleCropCentered JS reference — the drift guard below pins the
// extension's hand-copied port to it op-for-op (node --test never loads wasm).
import { scaleCropCenteredJS } from '../../browser/js/core/cropGeometry.js';

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

test('scaleCropCentered: grows/shrinks about the centre, keeps aspect, floors at minSize', () => {
  const cur = { x: 60, y: 60, width: 80, height: 80 };   // centre (100,100) in a 200x200 image
  const centre = r => ({ cx: r.x + r.width / 2, cy: r.y + r.height / 2 });

  const bigger = scaleCropCentered(cur, 1.5, 1, 200, 200);
  assert.ok(Math.abs(bigger.width - 120) < 1e-9 && Math.abs(bigger.height - 120) < 1e-9);
  assert.ok(Math.abs(centre(bigger).cx - 100) < 1e-9 && Math.abs(centre(bigger).cy - 100) < 1e-9);

  const smaller = scaleCropCentered(cur, 0.5, 1, 200, 200);
  assert.ok(Math.abs(smaller.width - 40) < 1e-9 && Math.abs(smaller.height - 40) < 1e-9);

  // Floor at minSize (default 16): can't shrink below it.
  const tiny = scaleCropCentered(cur, 0.0001, 1, 200, 200);
  assert.ok(Math.abs(tiny.width - 16) < 1e-9 && Math.abs(tiny.height - 16) < 1e-9);
});

test('scaleCropCentered: off-centre growth is capped by the nearer edge and repositioned in-bounds', () => {
  // Centre (40,100): nearer horizontal edge is 40px away, so width caps at 80 even asked to grow huge.
  const cur = { x: 20, y: 80, width: 40, height: 40 };
  const capped = scaleCropCentered(cur, 100, 1, 200, 200);
  assert.ok(Math.abs(capped.width - 80) < 1e-9 && Math.abs(capped.height - 80) < 1e-9);
  assert.ok(capped.x >= -1e-9 && capped.y >= -1e-9);
  assert.ok(capped.x + capped.width <= 200 + 1e-9 && capped.y + capped.height <= 200 + 1e-9);
});

test('scaleCropCentered: invalid inputs return an untouched copy', () => {
  const cur = { x: 10, y: 10, width: 30, height: 20 };
  assert.deepEqual(scaleCropCentered(cur, 0, 1, 200, 200), cur);   // factor <= 0
  assert.deepEqual(scaleCropCentered(cur, -2, 1, 200, 200), cur);
  assert.deepEqual(scaleCropCentered(cur, 2, 0, 200, 200), cur);   // non-positive aspect
  const zero = { x: 0, y: 0, width: 0, height: 0 };
  assert.deepEqual(scaleCropCentered(zero, 2, 1, 200, 200), zero); // zero size
  assert.notStrictEqual(scaleCropCentered(cur, 0, 1, 200, 200), cur); // a copy, not the same ref
});

test('scaleCropCentered matches the browser editor reference (drift guard)', () => {
  // Same inputs must yield identical rects on both surfaces (aspect, cap, floor, reposition).
  const cases = [
    [{ x: 60, y: 60, width: 80, height: 80 }, 1.5, 1, 200, 200, 16],
    [{ x: 20, y: 80, width: 40, height: 40 }, 100, 1, 200, 200, 16],
    [{ x: 100, y: 50, width: 200, height: 100 }, 0.7, 2, 640, 480, 16],
    [{ x: 5, y: 5, width: 30, height: 45 }, 0.01, 30 / 45, 400, 600, 24]
  ];
  for (const args of cases) assert.deepEqual(scaleCropCentered(...args), scaleCropCenteredJS(...args));
});

test('roundRect: integers, clamped inside the image', () => {
  assert.deepEqual(roundRect({ x: 10.6, y: 5.2, width: 99.4, height: 70.8 }, 200, 200), { x: 11, y: 5, width: 99, height: 71 });
  const r = roundRect({ x: -5, y: -5, width: 5000, height: 5000 }, 300, 200);
  assert.deepEqual(r, { x: 0, y: 0, width: 300, height: 200 });
});

test('isAlbumOrientation + pageDims (incl. custom, unknown → A4)', () => {
  assert.equal(isAlbumOrientation(100, 50), true);
  assert.equal(isAlbumOrientation(50, 100), false);
  assert.deepEqual(pageDims('A4'), PAGE_SIZES.A4);
  assert.deepEqual(pageDims('B5'), PAGE_SIZES.B5);
  assert.deepEqual(pageDims('C5'), PAGE_SIZES.C5);
  assert.deepEqual(pageDims('custom', 30, 40), { width: 30, height: 40 });
  assert.deepEqual(pageDims('nope'), PAGE_SIZES.A4);
});

test('PAGE_SIZES: the full ISO A/B/C table in canonical order', () => {
  const names = [];
  for (const series of ['A', 'B', 'C'])
    for (let i = 0; i <= 10; i++) names.push(`${series}${i}`);
  assert.deepEqual(Object.keys(PAGE_SIZES), names);
  // A3/A4 keep their historical values exactly; spot-check the new series.
  assert.deepEqual(PAGE_SIZES.A3, { width: 29.7, height: 42 });
  assert.deepEqual(PAGE_SIZES.A4, { width: 21, height: 29.7 });
  assert.deepEqual(PAGE_SIZES.A10, { width: 2.6, height: 3.7 });
  assert.deepEqual(PAGE_SIZES.B5, { width: 17.6, height: 25 });
  assert.deepEqual(PAGE_SIZES.C5, { width: 16.2, height: 22.9 });
  assert.deepEqual(PAGE_SIZES.C10, { width: 2.8, height: 4 });
  // Every format is portrait with positive cm dims.
  for (const { width, height } of Object.values(PAGE_SIZES))
    assert.ok(width > 0 && width < height);
});

test('PAGE_SIZES matches the browser editor table exactly (drift guard)', () => {
  assert.deepEqual(PAGE_SIZES, browserConstants.PAGE_SIZES);
});

test('pageSizeLabel: name + cm dims; unknown names pass through', () => {
  assert.equal(pageSizeLabel('A4'), 'A4 (21 × 29.7 cm)');
  assert.equal(pageSizeLabel('B5'), 'B5 (17.6 × 25 cm)');
  assert.equal(pageSizeLabel('nope'), 'nope');
});

test('pageSizeOptions: one labelled <option> per named format, canonical order', () => {
  const html = pageSizeOptions();
  assert.ok(html.startsWith('<option value="A0">A0 (84.1 × 118.9 cm)</option>'));
  assert.ok(html.includes('<option value="A4">A4 (21 × 29.7 cm)</option>'));
  assert.equal((html.match(/<option /g) || []).length, Object.keys(PAGE_SIZES).length);
});
