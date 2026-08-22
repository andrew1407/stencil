import { test } from 'node:test';
import assert from 'node:assert/strict';

// Placement for the panel's anchored dialogs (src/lib/popover.js) — a PORT of
// browser/js/ui/popover.js popoverPosition. These are the browser suite's placement
// cases (browser/tests/popover.test.js), carried here so the two implementations
// cannot drift apart; keep them in sync when either side's rules change.
import { popoverPosition } from '../src/lib/popover.js';

test('popoverPosition: below the anchor, left-aligned, with the gap', () => {
  const p = popoverPosition({
    anchor: { left: 100, top: 20, bottom: 44 },
    box: { width: 420, height: 300 },
    viewport: { width: 1280, height: 800 },
  });
  assert.deepEqual(p, { left: 100, top: 52 });
});

test('popoverPosition: flips above when the bottom would overflow', () => {
  const p = popoverPosition({
    anchor: { left: 100, top: 700, bottom: 724 },
    box: { width: 420, height: 300 },
    viewport: { width: 1280, height: 800 },
  });
  assert.equal(p.top, 700 - 8 - 300);
});

test('popoverPosition: clamps into the viewport when neither side fits', () => {
  const tall = popoverPosition({
    anchor: { left: 100, top: 300, bottom: 324 },
    box: { width: 420, height: 700 },
    viewport: { width: 1280, height: 760 },
  });
  assert.equal(tall.top, 760 - 8 - 700);
  const huge = popoverPosition({
    anchor: { left: 100, top: 300, bottom: 324 },
    box: { width: 420, height: 900 },
    viewport: { width: 1280, height: 760 },
  });
  assert.equal(huge.top, 8);
});

test('popoverPosition: clamps horizontally at both edges', () => {
  const right = popoverPosition({
    anchor: { left: 1200, top: 20, bottom: 44 },
    box: { width: 420, height: 300 },
    viewport: { width: 1280, height: 800 },
  });
  assert.equal(right.left, 1280 - 8 - 420);
  const left = popoverPosition({
    anchor: { left: 2, top: 20, bottom: 44 },
    box: { width: 420, height: 300 },
    viewport: { width: 1280, height: 800 },
  });
  assert.equal(left.left, 8);
});

test('popoverPosition: a narrow panel viewport (the 400px popup) still fits the 280px dialog', () => {
  // The real surface this port serves: the popup window is ~400px wide and the dialog
  // 280px, so an anchor at the right edge (a row's ⋯ button) must pull the box back in.
  const p = popoverPosition({
    anchor: { left: 360, top: 90, bottom: 112 },
    box: { width: 280, height: 180 },
    viewport: { width: 400, height: 600 },
  });
  assert.equal(p.left, 400 - 8 - 280);
  assert.equal(p.top, 120);
});
