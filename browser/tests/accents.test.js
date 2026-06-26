// Unit tests for accents.js pure helpers — the hex normalizer used by the custom
// (non-preset) accent path (logo double-click picker + stencil.mainTheme = '#hex').
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { normalizeHex, isAccent, accentHex, DEFAULT_ACCENT } from '../js/core/accents.js';

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
