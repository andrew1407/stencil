// Unit tests for the Alt-hover export preview (js/ui/exportPreview.js).
//
// REGRESSION: the tip's <img> gets a data: URL synchronously, but the browser still
// decodes it asynchronously — the very FIRST time the tip is shown, place() measured
// its box before that decode landed, so it laid out at whatever tiny/zero size an
// empty <img> reports and (with no dust flight playable at that size either) was easy
// to miss entirely. Only a later re-hover, reusing the by-then-decoded/cached image,
// ever looked right. Fixed by re-placing once the image's own `load` fires.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom } from './helpers/dom.js';

// A window that actually keeps its own keydown/keyup/blur listeners — wireAltPreview
// installs its Alt tracking there once, so the regression tests below need a real
// dispatch, not the addEventListener-that-does-nothing most suites stub in.
const winListeners = {};
const win = {
  innerWidth: 100, innerHeight: 100,
  addEventListener(t, fn) { (winListeners[t] ||= []).push(fn); },
  removeEventListener(t, fn) {
    const a = winListeners[t] || [];
    const i = a.indexOf(fn);
    if (i >= 0) a.splice(i, 1);
  },
  dispatch(t, ev = {}) { for (const fn of [...(winListeners[t] || [])]) fn(ev); },
};
const doc = installDom({}, { window: win });

// document.createElement('canvas') needs a working 2D context stand-in for the
// preview's own thumbnail render (renderThumbDataUrl); every other element gets
// the real stub factory plus a minimal tag-only querySelector (the stub's default
// always returns null), since exportPreview.js reaches into the tip for its <img>.
const origCreateElement = doc.createElement;
doc.createElement = (tag) => {
  if (tag === 'canvas') {
    return { width: 0, height: 0, getContext: () => ({ drawImage() {} }), toDataURL: () => 'data:image/png;base64,x' };
  }
  const el = origCreateElement(tag);
  el.querySelector = (sel) => el.children.find((c) => c.tagName === sel.toUpperCase()) ?? null;
  return el;
};

const { showExportPreview, hideExportPreview, wireAltPreview, clearAltPreviewHover } = await import('../js/ui/exportPreview.js');

const makeApp = () => ({
  image: {},
  export: { renderExportCanvas: () => ({ width: 100, height: 80 }) },
});

const makeItem = (rect = { left: 0, top: 0, right: 40, bottom: 20, width: 40, height: 20 }) => {
  const item = doc.createElement('li');
  item.getBoundingClientRect = () => rect;
  return item;
};

const tipEl = () => doc.body.children.find((c) => c.id === 'ctx-preview-tip');

test('showExportPreview: first-ever show still repositions once the image actually decodes', () => {
  const app = makeApp();
  showExportPreview(app, 'current', 50, 60, { x: 5, y: 5 });

  const tip = tipEl();
  assert.ok(tip, 'the preview tip was never created/appended');
  assert.ok(tip.classList.contains('ctx-preview-visible'), 'the tip never became visible');

  // Not yet decoded: place() measured the stub's default 0×0 rect, so the "would
  // overflow the viewport" branch never triggered — it just sat at cursor+18.
  const beforeLoad = tip.style.left;
  assert.equal(beforeLoad, '68px');

  // Decode lands: the box is now big enough to actually overflow the (100×100)
  // viewport from x=50 — a REAL re-place flips it to the left side and clamps.
  tip.getBoundingClientRect = () => ({ left: 0, top: 0, right: 400, bottom: 300, width: 400, height: 300 });
  const img = tip.children.find((c) => c.tagName === 'IMG');
  assert.ok(img, 'no <img> inside the tip');
  img.dispatch('load');

  assert.equal(tip.style.left, '8px', 'the tip never re-placed itself once the image finished decoding');
});

test('showExportPreview: no-ops quietly with no image loaded', () => {
  showExportPreview({ image: null }, 'current', 0, 0, null);
  // Nothing to assert beyond "doesn't throw" — see exportPreview.js's own guard.
});

// REGRESSION: wireAltPreview used to read Alt only off `mousemove` — pressing or
// releasing the key while parked on a row (no further pointer motion) did nothing
// until the next rehover. Alt tracking now rides its own window keydown/keyup.
test('wireAltPreview: pressing Alt while already parked on a row shows the preview — no rehover needed', () => {
  hideExportPreview();
  clearAltPreviewHover();
  const app = makeApp();
  const item = makeItem();
  wireAltPreview(item, app, 'current');

  item.dispatch('mouseenter', { clientX: 10, clientY: 10 });
  assert.equal(tipEl().classList.contains('ctx-preview-visible'), false, 'no Alt held yet — nothing should show on hover alone');

  win.dispatch('keydown', { key: 'Alt' });
  assert.ok(tipEl().classList.contains('ctx-preview-visible'), 'Alt pressed while hovering must show the preview at once');

  win.dispatch('keyup', { key: 'Alt' });
  clearAltPreviewHover();
});

test('wireAltPreview: releasing Alt while still hovering hides the preview — no rehover needed', () => {
  hideExportPreview();
  clearAltPreviewHover();
  const app = makeApp();
  const item = makeItem();
  wireAltPreview(item, app, 'current');

  item.dispatch('mouseenter', { clientX: 10, clientY: 10 });
  win.dispatch('keydown', { key: 'Alt' });
  assert.ok(tipEl().classList.contains('ctx-preview-visible'), 'setup: preview should be showing');

  win.dispatch('keyup', { key: 'Alt' });
  assert.equal(tipEl().classList.contains('ctx-preview-visible'), false, 'Alt released while hovering must hide the preview at once, without needing the pointer to move again');

  clearAltPreviewHover();
});

test('wireAltPreview: window blur (e.g. Alt-tabbing away) drops a stuck-open preview', () => {
  hideExportPreview();
  clearAltPreviewHover();
  const app = makeApp();
  const item = makeItem();
  wireAltPreview(item, app, 'current');

  item.dispatch('mouseenter', { clientX: 10, clientY: 10 });
  win.dispatch('keydown', { key: 'Alt' });
  assert.ok(tipEl().classList.contains('ctx-preview-visible'), 'setup: preview should be showing');

  // Alt-tab delivers no matching keyup — only the blur.
  win.dispatch('blur');
  assert.equal(tipEl().classList.contains('ctx-preview-visible'), false, 'losing focus while Alt is held must not leave the preview stuck open');

  win.dispatch('keyup', { key: 'Alt' });
  clearAltPreviewHover();
});

test('wireAltPreview: moving to another row while Alt stays held swaps the preview straight away', () => {
  hideExportPreview();
  clearAltPreviewHover();
  const app = makeApp();
  const first = makeItem({ left: 0, top: 0, right: 40, bottom: 20, width: 40, height: 20 });
  const second = makeItem({ left: 0, top: 30, right: 40, bottom: 50, width: 40, height: 20 });
  wireAltPreview(first, app, 'current');
  wireAltPreview(second, app, 'original');

  first.dispatch('mouseenter', { clientX: 10, clientY: 10 });
  win.dispatch('keydown', { key: 'Alt' });
  assert.ok(tipEl().classList.contains('ctx-preview-visible'));

  // Real pointer travel: leaving the first row fires before entering the second.
  first.dispatch('mouseleave');
  second.dispatch('mouseenter', { clientX: 10, clientY: 40 });
  assert.ok(tipEl().classList.contains('ctx-preview-visible'), 'the preview should still be showing, now for the row underneath the pointer');

  win.dispatch('keyup', { key: 'Alt' });
  clearAltPreviewHover();
});

test('clearAltPreviewHover: forgets the tracked row, so a stale hover cannot resurrect a closed menu\'s preview', () => {
  hideExportPreview();
  clearAltPreviewHover();
  const app = makeApp();
  const item = makeItem();
  wireAltPreview(item, app, 'current');

  item.dispatch('mouseenter', { clientX: 10, clientY: 10 });
  clearAltPreviewHover();   // e.g. the menu that owned `item` just closed

  win.dispatch('keydown', { key: 'Alt' });
  assert.equal(tipEl().classList.contains('ctx-preview-visible'), false, 'no row is tracked as hovered any more — Alt alone must not show anything');

  win.dispatch('keyup', { key: 'Alt' });
});
