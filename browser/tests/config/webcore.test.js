// ── webcore.json ↔ css/webcore/tokens.css, and the icon set ↔ icons.json ──────────
// The skin's palette is declared in the stylesheet and copied into the config for the desktop;
// this re-derives the copy from the sheet. The pixel set must cover every glyph the app draws.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

import WEBCORE from '../../js/config/webcore.json' with { type: 'json' };
import PIXELS from '../../js/config/iconsWebcore.json' with { type: 'json' };
import ICONS from '../../js/config/icons.json' with { type: 'json' };
import { splitThemeTokens } from '../helpers/themeCss.js';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const read = (rel) => readFileSync(resolve(ROOT, rel), 'utf8');
const wcOnly = (map) => Object.fromEntries(Object.entries(map).filter(([k]) => k.startsWith('--wc-') && !k.includes('raised')
  && !k.includes('sunken') && !k.includes('well') && !k.includes('title:') && k !== '--wc-title' && k !== '--wc-font'));

test('webcore.json tokens are exactly what css/webcore/tokens.css declares, both themes', () => {
  const { light, dark } = splitThemeTokens(read('css/webcore/tokens.css'),
    { light: ':root[data-skin="webcore"]', dark: ':root[data-skin="webcore"][data-theme="dark"]' });
  assert.deepEqual(wcOnly(light), WEBCORE.tokens.light);
  assert.deepEqual(wcOnly(dark), WEBCORE.tokens.dark);
  assert.deepEqual(Object.keys(WEBCORE.tokens.dark), Object.keys(WEBCORE.tokens.light), 'the dark set restates every token');
  for (const v of [...Object.values(WEBCORE.tokens.light), ...Object.values(WEBCORE.tokens.dark)])
    assert.match(v, /^#[0-9a-f]{6}$/, `${v} is a plain hex the desktop can read`);
});

test('the word is seven simple polygons on the glyph grid, one colour each', () => {
  const { text, glyphs, grid, colors } = WEBCORE.word;
  assert.equal(text, 'STENCIL');
  assert.equal(colors.length, text.length);
  assert.equal(new Set(colors).size, colors.length, 'every letter its own colour');
  for (const ch of text) {
    const poly = glyphs[ch];
    assert.ok(poly && poly.length >= 6, `${ch} is a polygon`);
    for (const [x, y] of poly) assert.ok(x >= 0 && x <= grid[0] && y >= 0 && y <= grid[1], `${ch} stays on its grid`);
    // Closed by the line lock, so the first point is not repeated.
    assert.notDeepEqual(poly[0], poly[poly.length - 1]);
  }
});

test('the picture keeps to its cell grid and its parts stay in the sky', () => {
  const { width, height, cell, horizonShare, clouds, skyBands, grass } = WEBCORE.image;
  assert.equal(width % cell, 0);
  assert.equal(height % cell, 0);
  const horizon = Math.round((height / cell) * horizonShare);
  for (const c of clouds)
    for (const [dx, dy, w, h] of [...c.blocks, ...(c.shade || [])])
      assert.ok(c.y + dy + h <= horizon && c.x + dx + w <= width / cell, 'a cloud sits in the sky');
  assert.ok(skyBands.length >= 4 && grass.length >= 2);
});

test('the pixel set covers every glyph, the desktop extras and the faces, 16×16 of palette chars', () => {
  const names = Object.keys(PIXELS.icons);
  for (const n of [...Object.keys(ICONS), 'power', 'search', 'more-vertical', 'logo', 'draw-mode-line', 'draw-mode-rect'])
    assert.ok(names.includes(n), `${n} has a pixel twin`);
  const chars = new Set(Object.keys(PIXELS.palette));
  for (const [n, rows] of Object.entries(PIXELS.icons)) {
    assert.equal(rows.length, PIXELS.size, `${n}: ${PIXELS.size} rows`);
    for (const row of rows) {
      assert.equal(row.length, PIXELS.size, `${n}: ${PIXELS.size} wide`);
      for (const ch of row) assert.ok(chars.has(ch), `${n}: '${ch}' is in the palette`);
    }
    assert.ok(rows.some((r) => /[^.]/.test(r)), `${n} is not empty`);
  }
  assert.equal(PIXELS.palette['.'], null, 'the dot is transparent');
});
