// Unit tests for AccentController (js/core/accentController.js) — the theme/accent writes
// extracted out of DrawingApp. State lives on the document element (data-theme / data-accent /
// inline --accent) + localStorage, so we build a minimal fake document + localStorage and
// assert the writes, the preset-vs-custom split, and the cross-tab broadcast (app.tabs).

import { test } from 'node:test';
import assert from 'node:assert/strict';

// Minimal <html> stand-in: attribute map + a CSS style object supporting the three ops the
// controller uses (setProperty / removeProperty / getPropertyValue).
const attrs = new Map();
const styleProps = new Map();
const docEl = {
  getAttribute: (k) => (attrs.has(k) ? attrs.get(k) : null),
  setAttribute: (k, v) => attrs.set(k, v),
  style: {
    setProperty: (k, v) => styleProps.set(k, v),
    removeProperty: (k) => styleProps.delete(k),
    getPropertyValue: (k) => styleProps.get(k) || '',
  },
};
const store = new Map();
globalThis.localStorage = {
  getItem: (k) => (store.has(k) ? store.get(k) : null),
  setItem: (k, v) => store.set(k, String(v)),
};
// The favicon path (applyAccentFavicon → applyFaviconHex) touches document; give it enough
// to run without throwing (querySelector → null → createElement + head.appendChild).
globalThis.document = {
  documentElement: docEl,
  getElementById: () => null,
  querySelector: () => null,
  createElement: () => ({}),
  head: { appendChild() {} },
};

const { AccentController } = await import('../js/core/accentController.js');

const makeApp = () => {
  const broadcasts = [];
  return {
    broadcasts,
    tabs: { broadcastAccent: (k) => broadcasts.push(k) },
    // theme getter reads the document element (same as DrawingApp's real getter).
    get theme() { return docEl.getAttribute('data-theme') === 'dark' ? 'dark' : 'light'; },
  };
};

const reset = () => { attrs.clear(); styleProps.clear(); store.clear(); };

test('setTheme: writes data-theme + persists the manual override', () => {
  reset();
  const app = makeApp();
  new AccentController(app).setTheme('dark');
  assert.equal(docEl.getAttribute('data-theme'), 'dark');
  assert.equal(localStorage.getItem('drawingApp_theme'), 'dark');
});

test('setTheme: anything but "dark" resolves to light', () => {
  reset();
  new AccentController(makeApp()).setTheme('whatever');
  assert.equal(docEl.getAttribute('data-theme'), 'light');
});

test('setAccent: applies a valid preset, persists it, and broadcasts to peers', () => {
  reset();
  const app = makeApp();
  new AccentController(app).setAccent('pink');
  assert.equal(docEl.getAttribute('data-accent'), 'pink');
  assert.equal(localStorage.getItem('drawingApp_accent'), 'pink');
  assert.deepEqual(app.broadcasts, ['pink']);
});

test('setAccent: an unknown key falls back to the default (violet)', () => {
  reset();
  const app = makeApp();
  new AccentController(app).setAccent('not-a-preset');
  assert.equal(docEl.getAttribute('data-accent'), 'violet');
  assert.deepEqual(app.broadcasts, ['violet']);
});

test('applyAccent: drops any inline custom override when a preset is applied', () => {
  reset();
  styleProps.set('--accent', '#123456'); // a lingering custom accent
  new AccentController(makeApp()).applyAccent('blue');
  assert.equal(styleProps.has('--accent'), false);
  assert.equal(docEl.getAttribute('data-accent'), 'blue');
});

test('setCustomAccent: normalizes hex + sets inline --accent, no broadcast; invalid → null', () => {
  reset();
  const app = makeApp();
  const ctrl = new AccentController(app);
  assert.equal(ctrl.setCustomAccent('#abc'), '#aabbcc');
  assert.equal(styleProps.get('--accent'), '#aabbcc');
  assert.equal(app.broadcasts.length, 0);           // custom is page-local, never broadcast
  assert.equal(ctrl.setCustomAccent('nope'), null);
});
