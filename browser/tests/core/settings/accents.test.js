// Unit tests for accents.js pure helpers — the hex normalizer used by the custom
// (non-preset) accent path (logo double-click picker + stencil.mainTheme = '#hex'),
// and the contrast maths that picks the ink drawn on an accent.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import {
  normalizeHex, isAccent, accentHex, DEFAULT_ACCENT,
  needsDarkGlyph, onAccentInk, contrastWithWhite, contrastWithBlack, relativeLuminance,
  ON_ACCENT_LIGHT, ON_ACCENT_DARK, LIGHT_ACCENT_KEYS,
} from '../../../js/core/settings/accents.js';

test('normalizeHex: accepts #rrggbb / #rgb (# optional), normalizes case + expands shorthand', () => {
  assert.equal(normalizeHex('#ff5623'), '#ff5623');
  assert.equal(normalizeHex('FF5623'), '#ff5623');     // no '#', upper-case
  assert.equal(normalizeHex('  #AbCdEf '), '#abcdef'); // trims + lower-cases
  assert.equal(normalizeHex('#f50'), '#ff5500');       // shorthand expands
  assert.equal(normalizeHex('000'), '#000000');
});

test('normalizeHex: rejects non-hex input', () => {
  for (const bad of ['', '#', '#ff', '#12345', '#1234567', 'rgb(0,0,0)', 'red', '#gggggg', null, undefined, 0x123]) {
    assert.equal(normalizeHex(bad), null, `should reject ${String(bad)}`);
  }
});

test('preset helpers still resolve keys to hexes', () => {
  assert.equal(isAccent(DEFAULT_ACCENT), true);
  assert.equal(isAccent('nope'), false);
  assert.equal(accentHex('violet'), '#7c3aed');
  assert.equal(accentHex('unknown'), '#7c3aed'); // falls back to the first preset
});

// Labels and currentColor line-art sit on --accent, so the accent picks whichever of white / near-black
// contrasts more: the controller flags <html data-accent-light> and css/theme.css swaps --on-accent.

test('needsDarkGlyph: the accents black reads better on than white', () => {
  assert.equal(needsDarkGlyph('#00ffff'), true);   // cyan — white 1.25:1, black 16.7:1
  assert.equal(needsDarkGlyph('#ffffff'), true);   // white on white
  assert.equal(needsDarkGlyph('#eab308'), true);   // the yellow preset — 1.92 vs 10.95
  assert.equal(needsDarkGlyph('#16a34a'), true);   // grass — 3.30 vs 6.37, both above 3:1
  assert.equal(needsDarkGlyph('#7c3aed'), false);  // violet default — 5.70 vs 3.69
  assert.equal(needsDarkGlyph('#000000'), false);  // black — white is the only readable ink
  assert.equal(needsDarkGlyph('nope'), false);     // not a hex → the white default, no throw
});

test('onAccentInk hands back the ink itself', () => {
  assert.equal(onAccentInk('#eab308'), ON_ACCENT_DARK);
  assert.equal(onAccentInk('#7c3aed'), ON_ACCENT_LIGHT);
  assert.equal(onAccentInk('nope'), ON_ACCENT_LIGHT);
  // Near-black, not black: the dark theme's own page ink, so the glyph doesn't out-ink
  // every other glyph in the app.
  assert.equal(ON_ACCENT_DARK, '#1a1a1a');
  assert.equal(ON_ACCENT_LIGHT, '#ffffff');
});

test('contrast helpers: the white/black anchors and the crossover', () => {
  assert.equal(Math.round(contrastWithWhite('#000000') * 100) / 100, 21);
  assert.equal(Math.round(contrastWithWhite('#ffffff') * 100) / 100, 1);
  assert.equal(Math.round(contrastWithBlack('#ffffff') * 100) / 100, 21);
  assert.equal(Math.round(contrastWithBlack('#000000') * 100) / 100, 1);
  assert.equal(contrastWithBlack('nope'), null);
  assert.equal(relativeLuminance('#ffffff'), 1);
  assert.equal(relativeLuminance('#000000'), 0);
  assert.equal(relativeLuminance('nope'), null);
  // The two ratios cross at luminance 0.1791 — anything lighter takes the dark ink.
  assert.equal(needsDarkGlyph('#757575'), false);   // L 0.1779, just under
  assert.equal(needsDarkGlyph('#767676'), true);    // L 0.1812, just over
});

test('LIGHT_ACCENT_KEYS matches prePaintTheme.js\'s inlined copy', () => {
  // prePaintTheme.js is a classic script and cannot import accents.js, so it
  // inlines this list. If a preset's hex changes, these two must move together.
  assert.deepEqual(LIGHT_ACCENT_KEYS,
    ['pink', 'orange', 'brown', 'yellow', 'grass', 'turquoise', 'aqua', 'sky', 'bluegray']);
  const inlined = readFileSync(new URL('../../../js/prePaintTheme.js', import.meta.url), 'utf8')
    .match(/LIGHT_ACCENT_KEYS = \[([^\]]*)\]/)[1]
    .split(',').map((s) => s.trim().replace(/^'|'$/g, '')).filter(Boolean);
  assert.deepEqual(inlined, LIGHT_ACCENT_KEYS);
});
