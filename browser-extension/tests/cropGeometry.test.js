// The extension's crop geometry (src/lib/cropGeometry.js).
// The six functions ported from browser/js/core/parse/cropGeometry.js are pinned to that original by
// portParity.test.js and behaviourally by the browser suite, so none of those run twice here.
// What remains is the extension's own half: the page-format table (a checked-in copy that
// dataParity.test.js pins to config/constants.json) and the three helpers built on it.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  roundRect, pageDims, pageSizeLabel, pageSizeOptions, PAGE_SIZES, DEFAULT_PAGE,
} from '../src/lib/image/cropGeometry.js';

test('roundRect: integers, clamped inside the image', () => {
  assert.deepEqual(roundRect({ x: 10.6, y: 5.2, width: 99.4, height: 70.8 }, 200, 200), { x: 11, y: 5, width: 99, height: 71 });
  const r = roundRect({ x: -5, y: -5, width: 5000, height: 5000 }, 300, 200);
  assert.deepEqual(r, { x: 0, y: 0, width: 300, height: 200 });
});

test('pageDims: named, custom, and unknown → A4', () => {
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
  assert.ok(DEFAULT_PAGE in PAGE_SIZES);
  // Every format is portrait with positive cm dims.
  for (const { width, height } of Object.values(PAGE_SIZES))
    assert.ok(width > 0 && width < height);
});

test('pageSizeLabel: name + bare dims, no unit word; unknown names pass through', () => {
  assert.equal(pageSizeLabel('A4'), 'A4 (21 × 29.7)');
  assert.equal(pageSizeLabel('B5'), 'B5 (17.6 × 25)');
  assert.equal(pageSizeLabel('nope'), 'nope');
});

test('pageSizeOptions: one labelled <option> per named format, canonical order', () => {
  const html = pageSizeOptions();
  assert.ok(html.startsWith('<option value="A0">A0 (84.1 × 118.9)</option>'));
  assert.ok(html.includes('<option value="A4">A4 (21 × 29.7)</option>'));
  assert.equal((html.match(/<option /g) || []).length, Object.keys(PAGE_SIZES).length);
});
