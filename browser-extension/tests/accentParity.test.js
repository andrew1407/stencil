// src/lib/accent.js's list against the two copies that cannot import it — lib/color.js
// and browser/js/config/accents.json — plus the on-accent ink the same list decides.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

import { loadAccent } from './helpers/accentSandbox.js';
import { ACCENT_HEX, DEFAULT_HL } from '../src/lib/highlight/color.js';

// ── Parity with the two copies that cannot import this file ──

test('ACCENT_HEX in lib/color.js matches the list exactly', () => {
  const { accent } = loadAccent();
  const fromAccentJs = Object.fromEntries(accent.list.map((a) => [a.key, a.hex]));
  assert.deepEqual(
    fromAccentJs,
    ACCENT_HEX,
    'lib/color.js ACCENT_HEX drifted from lib/accent.js — both say "keep in sync"',
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
  assert.equal(Object.keys(browserAccents).length, 16, 'failed to parse browser accents.json');

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
    fileURLToPath(new URL('../../browser/js/core/settings/accents.js', import.meta.url)),
    'utf8',
  );
  // Different origins, different stores — sharing a key would be the actual bug.
  assert.match(src, /ACCENT_STORAGE_KEY = 'drawingApp_accent'/);
  assert.equal(accent.storageKey, 'stencil_accent');
});

// On-accent ink: the accent picks whichever of white / near-black contrasts more on --accent,
// flagging <html data-accent-light>. Mirrors browser/tests/accentController.test.js.

test('data-accent-light is stamped only for the light presets', () => {
  const { accent, isAccentLight } = loadAccent();
  const light = [];
  for (const a of accent.list) {
    accent.set(a.key);
    if (isAccentLight()) light.push(a.key);
  }
  // The presets black reads better on than white does.
  assert.deepEqual(light, ['pink', 'orange', 'brown', 'yellow', 'grass', 'turquoise', 'aqua', 'sky', 'bluegray']);
});

test('inkOn hands a swatch the ink for its own colour', () => {
  const { accent } = loadAccent();
  assert.equal(accent.inkOn('#eab308'), '#1a1a1a');   // yellow — white 1.92:1, black 10.95:1
  assert.equal(accent.inkOn('#7c3aed'), '#ffffff');   // violet — 5.70 vs 3.69
  assert.equal(accent.inkOn('nope'), '#ffffff');      // not a hex → the white default
});

test('switching from a light accent back to a dark one clears the flag', () => {
  const { accent, isAccentLight } = loadAccent();
  accent.set('yellow');
  assert.equal(isAccentLight(), true);
  accent.set('violet');
  assert.equal(isAccentLight(), false);
});
