// lib/scrollbarHover.js is a rule-for-rule PORT of browser/js/ui/scrollbarHover.js
// (portParity.test.js pins the bodies identical; the geometry cases live in
// browser/tests/canvas-scrollbar.test.js). What remains here is the extension-specific
// wiring: every page calls it once, and the theme carries the tokens + the app-wide rule
// the `.sb-hover` mark drives.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { popupCss } from './helpers/sources.js';
import { readFileSync } from 'node:fs';
import { scrollbarHit } from '../src/lib/scrollbarHover.js';

const read = (rel) => readFileSync(new URL(rel, import.meta.url), 'utf8');

test('the bar strip is the last 14px of the box on the axis that scrolls', () => {
  const box = { left: 100, top: 50, right: 700, bottom: 450 };
  assert.equal(scrollbarHit(box, 690, 200, { canY: true }), true);
  assert.equal(scrollbarHit(box, 690, 200, { canY: false }), false);
  assert.equal(scrollbarHit(box, 400, 200, { canY: true }), false, 'the middle of the panel is not the bar');
  assert.equal(scrollbarHit(box, 400, 440, { canX: true }), true);
});

test('every page wires the document listener once', () => {
  for (const script of ['popup/popup.js', 'crop/crop.js', 'options/options.js']) {
    const src = read(`../src/${script}`);
    assert.match(src, /import \{ wireScrollbarHover \} from '\.\.\/lib\/scrollbarHover\.js';/, `${script} imports it`);
    assert.equal((src.match(/^wireScrollbarHover\(\);/gm) || []).length, 1, `${script} calls it exactly once`);
  }
  // The side panel and the devtools panel run popup.js, so they are covered by it.
  for (const page of ['sidepanel/sidepanel.html', 'devtools/panel.html'])
    assert.match(read(`../src/${page}`), /src="\.\.\/popup\/popup\.js"/, `${page} runs popup.js`);
});

test('the theme paints every scrollable thin + grey, and the accent only via .sb-hover', () => {
  const theme = read('../src/lib/theme.css');
  assert.equal((theme.match(/--sb-thumb-hover: var\(--accent\);/g) || []).length, 2, 'hover IS the accent, both palettes');
  assert.equal((theme.match(/--sb-thumb: #[0-9a-f]{6};/g) || []).length, 2, 'a rest grey per palette');
  assert.match(theme, /\n\* \{\s*scrollbar-width: thin;\s*scrollbar-color: var\(--sb-thumb\) transparent;\s*\}/);
  assert.match(theme, /\.sb-hover \{ scrollbar-color: var\(--sb-thumb-hover\) transparent; \}/);
  // The popup's resource list keeps its classic, space-taking bar — opted back out of
  // the thin rule (Chrome drops ::-webkit-scrollbar under it) but in the same colours.
  const popup = popupCss();
  assert.match(popup, /\.list \{ scrollbar-width: auto; scrollbar-color: auto; \}/);
  assert.match(popup, /\.list::-webkit-scrollbar-thumb \{ background: var\(--sb-thumb\);/);
  assert.match(popup, /\.list\.sb-hover::-webkit-scrollbar-thumb \{ background: var\(--sb-thumb-hover\); \}/);
  assert.ok(!/var\(--muted\); border-radius: 8px/.test(popup), 'no muted thumb left behind');
});
