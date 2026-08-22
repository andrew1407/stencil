// Tests for src/lib/accent.js — the extension's accent + Appearance store.
//
// It is loaded as a classic <script> in every extension page's <head> BEFORE lib/theme.css,
// so the saved choice is stamped on <html> before first paint (no flash). That makes it
// untestable by import; tests/helpers/accentSandbox.js runs it in a fabricated page instead.
//
// The list it owns is duplicated on purpose in two places that CANNOT import it — the
// service worker / page-bridge side (lib/highlightColor.js ACCENT_HEX) and the browser app
// (canonical rows in browser/js/config/accents.json). The parity tests at the bottom — and
// tests/dataParity.test.js — enforce the sync.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { loadAccent } from './helpers/accentSandbox.js';
import { ACCENT_HEX, ACCENT_STORAGE_KEY, DEFAULT_HL } from '../src/lib/highlightColor.js';
import { THEME_STORAGE_KEY, THEME_MODES } from '../src/lib/shellTheme.js';

// ── The accent list ──

test('publishes the twelve presets, each with a key, label and #rrggbb hex', () => {
  const { accent } = loadAccent();
  assert.equal(accent.list.length, 12);
  const keys = new Set();
  for (const a of accent.list) {
    assert.match(a.key, /^[a-z]+$/, `bad key ${a.key}`);
    assert.ok(a.label && typeof a.label === 'string', `missing label for ${a.key}`);
    assert.match(a.hex, /^#[0-9a-f]{6}$/i, `bad hex for ${a.key}`);
    assert.equal(keys.has(a.key), false, `duplicate key ${a.key}`);
    keys.add(a.key);
  }
});

test('the storage keys are the ones every other surface reads', () => {
  const { accent, theme } = loadAccent();
  assert.equal(accent.storageKey, 'stencil_accent');
  assert.equal(theme.storageKey, 'stencil_theme');
  // The mirrors in lib/highlightColor.js and lib/shellTheme.js must name the same keys.
  assert.equal(accent.storageKey, ACCENT_STORAGE_KEY);
  assert.equal(theme.storageKey, THEME_STORAGE_KEY);
});

test('hexOf resolves a preset, and falls back to the first entry for anything else', () => {
  const { accent } = loadAccent();
  assert.equal(accent.hexOf('sky'), '#0ea5e9');
  assert.equal(accent.hexOf('violet'), '#7c3aed');
  for (const bogus of ['nonsense', '', undefined, null, 'VIOLET']) {
    assert.equal(accent.hexOf(bogus), accent.list[0].hex, `hexOf(${bogus})`);
  }
});

// ── Reading and writing the accent ──

test('defaults to violet with nothing stored, and stamps it before first paint', () => {
  const page = loadAccent();
  assert.equal(page.accent.get(), 'violet');
  assert.equal(page.dataAccent(), 'violet', 'the attribute is set at load, not on first set()');
});

test('a stored accent is honoured at load', () => {
  const page = loadAccent({ stored: { stencil_accent: 'crimson' } });
  assert.equal(page.accent.get(), 'crimson');
  assert.equal(page.dataAccent(), 'crimson');
});

test('a stored value that is not a preset falls back to the default', () => {
  const page = loadAccent({ stored: { stencil_accent: 'chartreuse' } });
  assert.equal(page.accent.get(), 'violet');
  assert.equal(page.dataAccent(), 'violet');
});

test('set() persists, stamps the attribute, and returns the applied key', () => {
  const page = loadAccent();
  assert.equal(page.accent.set('aqua'), 'aqua');
  assert.equal(page.store.get('stencil_accent'), 'aqua');
  assert.equal(page.dataAccent(), 'aqua');
  assert.equal(page.accent.get(), 'aqua');
});

test('set() sanitises an unknown key to the default rather than storing it', () => {
  const page = loadAccent();
  assert.equal(page.accent.set('not-a-colour'), 'violet');
  assert.equal(page.store.get('stencil_accent'), 'violet');
  assert.equal(page.dataAccent(), 'violet');
});

// ── The favicon ──

test('load creates a <link rel="icon"> carrying the accent hex', () => {
  const page = loadAccent({ stored: { stencil_accent: 'sky' } });
  const link = page.faviconLink();
  assert.ok(link, 'a favicon link should have been created');
  assert.equal(link.type, 'image/svg+xml');
  assert.ok(link.href.startsWith('data:image/svg+xml,'), link.href);
  // The accent is painted into the SVG (URL-encoded, so # becomes %23).
  assert.ok(
    decodeURIComponent(link.href).includes('#0ea5e9'),
    'the accent hex should appear in the favicon SVG',
  );
});

test('changing the accent updates the SAME link instead of appending another', () => {
  const page = loadAccent();
  const before = page.headChildren.length;
  page.accent.set('crimson');
  page.accent.set('grass');
  assert.equal(page.headChildren.length, before, 'no extra <link> elements');
  assert.ok(decodeURIComponent(page.faviconLink().href).includes('#16a34a'));
});

// ── Appearance (light / dark / system) ──

test('the three modes are the ones the shell mirrors', () => {
  const { theme } = loadAccent();
  // Spread across the realm boundary: the sandbox's Array has a different prototype,
  // which strict deepEqual counts as a difference.
  assert.deepEqual([...theme.modes], ['system', 'light', 'dark']);
  assert.deepEqual([...theme.modes], THEME_MODES);
});

test('defaults to system, and resolves against the OS', () => {
  const light = loadAccent({ prefersDark: false });
  assert.equal(light.theme.get(), 'system');
  assert.equal(light.theme.resolved(), 'light');
  assert.equal(light.dataTheme(), 'light');

  const dark = loadAccent({ prefersDark: true });
  assert.equal(dark.theme.get(), 'system');
  assert.equal(dark.theme.resolved(), 'dark');
  assert.equal(dark.dataTheme(), 'dark');
});

test('an explicit choice beats the OS in both directions', () => {
  const onDarkOs = loadAccent({ stored: { stencil_theme: 'light' }, prefersDark: true });
  assert.equal(onDarkOs.theme.get(), 'light');
  assert.equal(onDarkOs.dataTheme(), 'light', 'Light must stay light on a dark OS');

  const onLightOs = loadAccent({ stored: { stencil_theme: 'dark' }, prefersDark: false });
  assert.equal(onLightOs.dataTheme(), 'dark', 'Dark must stay dark on a light OS');
});

test('the CHOSEN mode is stored but the RESOLVED one is stamped', () => {
  const page = loadAccent({ prefersDark: true });
  page.theme.set('system');
  assert.equal(page.store.get('stencil_theme'), 'system', 'storage keeps the choice');
  assert.equal(page.dataTheme(), 'dark', 'the attribute carries the resolution');
});

test('set() sanitises an unknown mode to system', () => {
  const page = loadAccent({ prefersDark: true });
  assert.equal(page.theme.set('sepia'), 'system');
  assert.equal(page.store.get('stencil_theme'), 'system');
  assert.equal(page.dataTheme(), 'dark');
});

test('system keeps tracking the OS after load', () => {
  const page = loadAccent({ prefersDark: false });
  assert.equal(page.dataTheme(), 'light');
  page.setPrefersDark(true);
  assert.equal(page.dataTheme(), 'dark', 'the OS flipping must re-stamp the attribute');
});

test('an explicit choice ignores the OS flipping', () => {
  const page = loadAccent({ stored: { stencil_theme: 'light' }, prefersDark: false });
  page.setPrefersDark(true);
  assert.equal(page.dataTheme(), 'light', 'Light must survive the OS going dark');
});

// ── Mirroring into chrome.storage.local ──

test('both choices are mirrored for contexts that cannot read this localStorage', () => {
  const page = loadAccent({ stored: { stencil_accent: 'brown', stencil_theme: 'dark' } });
  const merged = Object.assign({}, ...page.mirrored);
  assert.equal(merged.stencil_accent, 'brown');
  assert.equal(merged.stencil_theme, 'dark');
});

test('a sanitised accent is mirrored as the sanitised value, not the raw one', () => {
  const page = loadAccent({ stored: { stencil_accent: 'bogus' } });
  const merged = Object.assign({}, ...page.mirrored);
  assert.equal(merged.stencil_accent, 'violet');
});

test('no chrome API (an ordinary page) is not fatal', () => {
  const page = loadAccent({ withChrome: false });
  assert.equal(page.accent.get(), 'violet');
  assert.equal(page.dataAccent(), 'violet');
  assert.equal(page.mirrored.length, 0);
});

// ── Private mode ──

test('a throwing localStorage degrades to the defaults instead of breaking the page', () => {
  const page = loadAccent({ storageThrows: true, prefersDark: true });
  assert.equal(page.accent.get(), 'violet');
  assert.equal(page.theme.get(), 'system');
  assert.equal(page.dataAccent(), 'violet');
  assert.equal(page.dataTheme(), 'dark', 'the OS preference still resolves');
});

test('writes are swallowed in private mode, and the attribute still updates', () => {
  const page = loadAccent({ storageThrows: true });
  assert.equal(page.accent.set('pink'), 'pink');
  assert.equal(page.dataAccent(), 'pink', 'the visual change applies for this page session');
  assert.equal(page.theme.set('dark'), 'dark');
  assert.equal(page.dataTheme(), 'dark');
});

test('no matchMedia is not fatal — it resolves light', () => {
  const page = loadAccent({ withMatchMedia: false });
  assert.equal(page.theme.resolved(), 'light');
  assert.equal(page.dataTheme(), 'light');
});

// ── Cross-page live sync ──

test('another page changing the accent re-applies here without a reload', () => {
  const page = loadAccent();
  assert.equal(page.dataAccent(), 'violet');
  // Another same-origin extension page wrote the key; the storage event fires here.
  page.store.set('stencil_accent', 'orange');
  page.fireStorage('stencil_accent');
  assert.equal(page.dataAccent(), 'orange');
});

test('another page changing the Appearance re-applies here', () => {
  const page = loadAccent({ prefersDark: false });
  page.store.set('stencil_theme', 'dark');
  page.fireStorage('stencil_theme');
  assert.equal(page.dataTheme(), 'dark');
});

test('localStorage.clear() elsewhere (key === null) resets both to their defaults', () => {
  const page = loadAccent({ stored: { stencil_accent: 'grey', stencil_theme: 'dark' } });
  assert.equal(page.dataAccent(), 'grey');
  page.store.clear();
  page.fireStorage(null);
  assert.equal(page.dataAccent(), 'violet');
  assert.equal(page.dataTheme(), 'light');
});

test('an unrelated key is ignored', () => {
  const page = loadAccent({ stored: { stencil_accent: 'aqua' } });
  page.store.set('stencil_accent', 'pink'); // changed underneath, but…
  page.fireStorage('someone_elses_key');    // …the event is for another key
  assert.equal(page.dataAccent(), 'aqua', 'only the accent/theme keys should trigger a re-apply');
});

test('onChange fires when another surface changes the Appearance', () => {
  const page = loadAccent();
  const seen = [];
  page.theme.onChange((mode) => seen.push(mode));
  page.store.set('stencil_theme', 'dark');
  page.fireStorage('stencil_theme');
  assert.deepEqual(seen, ['dark']);
});

// ── Where the palette-swap wipe starts ───────────────────────────────────────
// It has to come out of the CONTROL that changed the theme — the round toggle in the
// popup header — the way the desktop app blooms from its theme button. The trap was the
// pointer fallback: the last press is often an unrelated one, so it dragged the circle
// off into a window corner.
const TOGGLE = { id: 'theme-toggle', rect: { left: 276, top: 26, width: 28, height: 28 } };
const wipePage = (opts = {}) => loadAccent({ withViewTransitions: true, controls: [TOGGLE], ...opts });

test('the wipe starts at the theme button, and no press is remembered to override it', () => {
  const page = wipePage();
  assert.equal(page.pointerListeners.length, 0, 'the last click is never the origin');
  page.theme.set('dark');
  assert.deepEqual(page.swapOrigin(), { x: 290, y: 40 }, 'the centre of #theme-toggle');
  // And the circle still has to reach the furthest corner from there.
  assert.equal(Math.round(page.swapRadius()), Math.round(Math.hypot(290, 660)));
});

test('with nothing to anchor to, the wipe blooms from the centre — never a click', () => {
  const page = wipePage({ controls: [] });   // a page with no #theme-toggle at all
  page.theme.set('dark');
  assert.deepEqual(page.swapOrigin(), { x: 240, y: 350 }, 'the viewport centre');
});

test('the caller can hand over the control it owns (the options page has no toggle)', () => {
  const page = wipePage({ controls: [] });
  const select = { getBoundingClientRect: () => ({ left: 100, top: 200, width: 200, height: 30 }) };
  page.theme.set('dark', select);
  assert.deepEqual(page.swapOrigin(), { x: 200, y: 215 });
  const trigger = { getBoundingClientRect: () => ({ left: 10, top: 400, width: 100, height: 20 }) };
  page.accent.set('crimson', trigger);
  assert.deepEqual(page.swapOrigin(), { x: 60, y: 410 });
});

test('a laid-out but invisible copy of the control is skipped for the visible one', () => {
  // A panel that clones its toolbar duplicates ids; an opacity-0 / off-screen copy keeps a
  // perfectly good rect, and blooming from it is exactly how the circle ends up in a corner.
  const page = wipePage({
    controls: [
      { id: 'theme-toggle', rect: { left: 0, top: 0, width: 28, height: 28 }, visible: false },
      { id: 'theme-toggle', rect: { left: -400, top: 10, width: 28, height: 28 } },   // off-screen
      TOGGLE,
    ],
  });
  page.theme.set('dark');
  assert.deepEqual(page.swapOrigin(), { x: 290, y: 40 });
});

test('the accent swap anchors to the toggle too, and a cross-page change animates', () => {
  const page = wipePage();
  page.accent.set('sky');
  assert.deepEqual(page.swapOrigin(), { x: 290, y: 40 });
  // A change made in ANOTHER extension page repaints here with no press at all.
  page.store.set('stencil_accent', 'brown');
  page.fireStorage('stencil_accent');
  assert.equal(page.dataAccent(), 'brown');
  assert.deepEqual(page.swapOrigin(), { x: 290, y: 40 });
});

// ── Parity with the two copies that cannot import this file ──

test('ACCENT_HEX in lib/highlightColor.js matches the list exactly', () => {
  const { accent } = loadAccent();
  const fromAccentJs = Object.fromEntries(accent.list.map((a) => [a.key, a.hex]));
  assert.deepEqual(
    fromAccentJs,
    ACCENT_HEX,
    'lib/highlightColor.js ACCENT_HEX drifted from lib/accent.js — both say "keep in sync"',
  );
  assert.equal(DEFAULT_HL, accent.hexOf('violet'), 'the default highlight is the default accent');
});

test('the browser palette (config/accents.json) carries the same keys and hexes', () => {
  const { accent } = loadAccent();
  // The canonical palette now lives in the browser's JSON config; parse it directly.
  const rows = JSON.parse(readFileSync(
    fileURLToPath(new URL('../../browser/js/config/accents.json', import.meta.url)),
    'utf8',
  ));
  const browserAccents = Object.fromEntries(rows.map((a) => [a.key, a.hex.toLowerCase()]));
  assert.equal(Object.keys(browserAccents).length, 12, 'failed to parse browser accents.json');

  const fromAccentJs = Object.fromEntries(accent.list.map((a) => [a.key, a.hex.toLowerCase()]));
  assert.deepEqual(
    fromAccentJs,
    browserAccents,
    'the extension and browser accent palettes drifted',
  );
});

test('the extension and the browser deliberately use DIFFERENT storage keys', () => {
  const { accent } = loadAccent();
  const src = readFileSync(
    fileURLToPath(new URL('../../browser/js/core/accents.js', import.meta.url)),
    'utf8',
  );
  // Different origins, different stores — sharing a key would be the actual bug.
  assert.match(src, /ACCENT_STORAGE_KEY = 'drawingApp_accent'/);
  assert.equal(accent.storageKey, 'stencil_accent');
});

// ── Glyph-shadow contrast switch ────────────────────────────────────────────
// White glyphs sit on --accent; a LIGHT preset washes them out, so accent.js flags
// <html data-accent-light> and lib/theme.css swaps --glyph-shadow to a dark halo.
// Mirrors browser/tests/accentController.test.js (same WCAG 3:1 threshold).

test('data-accent-light is stamped only for the light presets', () => {
  const { accent, isAccentLight } = loadAccent();
  const light = [];
  for (const a of accent.list) {
    accent.set(a.key);
    if (isAccentLight()) light.push(a.key);
  }
  // Yellow (1.92:1) and sky (2.77:1) are the presets white reads poorly on.
  assert.deepEqual(light, ['yellow', 'sky']);
});

test('switching from a light accent back to a dark one clears the flag', () => {
  const { accent, isAccentLight } = loadAccent();
  accent.set('yellow');
  assert.equal(isAccentLight(), true);
  accent.set('violet');
  assert.equal(isAccentLight(), false);
});
