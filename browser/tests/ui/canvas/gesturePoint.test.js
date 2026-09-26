// Where an unanchored confirm grows from (js/ui/canvas/gesturePoint.js), the desktop's
// modalReveal.cpp gestureAnchorRect twin: after a shortcut, the icon its hotkey stands for; that
// icon folded away, null (the flight falls from above); after a press, a box around the press.
import { test } from 'node:test';
import assert from 'node:assert/strict';

const ICON = { left: 820, top: 12, width: 28, height: 24, right: 848, bottom: 36 };
let iconRect = ICON;
let pointerdown = null;
globalThis.document = {
  addEventListener: (type, fn) => { if (type === 'pointerdown') pointerdown = fn; },
  querySelector: (sel) => (sel === '[data-hk-title="clearProject"]' ? { getBoundingClientRect: () => iconRect } : null),
  activeElement: null,
  body: {},
};
globalThis.innerHeight = 900;
const { gestureAnchorRect, noteKeyGesture, GESTURE_ANCHOR_PX } = await import('../../../js/ui/canvas/gesturePoint.js');

test('a shortcut grows its confirm out of the icon its hotkey stands for', () => {
  iconRect = ICON;
  noteKeyGesture('clearProject');
  assert.deepEqual(gestureAnchorRect(), ICON);
});

test('with that icon folded away the flight falls from above, never from a stale press', () => {
  pointerdown({ clientX: 50, clientY: 60 });
  iconRect = { ...ICON, width: 0, height: 0 };
  noteKeyGesture('clearProject');
  assert.equal(gestureAnchorRect(), null);
  noteKeyGesture('noSuchHotkey');
  assert.equal(gestureAnchorRect(), null, 'a hotkey with no icon falls from above too');
});

test('a press after the shortcut takes the origin back to the pointer', () => {
  noteKeyGesture('clearProject');
  pointerdown({ clientX: 50, clientY: 60 });
  const r = gestureAnchorRect();
  assert.equal(r.width, GESTURE_ANCHOR_PX);
  assert.equal(r.left + r.width / 2, 50);
  assert.equal(r.top + r.height / 2, 60);
});
