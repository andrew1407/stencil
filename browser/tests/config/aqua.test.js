// ── aqua.json ↔ css/aqua/tokens.css, and the wallpaper's table ──────────
// The skin's palette is declared in the stylesheet and copied into the config for the desktop;
// this re-derives the copy from the sheet.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

import AQUA from '../../../common/config/aqua.json' with { type: 'json' };
import STAGE from '../../../common/config/logoStage.json' with { type: 'json' };
import { splitThemeTokens } from '../helpers/themeCss.js';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const read = (rel) => readFileSync(resolve(ROOT, rel), 'utf8');
const COMPOSITE = new Set(['--aq-pinstripe', '--aq-metal', '--aq-glass', '--aq-gel', '--aq-glass-ring',
  '--aq-gel-ring', '--aq-window-ring', '--aq-tile-ring', '--aq-font', '--aq-arrows', '--aq-tick', '--aq-popup-ring']);
const aqOnly = (map) => Object.fromEntries(Object.entries(map).filter(([k]) => k.startsWith('--aq-') && !COMPOSITE.has(k)));

test('aqua.json tokens are exactly what css/aqua/tokens.css declares, both themes', () => {
  const { light, dark } = splitThemeTokens(read('css/aqua/tokens.css'),
    { light: ':root[data-skin="aqua"]', dark: ':root[data-skin="aqua"][data-theme="dark"]' });
  assert.deepEqual(aqOnly(light), AQUA.tokens.light);
  assert.deepEqual(aqOnly(dark), AQUA.tokens.dark);
  assert.deepEqual(Object.keys(AQUA.tokens.dark), Object.keys(AQUA.tokens.light), 'the dark set restates every token');
  for (const v of [...Object.values(AQUA.tokens.light), ...Object.values(AQUA.tokens.dark)])
    assert.match(v, /^#[0-9a-f]{6}$/, `${v} is a plain hex the desktop can read`);
});

test('aqua.json aliases are every base token tokens.css points at one --aq-* token, and only those', () => {
  const { light } = splitThemeTokens(read('css/aqua/tokens.css'), { light: ':root[data-skin="aqua"]' });
  const aliases = Object.fromEntries(Object.entries(light).filter(([k]) => !k.startsWith('--aq-'))
    .map(([k, v]) => [k, /^var\((--aq-[\w-]+)\)$/.exec(v)?.[1]]).filter(([, v]) => v));
  assert.deepEqual(AQUA.aliases, aliases);
  for (const target of Object.values(AQUA.aliases)) assert.ok(target in AQUA.tokens.light, `${target} is a plain token`);
});

test('the wallpaper keeps to its cell grid and every swoosh is a whole parabola in cells', () => {
  const { width, height, cell, ramp, swooshes } = AQUA.image;
  assert.equal(width % cell, 0);
  assert.equal(height % cell, 0);
  assert.ok(ramp.length >= 4 && swooshes.length >= 1);
  for (const s of swooshes) {
    for (const k of ['x0', 'span', 'base', 'amp', 'width', 'edge']) assert.ok(Number.isInteger(s[k]), `${k} is an integer`);
    assert.ok(s.span > 0 && s.width > 2 * s.edge);
  }
});

test('the word has one gel colour per STENCIL letter, and the hold row asks for blue, slide and light', () => {
  assert.equal(AQUA.word.colors.length, 7);
  assert.equal(new Set(AQUA.word.colors).size, 7);
  assert.deepEqual(STAGE.shows.someFruitCompany,
    { effect: 'aqua', accents: ['blue'], motion: 'slide', theme: 'light' });
});
