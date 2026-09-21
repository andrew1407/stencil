// The swap origin (js/ui/motion.js): originOf/originOfId take the control actually on
// screen, decline every invisible one, and fall back to the viewport centre.
import test from 'node:test';
import assert from 'node:assert';
import { themeSwap, originOf, originOfId } from '../../js/ui/motion.js';
import { withDoc, rootStub } from '../helpers/motionRig.js';

// The fullscreen layer leaves cloned duplicate ids behind and getElementById returns
// document order, so the swap origin must resolve to the element actually on screen.
test('originOfId skips a hidden duplicate and takes the on-screen one', () => {
  const mk = (w, h, left, top) => ({
    getBoundingClientRect: () => ({ width: w, height: h, left, top }),
  });
  const prior = globalThis.document;
  globalThis.document = {
    querySelectorAll: () => [mk(0, 0, 0, 0), mk(38, 38, 356, 304)],
  };
  try {
    assert.deepEqual(originOfId('theme-toggle'), { x: 375, y: 323 });
  } finally { globalThis.document = prior; }
});

// A rect is not on-screen-ness: `visibility:hidden`, `opacity:0` and a parked clone all
// keep their size.
test('originOf declines a control that is laid out but not visible', () => {
  const rect = () => ({ left: 10, top: 20, width: 40, height: 10 });
  assert.equal(originOf({ getBoundingClientRect: rect, checkVisibility: () => false }), null);
  assert.deepEqual(originOf({ getBoundingClientRect: rect, checkVisibility: () => true }), { x: 30, y: 25 });
});

// A control clipped away by an ancestor's overflow is off screen too, though its own style
// says `visible`.
test('originOf declines a control an ancestor clips away', () => {
  const priorCS = globalThis.getComputedStyle;
  const priorWin = globalThis.window;
  globalThis.window = { innerWidth: 1000, innerHeight: 800 };
  const panel = (h) => ({
    parentElement: null,
    getBoundingClientRect: () => ({ left: 0, top: 0, right: 400, bottom: h, width: 400, height: h }),
    __overflow: 'hidden',
  });
  const btn = (parent) => ({
    parentElement: parent,
    getBoundingClientRect: () => ({ left: 20, top: 120, right: 60, bottom: 152, width: 40, height: 32 }),
    checkVisibility: () => true,
  });
  globalThis.getComputedStyle = (n) => ({ overflow: n.__overflow || 'visible',
                                          overflowX: n.__overflow || 'visible',
                                          overflowY: n.__overflow || 'visible' });
  try {
    assert.equal(originOf(btn(panel(0))), null, 'collapsed to nothing — not on screen');
    assert.deepEqual(originOf(btn(panel(300))), { x: 40, y: 136 }, 'open — the button is real');
  } finally { globalThis.getComputedStyle = priorCS; globalThis.window = priorWin; }
});

test('originOf declines a control parked outside the viewport', () => {
  const prior = globalThis.window;
  globalThis.window = { innerWidth: 300, innerHeight: 400 };
  try {
    assert.equal(originOf({ getBoundingClientRect: () => ({ left: -80, top: 20, width: 40, height: 10 }) }), null);
    assert.equal(originOf({ getBoundingClientRect: () => ({ left: 400, top: 20, width: 40, height: 10 }) }), null);
    assert.deepEqual(originOf({ getBoundingClientRect: () => ({ left: 10, top: 20, width: 40, height: 10 }) }),
      { x: 30, y: 25 });
  } finally { globalThis.window = prior; }
});

// The origin is a control or the centre, never the cursor — the same rule as
// desktop/src/app/MainWindow.cpp.
test('with no control to anchor to, themeSwap blooms from the viewport centre', () => {
  const root = rootStub();
  const doc = { documentElement: root,
    startViewTransition: (cb) => { cb(); return { ready: Promise.resolve(), finished: Promise.resolve() }; } };
  const priorWin = globalThis.window;
  globalThis.window = { innerWidth: 300, innerHeight: 400 };
  try {
    withDoc(doc, () => themeSwap(() => {}));
    assert.equal(root.props['--swap-x'], '50%');
    assert.equal(root.props['--swap-y'], '50%');
    // …and a control always outranks that (275/300, 12/400 of the viewport).
    withDoc(doc, () => themeSwap(() => {}, { x: 275, y: 12 }));
    assert.equal(root.props['--swap-x'], '91.667%');
    assert.equal(root.props['--swap-y'], '3%');
  } finally { globalThis.window = priorWin; }
});
