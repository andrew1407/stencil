// config/svgArt.json is the one copy of the inline SVG that is NOT a 24-grid toolbar glyph: the draw-mode
// toggle's 16-grid pair and the accent-tinted favicon. Each has a second copy the browser cannot import
// from, so each is pinned here — favicon against favicon.svg (the static file the tab loads before JS runs)
// and the extension's pre-paint classic script (browser-extension/src/lib/accent.js), drawMode against
// icons.json's canonical `line`/`rect`, which this pair is x1.5 smaller than.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

import ART from '../js/config/svgArt.json' with { type: 'json' };
import ICONS from '../js/config/icons.json' with { type: 'json' };
import ACCENTS from '../js/config/accents.json' with { type: 'json' };
import { faviconSvg } from '../js/core/accents.js';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const read = (rel) => readFileSync(resolve(ROOT, rel), 'utf8');

// Every element of an SVG as `tag attr=value …`, attributes sorted — so indentation,
// line breaks and attribute order are free but a moved point or a changed colour is not.
const elements = (svg) => [...svg.replace(/<!--[\s\S]*?-->/g, '').matchAll(/<([a-z]+)\b([^>]*?)\/?>/g)]
  .map(([, tag, attrs]) => {
    const pairs = [...attrs.matchAll(/([\w:-]+)\s*=\s*"([^"]*)"/g)]
      .map(([, k, v]) => `${k}=${v.replace(/\s+/g, ' ').trim()}`);
    return `${tag} ${pairs.sort().join(' ')}`.trim();
  });

test('the favicon art draws exactly what favicon.svg draws', () => {
  const painted = elements(faviconSvg(ACCENTS[0].hex));
  const onDisk = elements(read('favicon.svg'));
  assert.equal(painted.length, onDisk.length, 'the two marks have a different element count');
  // The root <svg> alone may differ, since the file carries role/aria-label a data: URI has no use for:
  // everything after it — panel, frame, polyline, the seven points — must match attribute for attribute.
  assert.deepEqual(painted.slice(1), onDisk.slice(1));
  for (const attr of [`viewBox=0 0 64 64`, 'xmlns=http://www.w3.org/2000/svg'])
    for (const root of [painted[0], onDisk[0]]) assert.ok(root.includes(attr), `${root} lost ${attr}`);
});

// Two surfaces ship the mark as a FILE, where the browser cannot import it from: the Chrome
// extension's action icon and the VS Code extension's logo source. Same art, or they drift.
test('the extension icon files are the same mark as favicon.svg', () => {
  const onDisk = elements(read('favicon.svg'));
  for (const copy of ['../browser-extension/icons/icon.svg', '../vscode-extension/icons/stencil.svg'])
    assert.deepEqual(elements(read(copy)), onDisk, `${copy} drifted from favicon.svg`);
});

test('%1 is the only placeholder, and it is the accent-stroked panel outline', () => {
  assert.equal(ART.favicon.match(/%\d/g).join(''), '%1', 'one placeholder, exactly once');
  assert.match(ART.favicon, /<rect [^>]*stroke="%1"/, '%1 strokes the panel outline');
  for (const { hex } of ACCENTS) assert.ok(faviconSvg(hex).includes(`stroke="${hex}"`), hex);
  assert.ok(!faviconSvg('#ff0000').includes('%1'), 'the placeholder is consumed');
});

// The extension paints the same mark from a pre-paint CLASSIC script, which cannot import JSON, so its copy
// is string concatenation: compare the drawing, not the source text.
test('the extension paints the same favicon (src/lib/prefs.js)', () => {
  const src = read('../browser-extension/src/lib/prefs.js');
  const body = src.slice(src.indexOf('const faviconSvg ='), src.indexOf('const applyFavicon'));
  const copy = [...body.matchAll(/'([^']*)'/g)].map((m) => m[1]).join('');
  assert.ok(copy.includes('<svg'), 'failed to read the extension copy');
  assert.deepEqual(elements(copy), elements(ART.favicon.replace('%1', '')),
    'browser-extension/src/lib/prefs.js faviconSvg drifted from config/svgArt.json');
});

test('the draw-mode pair is the canonical line/rect pair, x1.5 smaller', () => {
  const { line, rect } = ART.drawMode;
  for (const [name, svg] of Object.entries(ART.drawMode)) {
    assert.match(svg, /^<svg class="draw-mode-icon" viewBox="0 0 16 16" width="13" height="13" aria-hidden="true">/,
      `${name} keeps the wrapper the toggle swaps`);
    assert.ok(!/#|rgb\(|var\(/.test(svg), `${name} paints in currentColor only`);
  }
  // Each number in the inline face is its canonical twin's, divided by 1.5.
  const nums = (svg, attrs) => attrs.map((a) => +(parseFloat(svg.match(new RegExp(`${a}="([\\d.]+)"`))[1])).toFixed(4));
  const scale = (list) => list.map((n) => +(n * 1.5).toFixed(4));
  assert.deepEqual(scale(nums(line, ['x1', 'y1', 'x2', 'y2'])), nums(ICONS.line, ['x1', 'y1', 'x2', 'y2']));
  assert.deepEqual(scale(nums(rect.match(/<rect[^>]*>/)[0], ['x', 'y', 'width', 'height'])),
    nums(ICONS.rect, ['x', 'y', 'width', 'height']));
  // …and both faces anchor the same two handles, on the same corners.
  for (const svg of [line, rect])
    for (const handle of ['cx="3" cy="13"', 'cx="13" cy="3"'])
      assert.ok(svg.includes(`<circle class="ic-handle" ${handle} r="2" fill="currentColor"/>`), handle);
});

test('the art never lands in icons.json, whose keys are all glyph-table names', () => {
  // icons.json is iterated whole by every surface's glyph table (desktop iconSet.cpp) and
  // must be covered exactly by iconMotion.json — so this art cannot live there.
  for (const key of ['favicon', 'drawMode', 'draw-mode-line', 'draw-mode-rect', 'secretEgg', 'egg'])
    assert.ok(!(key in ICONS), `${key} belongs in svgArt.json, not icons.json`);
  for (const inner of Object.values(ICONS))
    assert.ok(!inner.includes('<svg'), 'icons.json holds INNER markup only — icon() adds the wrapper');
});
