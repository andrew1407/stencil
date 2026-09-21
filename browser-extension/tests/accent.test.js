// src/lib/accent.js — the extension's accent list, its store and the favicon it paints.
// A pre-paint classic <script>, so tests/helpers/accentSandbox.js runs it in a fabricated page.

import { test } from 'node:test';
import assert from 'node:assert/strict';

import { loadAccent } from './helpers/accentSandbox.js';
import { ACCENT_STORAGE_KEY } from '../src/lib/highlight/highlightColor.js';
import { THEME_STORAGE_KEY } from '../src/lib/prefs/shellTheme.js';

// ── The accent list ──

test('publishes the sixteen presets, each with a key, label and #rrggbb hex', () => {
  const { accent } = loadAccent();
  assert.equal(accent.list.length, 16);
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

test('setCustom: a page-only inline --accent, not persisted; junk → null', () => {
  const page = loadAccent();
  assert.equal(page.accent.setCustom('#abc'), '#aabbcc', 'normalizes a 3-digit hex');
  assert.equal(page.inlineAccent(), '#aabbcc', 'the inline override is set');
  assert.equal(page.store.get('stencil_accent'), undefined, 'a custom accent never persists');
  assert.equal(page.accent.setCustom('nope'), null, 'invalid hex is rejected');
});

test('previewAccent paints a preset instantly (no persist); endAccentPreview reverts to the committed accent', () => {
  const page = loadAccent();
  page.accent.set('blue');                     // committed preset
  assert.equal(page.dataAccent(), 'blue');
  page.accent.previewAccent('pink');           // hover
  assert.equal(page.dataAccent(), 'pink', 'the page shows the previewed preset');
  assert.equal(page.store.get('stencil_accent'), 'blue', 'preview never persists');
  page.accent.endAccentPreview();
  assert.equal(page.dataAccent(), 'blue', 'leaving the list restores the committed preset');
});

test('previewAccent over a custom accent restores the inline hex on revert', () => {
  const page = loadAccent();
  page.accent.setCustom('#123456');
  page.accent.previewAccent('pink');
  assert.equal(page.dataAccent(), 'pink');
  assert.equal(page.inlineAccent(), '', 'the preview drops the inline override');
  page.accent.endAccentPreview();
  assert.equal(page.inlineAccent(), '#123456', 'the custom hex comes back');
});

test('a committed set during a preview supersedes it — endAccentPreview then no-ops', () => {
  const page = loadAccent();
  page.accent.set('blue');
  page.accent.previewAccent('pink');
  page.accent.set('aqua');                     // a real pick mid-hover
  page.accent.endAccentPreview();
  assert.equal(page.dataAccent(), 'aqua', 'the pick stands; no revert to blue');
});

// The real thing defers the swap's callback a beat and the menu's own close ends the preview in
// that gap, so the supersede must already have happened by then.
test('a pick supersedes the preview BEFORE its swap callback runs, not inside it', () => {
  const page = loadAccent({ deferViewTransitions: true });
  page.accent.set('blue');
  page.runSwaps();
  page.accent.previewAccent('pink');
  page.runSwaps();
  assert.equal(page.dataAccent(), 'pink');

  page.accent.set('aqua');                     // the pick — its paint is still queued
  page.accent.endAccentPreview();              // …and the menu closes on top of it
  page.runSwaps();
  assert.equal(page.dataAccent(), 'aqua', 'the pick stands; the close cannot flood it away');
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
