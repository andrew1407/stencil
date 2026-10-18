// The clock turn (js/ui/cli/clock.js), the cases of cli/src/console/render/logoFx/clock.zig.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { cellAngle, sweptSpansAt, iconDegrees, iconSpans, ICON_COLS } from '../../../js/ui/cli/clock.js';

const near = (a, b) => assert.ok(Math.abs(a - b) < 0.001, `${a} ≈ ${b}`);

test('cellAngle: a clock face — up is 0, then right, down, left; rows count double', () => {
  near(cellAngle(9, 10, 10, 10), 0);
  near(cellAngle(10, 11, 10, 10), 90);
  near(cellAngle(11, 10, 10, 10), 180);
  near(cellAngle(10, 9, 10, 10), 270);
  near(cellAngle(9, 12, 10, 10), 45);
  assert.equal(cellAngle(10, 10, 10, 10), 0);
  assert.ok(cellAngle(9, 8, 10, 10) > 270);
});

test('sweptSpansAt: a quadrant at a time, wrapping over 12 o\'clock at the end', () => {
  assert.deepEqual(sweptSpansAt(5, 11, 5, 6, 0), []);
  assert.deepEqual(sweptSpansAt(5, 11, 5, 6, 360), [[0, 11]]);
  assert.deepEqual(sweptSpansAt(5, 11, 5, 6, 90.001), [[5, 11]]);
  const half = sweptSpansAt(7, 11, 5, 6, 180);
  assert.equal(half.length, 1);
  assert.ok(half[0][0] >= 5 && half[0][1] === 11);
  const late = sweptSpansAt(3, 11, 5, 6, 330);
  assert.equal(late.length, 2);
  assert.equal(late[0][0], 0);
  assert.ok(late[0][1] < late[1][0]);
  assert.equal(late[1][1], 11);
  assert.deepEqual(sweptSpansAt(5, 0, 5, 6, 180), []);
});

test('iconDegrees: one turn over the seam\'s travel at 1.25×, never backwards; iconSpans hands the rest to the seam', () => {
  assert.equal(iconDegrees(0, 40), 0);
  assert.equal(iconDegrees(20, 40), 225);
  assert.equal(iconDegrees(32, 40), 360);
  assert.equal(iconDegrees(999, 40), 360);
  assert.equal(iconDegrees(0, 0), 360);
  let prev = 0;
  for (let x = 0; x <= 40; x++) { const d = iconDegrees(x, 40); assert.ok(d >= prev && d <= 360); prev = d; }
  assert.deepEqual(iconSpans(1, 10, 80, 0, 0), []);
  const mid = iconSpans(5, 10, 80, 90, 30);
  assert.deepEqual(mid.at(-1), [ICON_COLS, 30]);
  for (const [, c1] of mid.slice(0, -1)) assert.ok(c1 <= ICON_COLS);
  assert.deepEqual(iconSpans(5, 10, 80, 360, 80), [[0, ICON_COLS], [ICON_COLS, 80]]);
});
