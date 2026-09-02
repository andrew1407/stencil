// Compare-mode coordinate tooltip (desktop parity, confirmed working there).
//
// A split comparison shows the ORIGINAL on the left/top of the divider and the edit on
// the right/bottom. Hovering a layout point in the edited half shows the usual
// coordinate tooltip; a point behind the original half shows NOTHING, because the user
// cannot see it there. The geometry mirrors the desktop's hover gate exactly:
//   'none' → always · 'original' (incl. the Alt+Shift+O peek) → never
//   'vertical' → x >= imageW * split · 'horizontal' → y >= imageH * split
// and it is judged on the POINT's own coordinates, never the cursor's.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { compareEditedShows } from '../js/utils.js';
import { DrawingApp } from '../js/core/drawingApp.js';
import { StencilTooltip } from '../js/ui/tooltip.js';

const W = 400, H = 300;

test('compareEditedShows: the exact desktop geometry, both axes and both ends', () => {
  // No comparison → everything is visible, whatever the split says.
  for (const [x, y] of [[0, 0], [W, H], [10, 290]]) {
    assert.equal(compareEditedShows('none', 0.5, x, y, W, H), true);
    assert.equal(compareEditedShows(undefined, 0.5, x, y, W, H), true);
  }
  // The original view (and the momentary peek, which resolves to 'original') → never.
  assert.equal(compareEditedShows('original', 0.5, 399, 299, W, H), false);
  assert.equal(compareEditedShows('original', 0.5, 0, 0, W, H), false);
  // VERTICAL: original is LEFT of the divider, the edit is right — boundary included.
  assert.equal(compareEditedShows('vertical', 0.5, 199, 150, W, H), false, 'left of centre');
  assert.equal(compareEditedShows('vertical', 0.5, 200, 150, W, H), true, 'exactly on it');
  assert.equal(compareEditedShows('vertical', 0.5, 201, 150, W, H), true);
  // …and y is irrelevant on that axis.
  assert.equal(compareEditedShows('vertical', 0.5, 300, 0, W, H), true);
  assert.equal(compareEditedShows('vertical', 0.5, 100, 299, W, H), false);
  // HORIZONTAL: original is on TOP.
  assert.equal(compareEditedShows('horizontal', 0.5, 200, 149, W, H), false, 'above the divider');
  assert.equal(compareEditedShows('horizontal', 0.5, 200, 150, W, H), true);
  assert.equal(compareEditedShows('horizontal', 0.5, 0, 299, W, H), true);
  // The split MOVES the answer for one and the same point — the case that proves the
  // gate is really consulted rather than fixed.
  assert.equal(compareEditedShows('vertical', 0.25, 150, 10, W, H), true, 'right of 25%');
  assert.equal(compareEditedShows('vertical', 0.75, 150, 10, W, H), false, 'left of 75%');
  // A missing/degenerate split falls back to the centre, and is clamped to 0..1.
  assert.equal(compareEditedShows('vertical', undefined, 250, 10, W, H), true);
  assert.equal(compareEditedShows('vertical', -5, 0, 10, W, H), true, 'clamped to 0');
  assert.equal(compareEditedShows('vertical', 5, 399, 10, W, H), false, 'clamped to 1');
});

// The app method the tooltip consults: the EFFECTIVE mode (so the peek is folded in)
// and the canvas's own dimensions.
const appWithCompare = (mode, split = 0.5) => ({
  canvas: { width: W, height: H },
  compareSplit: split,
  renderer: { effectiveCompareMode: () => mode },
  compareShowsPoint: DrawingApp.prototype.compareShowsPoint,
});

test('compareShowsPoint reads the effective mode, the live split and the canvas size', () => {
  const v = appWithCompare('vertical');
  assert.equal(v.compareShowsPoint(250, 10), true);
  assert.equal(v.compareShowsPoint(150, 10), false);
  // The peek resolves to 'original' upstream, so nothing is labelled during it.
  assert.equal(appWithCompare('original').compareShowsPoint(399, 299), false);
  assert.equal(appWithCompare('none').compareShowsPoint(0, 0), true);
  // Moving the divider moves the answer without touching anything else.
  const moved = appWithCompare('horizontal', 0.9);
  assert.equal(moved.compareShowsPoint(10, 200), false);
  moved.compareSplit = 0.1;
  assert.equal(moved.compareShowsPoint(10, 200), true);
});

// ── The tooltip decision itself ─────────────────────────────────────────────
// applyHover is called directly (it is the shared decision both mousemove and the
// modifier-refresh use), with a mock app recording what it was asked to show.
const hoverHarness = ({ mode = 'vertical', split = 0.5, point = null, lineIdx = -1, gate } = {}) => {
  const calls = { show: [], line: [], hide: 0 };
  const app = {
    canvas: { width: W, height: H },
    compareSplit: split,
    renderer: { effectiveCompareMode: () => mode },
    lines: [{ points: [{ x: 1, y: 2 }], color: '#ff0000' }],
    findNearestPoint: () => point,
    findLineAt: () => lineIdx,
  };
  app.compareShowsPoint = gate || DrawingApp.prototype.compareShowsPoint.bind(app);
  const tip = Object.create(StencilTooltip.prototype);
  tip.app = app;
  tip.show = (cx, cy, x, y) => calls.show.push([x, y]);
  tip.showLine = (cx, cy, line, full) => calls.line.push([line, full]);
  tip.hide = () => { calls.hide += 1; };
  return { tip, calls, app };
};
const NO_MODS = { altKey: false, ctrlKey: false, metaKey: false, shiftKey: false };
// A hover that WILL show now waits out tooltip.js SHOW_DELAY_MS before show()/showLine()
// runs; a hover that hides still does so at once. `arm` must run BEFORE the delayed
// applyHover() call schedules its timer, or `tick` has no mock timer to advance.
const arm = (t) => t.mock.timers.enable({ apis: ['setTimeout'] });
const tick = (t) => t.mock.timers.tick(StencilTooltip.SHOW_DELAY_MS);

test('a point in the EDITED half is labelled; the same point behind the original is not', (t) => {
  arm(t);
  // 212,270 sits right of a centred vertical divider → shown, with the point's coords.
  const shown = hoverHarness({ point: { x: 212, y: 270 } });
  shown.tip.applyHover(500, 400, 210, 268, NO_MODS);
  assert.equal(shown.calls.hide, 0);
  tick(t);
  assert.deepEqual(shown.calls.show, [[212, 270]], 'the POINT is labelled, not the cursor');
  // Move the divider past it and the very same hover shows nothing at all.
  const hidden = hoverHarness({ point: { x: 212, y: 270 }, split: 0.75 });
  hidden.tip.applyHover(500, 400, 210, 268, NO_MODS);
  assert.deepEqual(hidden.calls.show, [], 'a point the user cannot see is never labelled');
  assert.equal(hidden.calls.hide, 1, 'and the tooltip is actively hidden');
  // NON-VACUITY: forcing the gate open must make that same hidden case show. If this
  // ever passes with the gate stubbed true, the test above proved nothing.
  const forced = hoverHarness({ point: { x: 212, y: 270 }, split: 0.75, gate: () => true });
  forced.tip.applyHover(500, 400, 210, 268, NO_MODS);
  tick(t);
  assert.deepEqual(forced.calls.show, [[212, 270]], 'the gate is what decides');
});

test('the gate follows the POINT, not the cursor — a point across the divider stays dark', (t) => {
  arm(t);
  // Cursor at 205 (edited side), point at 195 (original side): the point wins.
  const h = hoverHarness({ point: { x: 195, y: 100 } });
  h.tip.applyHover(500, 400, 205, 100, NO_MODS);
  assert.deepEqual(h.calls.show, []);
  assert.equal(h.calls.hide, 1);
  // …and the mirror image: cursor on the original side, point on the edited side.
  const h2 = hoverHarness({ point: { x: 205, y: 100 } });
  h2.tip.applyHover(500, 400, 195, 100, NO_MODS);
  tick(t);
  assert.deepEqual(h2.calls.show, [[205, 100]]);
});

test('original mode labels nothing; no comparison behaves exactly as before', (t) => {
  arm(t);
  const orig = hoverHarness({ mode: 'original', point: { x: 399, y: 299 } });
  orig.tip.applyHover(1, 1, 399, 299, NO_MODS);
  assert.deepEqual(orig.calls.show, []);
  assert.equal(orig.calls.hide, 1);
  // 'none' → the pre-existing behaviour, unchanged, anywhere on the canvas.
  for (const p of [{ x: 0, y: 0 }, { x: 399, y: 299 }]) {
    const none = hoverHarness({ mode: 'none', point: p });
    none.tip.applyHover(1, 1, p.x, p.y, NO_MODS);
    assert.equal(none.calls.hide, 0);
    tick(t);
    assert.deepEqual(none.calls.show, [[p.x, p.y]]);
  }
});

test('a LINE follows the same rule, judged where the cursor points at it', (t) => {
  arm(t);
  // Hovering the line's visible half → its info shows, exactly as outside compare mode.
  const seen = hoverHarness({ point: null, lineIdx: 0 });
  seen.tip.applyHover(1, 1, 260, 100, NO_MODS);
  tick(t);
  assert.equal(seen.calls.line.length, 1);
  assert.equal(seen.calls.line[0][0], seen.app.lines[0]);
  assert.equal(seen.calls.line[0][1], false, 'Shift still selects the full point list');
  // The same line hovered on the ORIGINAL side of the divider → nothing.
  const dark = hoverHarness({ point: null, lineIdx: 0 });
  dark.tip.applyHover(1, 1, 40, 100, NO_MODS);
  assert.deepEqual(dark.calls.line, []);
  assert.equal(dark.calls.hide, 1);
  // Shift is passed through untouched on the visible side.
  const full = hoverHarness({ point: null, lineIdx: 0 });
  full.tip.applyHover(1, 1, 260, 100, { ...NO_MODS, shiftKey: true });
  tick(t);
  assert.equal(full.calls.line[0][1], true);
});

test('the Ctrl cursor-coordinates tooltip obeys the same visibility', (t) => {
  arm(t);
  const shown = hoverHarness({ point: null });
  shown.tip.applyHover(1, 1, 300, 100, { ...NO_MODS, ctrlKey: true });
  tick(t);
  assert.deepEqual(shown.calls.show, [[300, 100]]);
  const hidden = hoverHarness({ point: null });
  hidden.tip.applyHover(1, 1, 100, 100, { ...NO_MODS, ctrlKey: true });
  assert.deepEqual(hidden.calls.show, []);
  assert.equal(hidden.calls.hide, 1);
  // Alt still wins over everything, as before.
  const alt = hoverHarness({ point: { x: 300, y: 100 } });
  alt.tip.applyHover(1, 1, 300, 100, { ...NO_MODS, altKey: true });
  assert.deepEqual(alt.calls.show, []);
  assert.equal(alt.calls.hide, 1);
});

// ── The reveal delay itself ──────────────────────────────────────────────────
// The line/point tooltip now waits out the same delay as the toolbar/menu tooltip
// (controlTooltip.js SHOW_DELAY_MS) before it appears, so a sweep across the canvas
// doesn't flash a tooltip per pixel. Sweeping onto a NEW target re-arms the wait; staying
// on the SAME one (or a keyboard-triggered refresh) never does.
test('the reveal waits out the toolbar tooltip\'s delay, but only for a genuinely new target', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const h = hoverHarness({ point: { x: 212, y: 270 } });
  h.tip.applyHover(500, 400, 210, 268, NO_MODS);
  assert.deepEqual(h.calls.show, [], 'nothing yet — the delay is still running');
  assert.equal(h.calls.hide, 0, 'nothing was showing, so there is nothing to hide either');
  t.mock.timers.tick(StencilTooltip.SHOW_DELAY_MS - 1);
  assert.deepEqual(h.calls.show, [], 'not quite there');
  t.mock.timers.tick(1);
  assert.deepEqual(h.calls.show, [[212, 270]]);
  // Still hovering the SAME point (a sub-pixel move, say) updates at once — no new wait.
  h.tip.applyHover(501, 401, 210, 268, NO_MODS);
  assert.equal(h.calls.show.length, 2, 'the same target answers immediately once shown');
  // A keyboard-triggered refresh (Shift/Ctrl toggling what's shown for THIS hover) is
  // the `immediate` path and skips the wait outright, exactly like tooltip.js refresh().
  const fresh = hoverHarness({ mode: 'none', point: { x: 50, y: 60 } });
  fresh.tip.applyHover(1, 1, 40, 55, NO_MODS, /* immediate */ true);
  assert.deepEqual(fresh.calls.show, [[50, 60]], 'immediate=true never waits');
});

test('the tooltip formatting is the app\'s own — no second implementation', () => {
  const src = readFileSync(new URL('../js/ui/tooltip.js', import.meta.url), 'utf8');
  // One show()/showLine() pair, using the shared unit helpers; the gate only decides
  // WHETHER to call them.
  assert.equal((src.match(/\n  show\(/g) || []).length, 1);
  assert.equal((src.match(/\n  showLine\(/g) || []).length, 1);
  assert.match(src, /import \{ cmToUnit, unitLabel \} from '\.\.\/utils\.js';/);
  assert.ok(!/compareEditedShows/.test(src), 'the geometry lives once, in the app');
  // …and the app method is the single caller of the shared predicate.
  const app = readFileSync(new URL('../js/core/drawingApp.js', import.meta.url), 'utf8');
  assert.equal((app.match(/compareEditedShows\(/g) || []).length, 1);
});
