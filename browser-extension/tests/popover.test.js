// src/lib/popover.js ports ONE function out of browser/js/ui/popover.js — the placement
// math — and portParity.test.js pins it to that original line-for-line. Its placement
// cases are the browser suite's (browser/tests/popover.test.js). What remains here is the
// extension's own geometry: the 400px popup window the port exists to serve.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { popoverPosition } from '../src/lib/popover.js';

test('a narrow panel viewport (the 400px popup) still fits the 280px dialog', () => {
  // An anchor at the right edge (a row's ⋯ button) must pull the box back in.
  const p = popoverPosition({
    anchor: { left: 360, top: 90, bottom: 112 },
    box: { width: 280, height: 180 },
    viewport: { width: 400, height: 600 },
  });
  assert.equal(p.left, 400 - 8 - 280);
  assert.equal(p.top, 120);
});

test('…and a dialog taller than the popup is clamped to the top margin, not flipped off-screen', () => {
  const p = popoverPosition({
    anchor: { left: 20, top: 300, bottom: 322 },
    box: { width: 280, height: 700 },
    viewport: { width: 400, height: 600 },
  });
  assert.equal(p.top, 8);
  assert.equal(p.left, 20);
});
