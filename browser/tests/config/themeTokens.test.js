// ── themeTokens.json ↔ css/theme.css drift guard ────────────────────────────
// The asset is the cross-surface copy of the theme's colour tokens (desktop theme.cpp's
// Palette, the cli's logo/prompt colours). The STYLESHEET is the source of truth: this
// re-derives the whole table from theme.css and fails if the JSON says anything else, so
// the copy can never lag the app the browser actually paints.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

import ASSET from '../../js/config/themeTokens.json' with { type: 'json' };
import ACCENTS from '../../js/config/accents.json' with { type: 'json' };
import CONSTANTS from '../../js/config/constants.json' with { type: 'json' };
import { splitThemeTokens } from '../helpers/themeCss.js';
import { LAYOUT_CSS } from '../helpers/css.js';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const read = (rel) => readFileSync(resolve(ROOT, rel), 'utf8');

const derivedFromCss = splitThemeTokens(read('css/theme.css'));

test('themeTokens.json is exactly what theme.css declares, key order included', () => {
  assert.deepEqual(ASSET.tokens, derivedFromCss.tokens);
  assert.deepEqual(ASSET.derived, derivedFromCss.derived);
  assert.deepEqual(Object.keys(ASSET.tokens), Object.keys(derivedFromCss.tokens));
  assert.deepEqual(Object.keys(ASSET.derived), Object.keys(derivedFromCss.derived));
});

test('every literal token carries both themes; no computed value hides among them', () => {
  for (const [name, pair] of Object.entries(ASSET.tokens)) {
    assert.deepEqual(Object.keys(pair), ['light', 'dark'], `${name}: light+dark only`);
    for (const v of Object.values(pair)) {
      assert.ok(typeof v === 'string' && v.length, `${name} is a non-empty string`);
      assert.ok(!v.includes('var(') && !v.includes('color-mix('),
        `${name} is computed — it belongs in "derived"`);
    }
  }
  // A derived token records the EXPRESSION, never a resolved colour.
  for (const [name, pair] of Object.entries(ASSET.derived))
    assert.ok(pair.light.includes('var(') || pair.light.includes('color-mix('),
      `${name} no longer looks computed — move it to "tokens"`);
});

// The consumers this asset exists for. Named here so removing a token the desktop or the
// cli embeds fails in the browser first, where theme.css is edited.
const DESKTOP_PALETTE = [
  '--bg-page', '--bg-container', '--bg-controls', '--bg-sel-panel', '--border-main',
  '--border-canvas', '--border-sel', '--text-main', '--text-muted', '--text-sel-label',
  '--bg-sel-btn', '--bg-sel-btn-hov', '--text-sel-btn', '--text-key', '--input-bg',
  '--input-text', '--bg-coord-th', '--danger', '--bg-coord-hover', '--warning',
  '--disabled-text', '--on-accent', '--bg-info',
];

test('the 23 tokens desktop theme.cpp Palette mirrors are all present', () => {
  for (const name of DESKTOP_PALETTE)
    assert.ok(name in ASSET.tokens || name in ASSET.derived, `${name} left the asset`);
});

test('the 6 colours the cli logo paints are all present', () => {
  for (const name of ['accent', 'annotation', 'panel', 'panelInner', 'panelGrid', 'errorRed'])
    assert.match(ASSET.brand[name], /^#[0-9a-fA-F]{6}$/, `brand.${name}`);
});

test('brand colours agree with the art and the tables that also carry them', () => {
  const favicon = read('favicon.svg');
  for (const key of ['annotation', 'panel', 'panelInner'])
    assert.ok(favicon.includes(ASSET.brand[key]), `brand.${key} is not in favicon.svg`);
  assert.equal(ASSET.brand.accent, ASSET.tokens['--accent'].light);
  assert.equal(ASSET.brand.accent, ACCENTS[0].hex);
  assert.equal(ASSET.brand.annotation, CONSTANTS.DEFAULT_VISUALS.color);
  // Recorded drift, deliberately: the cli's `error:` red is NOT --danger. Pinned so the
  // day someone converges them, this test is the reminder to say so.
  assert.notEqual(ASSET.brand.errorRed, ASSET.tokens['--danger'].light);
  assert.ok(LAYOUT_CSS.includes(`var(--danger, ${ASSET.brand.errorRed})`));
});
