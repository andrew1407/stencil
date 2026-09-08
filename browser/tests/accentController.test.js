// Unit tests for AccentController (js/core/accentController.js) — the theme/accent writes
// extracted out of DrawingApp. State lives on the document element (data-theme / data-accent /
// inline --accent) + localStorage, so we build a minimal stub document + localStorage and
// assert the writes, the preset-vs-custom split, and the cross-tab broadcast (app.tabs).

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { installMemoryStorage } from './helpers/memoryStorage.js';
import {
  needsGlyphShadow, contrastWithWhite, relativeLuminance,
  GLYPH_SHADOW_MIN_CONTRAST, LIGHT_ACCENT_KEYS,
} from '../js/core/accents.js';

// Minimal <html> stand-in: attribute map + a CSS style object supporting the three ops the
// controller uses (setProperty / removeProperty / getPropertyValue).
const attrs = new Map();
const styleProps = new Map();
const docEl = {
  getAttribute: (k) => (attrs.has(k) ? attrs.get(k) : null),
  setAttribute: (k, v) => attrs.set(k, v),
  hasAttribute: (k) => attrs.has(k),
  removeAttribute: (k) => attrs.delete(k),
  // data-accent-light is a bare presence flag (the glyph-shadow switch).
  toggleAttribute: (k, on) => (on ? attrs.set(k, '') : attrs.delete(k)),
  style: {
    setProperty: (k, v) => styleProps.set(k, v),
    removeProperty: (k) => styleProps.delete(k),
    getPropertyValue: (k) => styleProps.get(k) || '',
  },
};
const store = installMemoryStorage()._map;
// The favicon path (applyAccentFavicon → applyFaviconHex) touches document; give it enough
// to run without throwing (querySelector → null → createElement + head.appendChild).
// The theme button, twice over: the fullscreen layer clones the toolbar with its ids and
// never removes the clone, so the page really does carry two of these. getElementById
// hands back the parked clone — the icon has to reach both.
const themeButtons = [{ innerHTML: '' }, { innerHTML: '' }];
globalThis.document = {
  documentElement: docEl,
  getElementById: (id) => (id === 'theme-toggle' ? themeButtons[0] : null),
  querySelectorAll: (sel) => (sel === '[id="theme-toggle"]' ? themeButtons : []),
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

test('the toggle icon reaches every copy of the button, not getElementById\'s first hit', () => {
  reset();
  themeButtons.forEach((b) => { b.innerHTML = ''; });
  new AccentController(makeApp()).setTheme('dark');
  // Dark shows the way BACK to light, so the icon is the sun.
  for (const btn of themeButtons) assert.match(btn.innerHTML, /ic-sun/, 'a stale clone took the icon');
  new AccentController(makeApp()).setTheme('light');
  for (const btn of themeButtons) assert.match(btn.innerHTML, /ic-moon/);
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

test('previewAccent: paints a preset instantly, no persist/broadcast; endAccentPreview reverts to the committed preset', () => {
  reset();
  const app = makeApp();
  const ctrl = new AccentController(app);
  ctrl.setAccent('blue');                       // committed
  assert.deepEqual(app.broadcasts, ['blue']);
  ctrl.previewAccent('pink');                   // hover
  assert.equal(docEl.getAttribute('data-accent'), 'pink', 'the page shows the previewed preset');
  assert.equal(localStorage.getItem('drawingApp_accent'), 'blue', 'preview never persists');
  assert.deepEqual(app.broadcasts, ['blue'], 'preview never broadcasts');
  ctrl.endAccentPreview();
  assert.equal(docEl.getAttribute('data-accent'), 'blue', 'leaving the list restores the committed preset');
});

test('previewAccent: over a committed CUSTOM hex, the inline --accent comes back on revert', () => {
  reset();
  const ctrl = new AccentController(makeApp());
  ctrl.setCustomAccent('#abcdef');              // committed custom
  ctrl.previewAccent('pink');
  assert.equal(docEl.getAttribute('data-accent'), 'pink');
  assert.equal(styleProps.has('--accent'), false, 'the preset preview drops the inline override');
  ctrl.endAccentPreview();
  assert.equal(styleProps.get('--accent'), '#abcdef', 'the custom hex is restored');
});

test('a committed change during a preview supersedes it: endAccentPreview then does nothing', () => {
  reset();
  const ctrl = new AccentController(makeApp());
  ctrl.setAccent('blue');
  ctrl.previewAccent('pink');
  ctrl.setAccent('aqua');                       // a real pick lands mid-hover
  ctrl.endAccentPreview();                       // the trailing revert must be a no-op
  assert.equal(docEl.getAttribute('data-accent'), 'aqua', 'the pick stands; no revert to blue');
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

// ── Glyph-shadow contrast switch (light accents) ────────────────────────────
// White icons/labels sit on --accent; a LIGHT accent washes them out, so the
// controller flags <html data-accent-light> and css/theme.css swaps --glyph-shadow
// from `none` to a dark halo. These assert the threshold and both apply paths.

test('needsGlyphShadow: only accents that white reads poorly on', () => {
  assert.equal(needsGlyphShadow('#00ffff'), true);   // cyan — 1.25:1 against white
  assert.equal(needsGlyphShadow('#ffffff'), true);   // white on white
  assert.equal(needsGlyphShadow('#eab308'), true);   // the yellow preset — 1.92:1
  assert.equal(needsGlyphShadow('#7c3aed'), false);  // violet default — 5.70:1
  assert.equal(needsGlyphShadow('#000000'), false);  // black — maximum contrast
  assert.equal(needsGlyphShadow('nope'), false);     // not a hex → no shadow, no throw
});

test('contrast helpers: white-vs-black anchors and the 3:1 threshold', () => {
  assert.equal(Math.round(contrastWithWhite('#000000') * 100) / 100, 21);
  assert.equal(Math.round(contrastWithWhite('#ffffff') * 100) / 100, 1);
  assert.equal(relativeLuminance('#ffffff'), 1);
  assert.equal(relativeLuminance('#000000'), 0);
  assert.equal(relativeLuminance('nope'), null);
  assert.equal(GLYPH_SHADOW_MIN_CONTRAST, 3);
});

test('LIGHT_ACCENT_KEYS matches prePaintTheme.js\'s inlined copy', () => {
  // prePaintTheme.js is a classic script and cannot import accents.js, so it
  // inlines this list. If a preset's hex changes, these two must move together.
  assert.deepEqual(LIGHT_ACCENT_KEYS, ['yellow', 'sky']);
  const inlined = readFileSync(new URL('../js/prePaintTheme.js', import.meta.url), 'utf8')
    .match(/LIGHT_ACCENT_KEYS = \[([^\]]*)\]/)[1]
    .split(',').map((s) => s.trim().replace(/^'|'$/g, '')).filter(Boolean);
  assert.deepEqual(inlined, LIGHT_ACCENT_KEYS);
});

test('applyAccent / setCustomAccent toggle the data-accent-light flag both ways', () => {
  reset();
  const ctrl = new AccentController(makeApp());

  ctrl.applyAccent('yellow');                                   // light preset → on
  assert.equal(docEl.hasAttribute('data-accent-light'), true);
  ctrl.applyAccent('violet');                                   // dark preset → off again
  assert.equal(docEl.hasAttribute('data-accent-light'), false);

  ctrl.setCustomAccent('#00ffff');                              // light custom → on
  assert.equal(docEl.hasAttribute('data-accent-light'), true);
  ctrl.setCustomAccent('#123456');                              // dark custom → off
  assert.equal(docEl.hasAttribute('data-accent-light'), false);
});
