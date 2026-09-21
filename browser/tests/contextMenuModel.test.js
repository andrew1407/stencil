import { test } from 'node:test';
import assert from 'node:assert';

// The flyout placement rules, on numbers alone — the DOM half (measure, write styles) is
// ui/nav.js, and the behaviour it rests on is pinned here.
import { submenuPlacement, samePoint, SUB_GAP, SUB_EDGE_PAD, SUB_MIN_INSET } from '../js/ui/contextMenu/model.js';

const row = (left, right, top) => ({ left, right, top });

test('a flyout opens to the RIGHT of its row when there is room', () => {
  const { left, top } = submenuPlacement(row(100, 300, 200), 180, 120, 1000, 800);
  assert.strictEqual(left, 300 + SUB_GAP);
  assert.strictEqual(top, 200, 'aligned with the row it grew from');
});

test('it flips to the LEFT rather than cross the right edge, pad included', () => {
  // 900 + 2 + 180 = 1082, past 1000 - 6, so the flyout goes to the row's left instead.
  const { left } = submenuPlacement(row(700, 900, 100), 180, 120, 1000, 800);
  assert.strictEqual(left, 700 - 180 - SUB_GAP);
  // One pixel of room on the right is still the right: 814 + 180 = 994, exactly the limit.
  assert.strictEqual(submenuPlacement(row(600, 812, 100), 180, 120, 1000, 800).left, 814);
});

test('a flyout taller than the room below is lifted, never pushed off the top', () => {
  const tall = submenuPlacement(row(10, 100, 700), 180, 400, 1000, 800);
  assert.strictEqual(tall.top, 800 - 400 - SUB_EDGE_PAD, 'lifted to sit inside the bottom edge');
  const huge = submenuPlacement(row(10, 100, 700), 180, 2000, 1000, 800);
  assert.strictEqual(huge.top, SUB_MIN_INSET, 'taller than the viewport: pinned at the top inset');
});

test('neither side is ever placed inside the window frame', () => {
  const { left } = submenuPlacement(row(0, 0, 0), 500, 50, 300, 800);
  assert.strictEqual(left, SUB_MIN_INSET, 'wider than the viewport: pinned at the left inset');
});

test('samePoint is a strict pixel comparison, and null is never "the same"', () => {
  assert.strictEqual(samePoint({ x: 3, y: 4 }, { x: 3, y: 4 }), true);
  assert.strictEqual(samePoint({ x: 3, y: 4 }, { x: 3, y: 5 }), false);
  assert.strictEqual(samePoint(null, { x: 3, y: 4 }), false, 'nothing placed ⇒ no idle reference');
  assert.strictEqual(samePoint({ x: 3, y: 4 }, null), false);
});
