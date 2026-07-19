import { test } from 'node:test';
import assert from 'node:assert';
import { layoutLineLengthCm } from '../js/core/units.js';

// A4 portrait: 21 × 29.7 cm. Canvas 210 × 297 px ⇒ exactly 0.1 cm/px on both axes.
const A4 = (lines) => ({
  pageSize: 'A4', imageWidth: 210, imageHeight: 297, lines,
});

test('layoutLineLengthCm: single horizontal segment in cm (0.1 cm/px)', () => {
  const len = layoutLineLengthCm(A4([{ points: [{ x: 0, y: 0 }, { x: 100, y: 0 }] }]));
  assert.ok(Math.abs(len - 10) < 1e-9, `expected 10 cm, got ${len}`);
});

test('layoutLineLengthCm: sums every segment across all lines', () => {
  const len = layoutLineLengthCm(A4([
    { points: [{ x: 0, y: 0 }, { x: 100, y: 0 }, { x: 100, y: 100 }] }, // 10 + 10
    { points: [{ x: 0, y: 0 }, { x: 30, y: 40 }] },                     // hypot(3,4) = 5
  ]));
  assert.ok(Math.abs(len - 25) < 1e-9, `expected 25 cm, got ${len}`);
});

test('layoutLineLengthCm: named size swaps to landscape when the image is wider than tall', () => {
  // 297 × 210 px canvas (landscape) ⇒ A4 dims swap to 29.7 × 21 cm ⇒ still 0.1 cm/px.
  const len = layoutLineLengthCm({
    pageSize: 'A4', imageWidth: 297, imageHeight: 210,
    lines: [{ points: [{ x: 0, y: 0 }, { x: 0, y: 100 }] }],
  });
  assert.ok(Math.abs(len - 10) < 1e-9, `expected 10 cm, got ${len}`);
});

test('layoutLineLengthCm: custom page passes through without a landscape swap', () => {
  const len = layoutLineLengthCm({
    pageSize: 'custom', customPageWidth: 50, customPageHeight: 25,
    imageWidth: 500, imageHeight: 250,
    lines: [{ points: [{ x: 0, y: 0 }, { x: 100, y: 0 }] }],
  });
  assert.ok(Math.abs(len - 10) < 1e-9, `expected 10 cm, got ${len}`);
});

test('layoutLineLengthCm: 0 when nothing measurable', () => {
  assert.equal(layoutLineLengthCm(null), 0);
  assert.equal(layoutLineLengthCm(A4([])), 0);
  assert.equal(layoutLineLengthCm({ pageSize: 'A4', lines: [{ points: [{ x: 0, y: 0 }, { x: 9, y: 9 }] }] }), 0); // no image dims
  assert.equal(layoutLineLengthCm(A4([{ points: [{ x: 5, y: 5 }] }])), 0); // single point ⇒ no segment
});
