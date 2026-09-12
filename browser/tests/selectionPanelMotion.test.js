// The "Selected Line:" bar's entrance/exit (showSelectionPanel/hideSelectionPanels): forms
// from, and disperses back into, sand like other surfaces (js/ui/motion.js surfaceIn/
// surfaceOut). Desktop mirrors it via DisintegrateOverlay over SelectedLineBar (MainWindow.cpp).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { barDustPoint } from '../js/ui/selectionPanel.js';
import { LAYOUT_CSS } from './helpers/css.js';

const src = readFileSync(new URL('../js/ui/selectionPanel.js', import.meta.url), 'utf8');

// A rect stub good enough for barDustPoint: only left/width/top/height/bottom are read.
const rect = ({ left = 0, width = 0, top = 0, height = 0 }) =>
  ({ left, width, top, height, bottom: top + height });
// Shared fixtures: the bar's own box, and a #image-info stub (or none) for one test.
const testBar = () => ({ getBoundingClientRect: () => rect({ left: 0, width: 600, top: 200, height: 60 }) });
const stubImageInfo = (t, r) => {
  const prior = globalThis.document;
  globalThis.document = { getElementById: (id) => (r && id === 'image-info' ? { getBoundingClientRect: () => r } : null) };
  t.after(() => { globalThis.document = prior; });
};

test('the bar imports the shared surface-dust primitives', () => {
  // Pinned by NAME, not as a verbatim import line — the list grows (revealControls, for
  // the fill group's own slide), and a line-shaped regex only says the file was edited.
  const line = /import \{([^}]*)\} from '\.\/motion\.js';/.exec(src)[1];
  for (const fn of ['surfaceIn', 'surfaceOut', 'settleSurface', 'dockAwayPoint'])
    assert.ok(line.includes(fn), `the bar should still take ${fn} from the shared helpers`);
});

// The fill group is not shown/hidden outright: it SLIDES open and closed and dusts as it
// goes (motion.js revealControls), so unchaining a line does not make the bar jump. Its
// own separator travels with it.
test('the fill group and its separator come and go through revealControls', () => {
  const show = src.slice(src.indexOf('const fillSep ='), src.indexOf('const panel ='));
  assert.match(show, /revealControls\(fillGroup, true, 'flex'\)/);
  assert.match(show, /revealControls\(fillSep, true, 'block'\)/);
  assert.match(show, /revealControls\(fillGroup, false\)/);
  assert.match(show, /revealControls\(fillSep, false\)/);
  assert.ok(!/fillGroup\.style\.display = /.test(show), 'no outright show/hide left');
});

test('opening: the point is #image-info\'s own current rect — the reflow already ran', (t) => {
  // showSelectionPanel flips display:block (and so triggers the reflow) BEFORE ever
  // calling barDustPoint, so #image-info's rect it reads is already the post-open one.
  stubImageInfo(t, rect({ left: 100, width: 400, top: 300, height: 20 }));
  assert.deepEqual(barDustPoint(testBar()), { x: 100 + 400 / 2, y: 300 + 20 });
});

test('closing: the point is PREDICTED at image-info\'s post-close position, not its stale current one', (t) => {
  // hideSelectionPanels calls barDustPoint(el, true) before setting display:none, so
  // #image-info's rect here is still the bar's own height too low.
  stubImageInfo(t, rect({ left: 100, width: 400, top: 300, height: 20 }));
  // Predicted bottom = the bar's own top (200) + image-info's own height (20) = 220 —
  // NOT image-info's current (stale) bottom of 320.
  assert.deepEqual(barDustPoint(testBar(), true), { x: 100 + 400 / 2, y: 200 + 20 });
});

test('with no #image-info to anchor to, both directions fall back to the bar\'s own geometry', (t) => {
  stubImageInfo(t, null);
  const expected = { x: 300, y: 230 + 60 * 1.7 };   // dockAwayPoint(rect, 'bottom'): centre + reach
  assert.deepEqual(barDustPoint(testBar()), expected);
  assert.deepEqual(barDustPoint(testBar(), true), expected);
});

test('showSelectionPanel only gathers on the hidden -> visible edge', () => {
  const fn = src.slice(src.indexOf('export function showSelectionPanel'),
                       src.indexOf('export function hideSelectionPanels'));
  // wasHidden is read BEFORE display flips to 'block' — the whole point of the flag.
  const wasHiddenIdx = fn.indexOf('wasHidden');
  const displayBlockIdx = fn.indexOf("panel.style.display = 'block';");
  assert.ok(wasHiddenIdx > -1 && displayBlockIdx > -1 && wasHiddenIdx < displayBlockIdx,
    'wasHidden must be captured before the display flip it is judging');
  assert.match(fn, /if \(wasHidden && !surfaceIn\(panel, barDustPoint\(panel\)\)\) settleSurface\(panel\);/);
  // Re-populating an already-open bar (switching which line is selected) must NOT
  // re-trigger surfaceIn unconditionally — one call site, and it's the gated one above.
  assert.equal((fn.match(/surfaceIn\(/g) || []).length, 1);
});

test('hideSelectionPanels only scatters a bar that was actually visible, and hides it either way', () => {
  const fn = src.slice(src.indexOf('export function hideSelectionPanels'),
                       src.indexOf('export function applyFill'));
  assert.match(fn, /if \(selPanel\.style\.display === 'block'\) \{/);
  // The `true` here is the whole point: hideSelectionPanels is the CLOSING call site.
  assert.match(fn,
    /if \(!surfaceOut\(selPanel, barDustPoint\(selPanel, \/\* closing \*\/ true\)\)\) settleSurface\(selPanel\);/);
  assert.match(fn, /\} else settleSurface\(selPanel\);/);
  // The real hide still happens unconditionally and outside the branch — surfaceOut's
  // flying cloud is a snapshot of sand, not the element itself, so it can go at once
  // (js/ui/motion.js: "A surface NEVER dusts as clones of itself").
  const hideIdx = fn.indexOf("selPanel.style.display = 'none';");
  const elseIdx = fn.indexOf('} else settleSurface(selPanel);');
  assert.ok(hideIdx > elseIdx, 'display:none comes after the dust decision, unconditionally');
});

test('the old plain fadeIn keyframe stays as the reduced-motion / dust-declined fallback', () => {
  // motion.js: "Off again if the dust declines, so a surface that never plays it keeps
  // its old CSS entrance" — settleSurface() never adds SURFACE_DRIVEN_CLASS, so removing
  // this rule would leave a declined bar snapping in with no motion at all.
  const css = LAYOUT_CSS;
  assert.match(css, /#selection-panel \{[\s\S]*?animation: fadeIn 0\.15s ease;[\s\S]*?\}/);
});
