// Arriving content (js/ui/motion.js): the drop-point flight, the reversed sweep, the dust
// grid over the visible slice, and the ghost in/out guards.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';
import {
  flipTransform, arriveFrom, arrivalBox, ARRIVE_GLOW_CLASS, LANDING_CLASS, ARRIVE_ACTIVE_CLASS,
  dustDelay, dustEase, dustGrid, dustVisibleBox, pinDustStage, ghostIn, ghostOut, tileNoise,
  DUST_CELL_PX, DUST_MAX_PARTICLES, playCanvasArrival,
} from '../js/ui/motion.js';
import { ANIMATIONS_CSS } from './helpers/css.js';
import { box, el } from './helpers/motionRig.js';

// ── Arriving: dropped content plays in out of the drop point ────────────────
// Removal already had motion (leaveThenRemove/disintegrate); an upload just appeared.
test('arrivalBox is a small box centred on the drop point', () => {
  const b = arrivalBox({ x: 400, y: 300 }, 96);
  assert.equal(b.left + b.width / 2, 400, 'centred horizontally on the drop');
  assert.equal(b.top + b.height / 2, 300 - 12, 'and near it vertically');
  assert.ok(b.width > 0 && b.height > 0, 'non-degenerate, or flipTransform declines to play it');
  // Small relative to a canvas, so the content visibly grows out of the cursor.
  assert.ok(b.width <= 120 && b.height <= 120);
});

test('a drop with a point flies in; one without falls back to the plain landing', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const priorRaf = globalThis.requestAnimationFrame;
  globalThis.requestAnimationFrame = () => 0;   // the FLIP schedules on frames; don't run them
  const mk = () => ({
    ...el(),
    style: {},
    getBoundingClientRect: () => box(100, 200, 800, 500),
  });
  try {
    const dropped = mk();
    arriveFrom(dropped, { x: 420, y: 640 });
    assert.ok(dropped.has(ARRIVE_GLOW_CLASS), 'glows on arrival');
    assert.ok(!dropped.has(LANDING_CLASS), 'but not the scale-up: the FLIP owns the transform');
    assert.ok(dropped.has(ARRIVE_ACTIVE_CLASS), 'and is lifted for the flight');
    assert.match(dropped.style.transform, /^translate\(-?[\d.]+px, -?[\d.]+px\) scale\(/,
      'starts drawn at the drop point, then transitions to none');

    const opened = mk();
    arriveFrom(opened, null);
    assert.ok(opened.has(LANDING_CLASS), 'no drop point (dialog / paste) → the plain landing');
    assert.ok(!opened.has(ARRIVE_GLOW_CLASS));
    assert.equal(opened.style.transform, undefined, 'and nothing flies');
  } finally { globalThis.requestAnimationFrame = priorRaf; }
});

test('arriveFrom never throws on a missing element or a half-formed point', () => {
  assert.doesNotThrow(() => {
    arriveFrom(null, { x: 1, y: 2 });
    arriveFrom(el(), { x: NaN, y: 2 });
    arriveFrom(el(), {});
  });
});

test('animations.css: the arriving canvas glows only — the flight is the inline FLIP', () => {
  const css = ANIMATIONS_CSS;
  const arriving = css.slice(css.indexOf('.canvas-container.drop-arriving {'));
  const rule = arriving.slice(0, arriving.indexOf('}'));
  assert.match(rule, /animation: canvasLandGlow/, 'the accent pulse still plays');
  assert.ok(!/canvasLand /.test(rule),
    'canvasLand animates transform too and would win over the FLIP, snapping the flight away');
  const lift = css.slice(css.indexOf('.canvas-container.arrive-active {'));
  assert.match(lift.slice(0, lift.indexOf('}')), /z-index/,
    'the shrunken start must paint above the page it grows out of');
});

// ghostIn is ghostOut rewound: the same grid, per-mote noise and sweep, opposite direction.
test('the arrival sweep is the departure sweep, reversed', () => {
  const rows = 9;
  for (let cy = 0; cy < rows; cy++) {
    const n = tileNoise(cy, 3);
    // The row that leaves FIRST (delay 0 out) is the last one home, and vice versa.
    assert.ok(Math.abs(dustDelay(cy, rows, n) + dustDelay(rows - 1 - cy, rows, n)
      - (0.55 + 2 * n * 0.12)) < 1e-9, `row ${cy} mirrors its opposite`);
    assert.equal(dustDelay(cy, rows, n, true), dustDelay(rows - 1 - cy, rows, n),
      'reversing the sweep is the same as reading the rows backwards');
  }
  // A single row has nothing to sweep across, and must not divide by zero.
  assert.equal(dustDelay(0, 1, 0), 0);
});

test('a mote covers most of its flight early and settles', () => {
  assert.equal(dustEase(0), 0);
  assert.equal(dustEase(1), 1);
  assert.ok(dustEase(0.5) > 0.85, 'most of the distance by halfway');
  for (let k = 0.1; k < 1; k += 0.1) assert.ok(dustEase(k) > dustEase(k - 0.1), 'monotonic');
});

// Zoom scales the canvas's CSS box, not the viewport frame, so the grid is clipped to the
// frame first and mote size stays zoom-independent.
test('the dust grid over the visible slice is the same at any zoom', () => {
  const frame = box(0, 0, 800, 600);
  const fitted = dustVisibleBox(box(0, 0, 800, 600), frame);        // 100%
  const zoomed = dustVisibleBox(box(-1600, -1200, 4000, 3000), frame); // 500%, panned
  assert.deepEqual(fitted, { left: 0, top: 0, width: 800, height: 600 });
  assert.deepEqual(zoomed, fitted, 'only the slice inside the frame plays');
  assert.deepEqual(dustGrid(zoomed.width, zoomed.height),
    dustGrid(fitted.width, fitted.height), 'so the grid — and the mote size — match');
  // The old whole-box grid is what coarsened the motes: at 500% it is much sparser.
  const whole = dustGrid(4000, 3000);
  const vis = dustGrid(800, 600);
  assert.ok(4000 / whole.cols > 3 * (800 / vis.cols), 'gridding the whole box gives big flakes');
});

test('dustVisibleBox clips each edge independently', () => {
  const frame = box(100, 50, 400, 300);
  assert.deepEqual(dustVisibleBox(box(150, 80, 100, 100), frame),
    { left: 150, top: 80, width: 100, height: 100 }, 'fully inside: untouched');
  assert.deepEqual(dustVisibleBox(box(0, 0, 1000, 1000), frame),
    { left: 100, top: 50, width: 400, height: 300 }, 'covering the frame: the frame');
  const off = dustVisibleBox(box(600, 50, 100, 100), frame);
  assert.ok(off.width <= 0, 'scrolled clean out of the frame: nothing left to play');
});

// The stage lives inside the scrolling viewport, so it is pinned over the frame: a scroll
// (a project restore at high zoom) would carry the whole cloud off-screen.
test('a scroll under a flying stage re-anchors it to the frame', () => {
  const listeners = {};
  const host = {
    scrollLeft: 0, scrollTop: 0,
    addEventListener: (ev, fn) => { listeners[ev] = fn; },
    removeEventListener: (ev) => { delete listeners[ev]; },
  };
  const stage = { style: {} };
  const unpin = pinDustStage(stage, host, 10, 20);
  host.scrollLeft = 35232; host.scrollTop = 50516;   // the restore's saved-scroll jump
  listeners.scroll();
  assert.equal(stage.style.left, '35242px', 'base offset + the new scroll');
  assert.equal(stage.style.top, '50536px');
  unpin();
  assert.ok(!listeners.scroll, 'removed with the stage — no listener left on the viewport');
});

test('the restore sets the saved scroll BEFORE raising the arrival, in the same tick', () => {
  const src = readFileSync(new URL('../js/core/storage/storage.js', import.meta.url), 'utf8');
  const scrollAt = src.indexOf('scrollViewportTo(layout.scrollLeft, layout.scrollTop)');
  const arrivalAt = src.indexOf('playCanvasArrival(this.app.canvas)');
  assert.ok(scrollAt > -1 && arrivalAt > -1 && scrollAt < arrivalAt,
    'saved scroll applied before the dust snapshots the view');
  assert.ok(!/requestAnimationFrame[\s\S]{0,200}scrollViewportTo/.test(src),
    'and not deferred a frame — that jumped the viewport out from under the cloud');
});

test('dustGrid aims at DUST_CELL_PX and respects the particle ceiling', () => {
  const small = dustGrid(300, 240);
  assert.equal(small.cols, Math.round(300 / DUST_CELL_PX));
  assert.equal(small.rows, Math.round(240 / DUST_CELL_PX));
  const big = dustGrid(3000, 2400);
  assert.ok(big.cols * big.rows <= DUST_MAX_PARTICLES, 'thinned under the ceiling');
});

test('ghostOut declines the same way, so the emptied editor is not held back for nothing', () => {
  assert.equal(ghostOut(null), false, 'no canvas');
  assert.equal(ghostOut({ width: 0, height: 0 }), false, 'nothing to snapshot');
  const prior = globalThis.matchMedia;
  globalThis.matchMedia = () => ({ matches: true });
  try {
    assert.equal(ghostOut({ width: 100, height: 80, parentElement: {} }), false, 'reduced motion');
  } finally { globalThis.matchMedia = prior; }
});

test('ghostIn declines rather than hiding a canvas it cannot animate', () => {
  // Every bail-out matters: the caller only hides the real canvas when this says yes,
  // so a false negative is a blank editor.
  assert.equal(ghostIn(null), false, 'no canvas');
  assert.equal(ghostIn({ width: 0, height: 0 }), false, 'nothing painted yet');
  const prior = globalThis.matchMedia;
  globalThis.matchMedia = () => ({ matches: true });
  try {
    assert.equal(ghostIn({ width: 100, height: 80, parentElement: {} }), false, 'reduced motion');
  } finally { globalThis.matchMedia = prior; }
});
