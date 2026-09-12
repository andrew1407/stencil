// Scroll reveal + drop landing (js/ui/motion.js) and the CSS that drives them.
// Node has no IntersectionObserver, so the observer's behaviour is pinned against a
// minimal stub DOM + stub observers (the chat-markup.test.js convention); the CSS
// contract is asserted by reading the stylesheet, like the other style tests.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

import {
  observeReveal, flashLanding, flipTransform, revealDissolve, revealGrain,
  FLIP_MS, FLIP_EASING, FLIP_ACTIVE_CLASS,
  themeSwap, swapRadius, swapPercent, originOf, THEME_SWAP_MS, THEME_SWAP_CLASS,
  swapEase, swapDustSpecs, swapDustFrame, SWAP_DUST_FLARE,
  SWAP_DUST_MOTES, SWAP_DUST_LIFE_MS, SWAP_DUST_MIN_T, SWAP_DUST_MAX_T,
  swapEdgePolygon, SWAP_EDGE_POINTS,
  originOfId, arriveFrom, arrivalBox, ARRIVE_GLOW_CLASS, LANDING_CLASS, ARRIVE_ACTIVE_CLASS,
  dustDelay, dustEase, dustGrid, dustVisibleBox, pinDustStage, ghostIn, ghostOut, hasPixels, tileNoise,
  DUST_CELL_PX, DUST_MAX_PARTICLES,
  playCanvasArrival, ASSEMBLING_CLASS, CLEARING_CLASS, GHOST_MS,
  createFilterAnimator, FILTER_ENTERING_CLASS, FILTER_ENTER_MS,
  tileWaypoint, tileMotion, surfaceMotion, WAYPOINT_ALONG, SWIRL_SHARE, SWIRL_MAX_PX,
  DUST_ALPHA_LEVELS, DISINTEGRATE_MS, MIN_TILE_MS,
} from '../js/ui/motion.js';
import { STYLE_DUST, STYLE_WATER, STYLE_FIRE, edgeBaseOf, FILL_CHUNK } from '../js/ui/dustCloud.js';
import { FLIGHTS, moteFrame } from '../js/ui/dustCloud.js';
import { motionSource } from './helpers/motionSource.js';
import { COMPONENTS_CSS, ANIMATIONS_CSS } from './helpers/css.js';

const box = (left, top, width, height) => ({ left, top, width, height });

// ── The reveal ramp ────────────────────────────────────────────────────────
// Dissolve tracks ONLY the share of a row the scroller is already clipping. The
// earlier design dissolved anything inside a "band" of the visible area, which made
// fully-readable messages into an unreadable dot screen — the grain is finer than a
// glyph's strokes. Decoration must never cost legibility.
const H = 800;

// A minimal element stand-in for the class-toggling helpers below.
const el = (cls = '') => {
  const classes = new Set(cls ? cls.split(' ') : []);
  return {
    offsetWidth: 0,
    classList: {
      add: (...c) => c.forEach((x) => classes.add(x)),
      remove: (...c) => c.forEach((x) => classes.delete(x)),
      contains: (c) => classes.has(c),
      toggle: (c, on) => (on ? classes.add(c) : classes.delete(c)),
    },
    has: (c) => classes.has(c),
  };
};

test('a row you can see in FULL is never dissolved at all', () => {
  assert.equal(revealDissolve(0, 60, H), 0, 'flush with the top edge');
  assert.equal(revealDissolve(300, 400, H), 0, 'mid-scroller');
  assert.equal(revealDissolve(H - 50, H, H), 0, 'flush with the bottom edge');
  assert.equal(revealDissolve(0, H, H), 0, 'exactly filling the scroller');
});

test('dissolve equals the clipped share, at either edge', () => {
  assert.equal(revealDissolve(-50, 50, H), 0.5, 'half cut off the top');
  assert.equal(revealDissolve(H - 50, H + 50, H), 0.5, 'half cut off the bottom');
  assert.ok(Math.abs(revealDissolve(-75, 25, H) - 0.75) < 1e-9, 'three quarters gone');
});

test('a row fully out of view is fully dissolved, both ways', () => {
  assert.equal(revealDissolve(-200, -50, H), 1);
  assert.equal(revealDissolve(H + 20, H + 120, H), 1);
});

test('a row taller than the scroller is only dissolved by what hangs off', () => {
  // Half of a 1600px row is on screen, so half of it is dissolved.
  assert.equal(revealDissolve(-800, 800, H), 0.5);
});

// The grain covers the whole element, so it cannot follow the clipped share: a message
// taller than the scroller is clipped however you scroll it, and would sit there
// speckled and unreadable. Legibility outranks the decoration.
test('a row taller than the scroller gets no grain while it fills the view', () => {
  assert.equal(revealGrain(-800, 800, H), 0, 'a 1600px row filling the scroller');
  assert.equal(revealGrain(0, 1600, H), 0, 'its top edge in view, the rest below');
  assert.ok(revealDissolve(-800, 800, H) > 0, 'the soft edge still tracks the clipping');
});

test('grain equals the clipped share for rows that fit', () => {
  assert.equal(revealGrain(300, 400, H), 0);
  assert.equal(revealGrain(-50, 50, H), 0.5);
  assert.equal(revealGrain(H - 50, H + 50, H), 0.5);
});

test('a tall row grains only as it leaves the view', () => {
  assert.equal(revealGrain(-1400, 200, H), 0.75, '200 of a possible 800px still showing');
  assert.equal(revealGrain(-1700, -100, H), 1, 'gone');
  assert.equal(revealGrain(0, 1600, 0), 0, 'a zero-height scroller is not a guess');
});

test('degenerate inputs never dissolve anything', () => {
  assert.equal(revealDissolve(0, 50, 0), 0, 'a zero-height scroller is not a guess');
  assert.equal(revealDissolve(50, 50, H), 0, 'an empty row');
});

test('observeReveal is inert without requestAnimationFrame', () => {
  const prior = globalThis.requestAnimationFrame;
  delete globalThis.requestAnimationFrame;
  try {
    const stop = observeReveal({ addEventListener() {} }, '.row');
    assert.equal(typeof stop, 'function');
    stop();
  } finally { globalThis.requestAnimationFrame = prior; }
});

test('flashLanding is restart-safe and clears itself', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const node = el();
  flashLanding(node, 'drop-landing', 700);
  assert.ok(node.has('drop-landing'));
  // A second drop before the first expires replays it rather than doing nothing.
  flashLanding(node, 'drop-landing', 700);
  assert.ok(node.has('drop-landing'));
  t.mock.timers.tick(699);
  assert.ok(node.has('drop-landing'), 'the first drop\'s timer was cancelled, not left to fire early');
  t.mock.timers.tick(2);
  assert.ok(!node.has('drop-landing'));
});

// The canvas viewport carries two of these at once — canvas-clearing while the old
// image's dust falls, canvas-assembling while the new one's gathers. With one shared
// timer the second call cancelled the first one's removal and left a class that hides
// the canvas on it permanently.
test('flashLanding tracks a timer per class, not per element', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const node = el();
  flashLanding(node, 'canvas-clearing', 1100);
  t.mock.timers.tick(200);
  flashLanding(node, 'canvas-assembling', 1100);   // must NOT cancel the first removal
  assert.ok(node.has('canvas-clearing') && node.has('canvas-assembling'));
  t.mock.timers.tick(901);
  assert.ok(!node.has('canvas-clearing'), 'the first class still expires on its own clock');
  assert.ok(node.has('canvas-assembling'), 'while the second is still running');
  t.mock.timers.tick(200);
  assert.ok(!node.has('canvas-assembling'), 'and then it goes too');
});

test('flashLanding ignores a missing node', () => {
  assert.doesNotThrow(() => { flashLanding(null); flashLanding(undefined); });
});

// ── FLIP (the fullscreen stretch / minimise) ────────────────────────────────
test('flipTransform inverts the new box back onto the old one', () => {
  // Entering fullscreen: an 800x400 viewport at (40, 120) becomes the whole window.
  const t = flipTransform(box(40, 120, 800, 400), box(0, 0, 1600, 800));
  assert.equal(t, 'translate(40px, 120px) scale(0.5, 0.5)',
    'the fullscreen box starts drawn exactly where the old one was');
});

test('flipTransform runs the other way for the minimise back to the canvas', () => {
  const t = flipTransform(box(0, 0, 1600, 800), box(40, 120, 800, 400));
  assert.equal(t, 'translate(-40px, -120px) scale(2, 2)',
    'the restored box starts drawn at full-window size and shrinks in');
});

test('flipTransform declines when there is nothing to play', () => {
  assert.equal(flipTransform(box(0, 0, 100, 50), box(0, 0, 100, 50)), null, 'identical boxes');
  assert.equal(flipTransform(box(0, 0, 100, 50), box(0.2, -0.3, 100.02, 50.01)), null,
    'a sub-pixel difference is not an animation');
  assert.equal(flipTransform(null, box(0, 0, 100, 50)), null, 'no starting box (never measured)');
  assert.equal(flipTransform(box(0, 0, 0, 0), box(0, 0, 100, 50)), null, 'a collapsed starting box');
  assert.equal(flipTransform(box(0, 0, 100, 50), box(0, 0, 100, 0)), null, 'a collapsed target box');
});

test('the flight is long and hard-eased-out, and CSS lifts the element for it', () => {
  assert.ok(FLIP_MS >= 500, 'a full-window stretch needs room to read as deliberate');
  // An ease-out whose first control point is low and second is pinned at 1: most of
  // the distance early, coasting into the landing.
  const [x1, y1, , y2] = FLIP_EASING.match(/[\d.]+/g).map(Number);
  assert.ok(x1 < 0.3 && y1 > 0.9 && y2 === 1, `${FLIP_EASING} should ease hard out`);

  const css = COMPONENTS_CSS;
  const flight = css.slice(css.indexOf(`.canvas-viewport.${FLIP_ACTIVE_CLASS} {`));
  // Scrollbars belong to the settled state — appearing/disappearing mid-scale IS the flicker.
  assert.ok(/overflow: hidden !important/.test(flight.slice(0, 300)), 'no scrollbars mid-flight');
  // LEAVING starts scaled far beyond the in-flow box, so it must paint over the page
  // it is shrinking away from — otherwise it slides under the toolbars.
  const lifted = css.slice(css.indexOf(`body:not(.fullscreen-mode) .canvas-viewport.${FLIP_ACTIVE_CLASS} {`));
  assert.ok(/position: relative/.test(lifted.slice(0, 300)) && /z-index: 100\d\d/.test(lifted.slice(0, 300)),
    'the shrinking viewport is lifted above the restored page chrome');
});

// ── The CSS half of the contract ────────────────────────────────────────────
test('animations.css: reveal rest state, drop landing, and reduced-motion opt-outs', () => {
  const css = ANIMATIONS_CSS;
  const rest = css.slice(css.indexOf('.reveal-item.reveal-masked {'),
                         css.indexOf('\n}', css.indexOf('.reveal-item.reveal-masked {')));
  const base = css.slice(css.indexOf('.reveal-item {'), css.indexOf('\n}', css.indexOf('.reveal-item {')));
  // The mask is mounted only on rows straddling an edge: four gradient layers on every
  // row is what made a long transcript stutter while scrolling.
  assert.ok(!/mask-image/.test(base), 'a settled row composites no mask at all');
  // Scrolling is continuous, so the reveal CANNOT clone per particle the way a one-shot
  // removal does — a 20-row viewport would churn thousands of nodes. It is a mask
  // dissolve instead: one element, no extra DOM.
  assert.ok(/--dissolve: 1/.test(base), 'out-of-view rows rest fully dissolved');
  // The mask keeps the STILL-VISIBLE span solid, so only the clipped edge is sanded.
  assert.ok(/var\(--vis-start/.test(rest) && /var\(--vis-end/.test(rest),
    'the wipe is anchored to the visible span, not to the row');
  assert.ok(/transform: none/.test(base), 'and a readable row is never displaced');
  // --dissolve must be REGISTERED or it cannot be transitioned — it would jump.
  assert.ok(/@property --dissolve \{ syntax: "<number>"/.test(css), '--dissolve is a registered property');
  assert.ok(/transition: --dissolve/.test(base), 'the dissolve is a transition, not a jump');
  // Union, not intersect: intersect would punch dot-holes through a settled row.
  assert.ok(/mask-composite: add/.test(rest), 'the grain and the wipe are UNIONed');
  assert.ok(/radial-gradient/.test(rest), 'a tiled dot grain');
  assert.ok(/linear-gradient\(to bottom, transparent 0, transparent var\(--vis-start/.test(rest),
    'plus a wipe spanning the still-visible run of the row');
  assert.ok(/\.reveal-item\.reveal-in \{ --dissolve: 0; transform: none; \}/.test(css), 'revealed rows are whole');
  // Transcript rows must NOT also carry an `animation: … both`: its filled end state
  // would out-rank the reveal's transform and pin every row at rest.
  assert.ok(!/\.chat-msg[^{]*\{[^}]*animation: chatMsgIn/.test(css), 'chat rows ride the reveal, not chatMsgIn');
  assert.ok(/\.chat-attach-chip \{ animation: chatMsgIn/.test(css), 'attachment chips keep their own entrance');
  // Drop landing: the overlay animates out (click-through while it does) and the canvas in.
  assert.ok(/#global-drop-overlay\.drop-closing \{[\s\S]*?pointer-events: none/.test(css),
    'the closing overlay cannot swallow the drop it is dismissing for');
  assert.ok(/animation: dropOverlayOut/.test(css), 'overlay leaves on an animation');
  assert.ok(/\.canvas-container\.drop-landing \{[\s\S]*?animation: canvasLand/.test(css), 'the canvas reveals on a drop');
  // Every piece of this collapses under prefers-reduced-motion.
  assert.ok(/@media \(prefers-reduced-motion: reduce\) \{[\s\S]{0,300}?\.reveal-item \{ --dissolve: 0 !important;[\s\S]{0,160}?mask-image: none !important; \}/.test(css),
    'reduced motion shows every row whole, mask and all');
  const reduced = css.slice(css.indexOf('@media (prefers-reduced-motion: reduce) {',
    css.indexOf('.canvas-container.drop-landing')));
  const block = reduced.slice(0, reduced.indexOf('}\n'));
  for (const sel of ['#global-drop-overlay.drop-closing', '.canvas-container.drop-landing',
                     '.canvas-container.drop-arriving'])
    assert.ok(block.includes(sel), `reduced motion drops ${sel}`);
});

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

// ── The image assembles the way it comes apart ──────────────────────────────
// ghostOut blows the canvas away as dust; ghostIn is that same fall rewound. The two
// share the grid, the per-mote noise and the sweep — only the direction differs.
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

// ── Mote size must not follow the zoom ──────────────────────────────────────
// Zoom scales the canvas's CSS box, not the viewport frame. Regression: the grid was
// laid over the WHOLE canvas box, so a zoomed-in canvas tripped the particle ceiling
// and the thinning loop handed back big flakes — mote size rose with the zoom level.
// Clipping to the frame first keeps the grid viewport-bounded at any zoom.
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

// The stage lives INSIDE the scrolling viewport, so a scroll would carry it away from
// the frame. Regression: a project restore jumps to its saved scroll right after the
// arrival goes up, and at high zoom that moved the whole cloud off-screen — the exact
// "no animation at all at extreme zoom" report. The pin keeps it over the frame.
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
  const src = readFileSync(new URL('../js/core/storage.js', import.meta.url), 'utf8');
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

// ── The arrival is played from ONE place, by every route ────────────────────
// Regression: the arrival lived inline in the file loader only, so REOPENING a saved
// project painted the picture with no motion at all — the exact "nothing plays when an
// image appears" report. Both routes go through playCanvasArrival now; these tests fail
// if either stops calling it, or if the helper stops animating.
test('playCanvasArrival gathers the dust when it can, and hides the canvas for exactly that long', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const vp = el();
  const box = el();
  assert.equal(playCanvasArrival({}, { viewport: vp, container: box, ghost: () => true }), 'dust');
  assert.ok(vp.has(ASSEMBLING_CLASS), 'the viewport holds the canvas back while the motes gather');
  assert.ok(!box.has(LANDING_CLASS), 'and the flight is NOT played on top of it');
  t.mock.timers.tick(GHOST_MS - 1);
  assert.ok(vp.has(ASSEMBLING_CLASS), 'still up while the dust flies');
  t.mock.timers.tick(2);
  assert.ok(!vp.has(ASSEMBLING_CLASS), 'and the canvas is handed back at the end');
});

test('playCanvasArrival falls back to the landing flight when the dust cannot play', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const vp = el();
  const box = el();
  assert.equal(playCanvasArrival({}, { viewport: vp, container: box, ghost: () => false }), 'flight');
  assert.ok(box.has(LANDING_CLASS), 'the container plays the plain landing instead');
  assert.ok(!vp.has(ASSEMBLING_CLASS), 'and the canvas is never hidden — there is no dust to hide behind');
  t.mock.timers.tick(701);
  assert.ok(!box.has(LANDING_CLASS), 'one shot, then the class comes off');
});

test('playCanvasArrival: reduced motion still ends in the right state', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const prior = globalThis.matchMedia;
  globalThis.matchMedia = () => ({ matches: true });
  try {
    const vp = el();
    const box = el();
    // The real ghostIn — it is what declines under the preference, and the canvas must
    // NOT be hidden when nothing is going to gather in front of it.
    assert.equal(playCanvasArrival({ width: 100, height: 80, parentElement: {} },
      { viewport: vp, container: box }), 'flight');
    assert.ok(!vp.has(ASSEMBLING_CLASS), 'never hidden behind dust that will not play');
    t.mock.timers.tick(701);
    assert.ok(!box.has(LANDING_CLASS), 'and no class is left behind');
  } finally { globalThis.matchMedia = prior; }
});

test('playCanvasArrival survives having no DOM to reach for', () => {
  assert.doesNotThrow(() => playCanvasArrival(null));
});

test('both routes that put an image on the canvas play the arrival', () => {
  const loader = readFileSync(new URL('../js/core/imageSettle.js', import.meta.url), 'utf8');
  assert.match(loader, /playCanvasArrival\(app\.canvas, \{ from: opts\.from \}\)/,
    'a freshly loaded file arrives');
  const storage = readFileSync(new URL('../js/core/storage.js', import.meta.url), 'utf8');
  // The open passes it; the cross-tab sync path (the other caller) deliberately does not.
  assert.match(storage, /this\.loadPayloadIntoApp\(proj\.payload, \{ landing: true \}\)/,
    'opening a saved project arrives too — this is the regression');
  assert.match(storage, /if \(landing\) playCanvasArrival\(this\.app\.canvas\);/,
    'and it is played once the restored image is in the backing store');
  assert.match(storage, /syncActiveFromStorage\(\)[\s\S]*?this\.loadPayloadIntoApp\(payload\);/,
    'a peer\'s edit syncing in from another tab stays still — no landing option');
  // The clear-dissolve twin runs off the same pair of class names; keep them in step.
  assert.equal(CLEARING_CLASS, 'canvas-clearing');
  assert.equal(ASSEMBLING_CLASS, 'canvas-assembling');
  assert.match(storage, /flashLanding\(vp, 'canvas-clearing', GHOST_MS\)/,
    'and the clear still dissolves the outgoing image');
});

test('hasPixels tells a painted canvas from an empty one', () => {
  const ctx = (fill) => ({ getImageData: (_x, _y, w, h) => ({ data: fill(w * h * 4) }) });
  const empty = ctx((n) => new Uint8ClampedArray(n));
  assert.equal(hasPixels(empty, 400, 300), false, 'a blank canvas has nothing to make motes of');
  const painted = ctx((n) => { const d = new Uint8ClampedArray(n); d.fill(255); return d; });
  assert.equal(hasPixels(painted, 400, 300), true);
  // One opaque pixel is still an image — but the stride must actually be able to see it,
  // so the sample walks the buffer rather than peeking at the corner.
  const sparse = ctx((n) => { const d = new Uint8ClampedArray(n); d[4 * 41 * 7 + 3] = 255; return d; });
  assert.equal(hasPixels(sparse, 400, 300), true);
  assert.equal(hasPixels(null, 10, 10), false, 'no context');
  assert.equal(hasPixels(empty, 0, 0), false, 'no box');
});

// ── Icon hover ──────────────────────────────────────────────────────────────
// The shimmer sweeps the BUTTON; nothing moved the glyph, so an icon-only control had
// no hover motion of its own. A single shared turn-and-swell was the first answer and
// the WRONG one — a uniform tilt means nothing, and on `minus` a glyph that swells reads
// as "increase". Each icon now mimes its own action instead (config/iconMotion.json is
// the canonical table; tests/iconMotion.test.js pins it). What stays shared is the
// TRIGGER: one rule flips the `--ic-on` latch and switches on `--ic-play`.
test('animations.css: one trigger drives every icon, and the generic tilt is gone', () => {
  const css = ANIMATIONS_CSS;
  assert.ok(!/rotate\(-7deg\) scale\(1\.14\)/.test(css),
    'the one-size-fits-all tilt+swell must not come back');
  // Two selector branches sharing one declaration block, not two rules: a plain
  // descendant match for controls that can never nest a submenu, and a SCOPED
  // (`> .ctx-icon`/`> .ctx-check`) branch for `.ctx-item` — which CAN carry a nested
  // `.ctx-sub` flyout as a DOM child (Image / Layout, Copy Image, Style, …), so a plain
  // descendant match there reached every icon inside that flyout the instant the OUTER
  // row was hovered, not just the row the pointer was actually on.
  const triggerStart = css.indexOf(':is(button, .btn-icon');
  assert.ok(triggerStart >= 0, 'the icon-hover trigger exists');
  const triggerBraceAt = css.indexOf('{', triggerStart);
  const selector = css.slice(triggerStart, triggerBraceAt);
  const body = css.slice(triggerBraceAt + 1, css.indexOf('}', triggerBraceAt));
  assert.match(selector, /:hover\s*\n\s*:is\(\.ic, \.draw-mode-icon, \.ic \*, \.draw-mode-icon \*\)/,
    'the general branch reaches the PARTS, not just the glyph');
  assert.match(selector, /\.ctx-item:not\(\.is-loading\):not\(\.swapping\):not\(\[id\^="toggle-"\]\):hover\s*\n\s*> :is\(\.ctx-icon, \.ctx-check\)/,
    "a submenu parent's hover reaches only its OWN icon, never a nested flyout's");
  assert.match(body, /--ic-on: 1;/, 'it flips the hold latch');
  assert.match(body, /animation-name: var\(--ic-play, none\);/, 'and starts the settle play');
  assert.ok(!/transform:/.test(body),
    'the trigger itself moves nothing — the per-icon rules do');
  // The fold chevrons opt out as an ATTRIBUTE, so the rule stays at class specificity
  // and the reduced-motion override below can still outrank it.
  assert.match(css, /:not\(\[id\^="toggle-"\]\):hover/, 'the fold chevrons keep their own idiom');
  assert.ok(!/:not\(#toggle-coord-panel\)/.test(css),
    'as an attribute, so the rule stays at class specificity');
  // A face mid-swap owns its own animation-name; the trigger must not steal it.
  assert.match(css, /:not\(\.is-loading\):not\(\.swapping\)/);
  // A glyph already animating owns its transform outright.
  assert.match(css, /\.is-loading \.ic, \.is-loading \.ic \*, \.swapping > svg, \.swapping > svg \* \{ transition: none; \}/);
});

test('animations.css: reduced motion cancels the icon hover but not the chevrons’ state', () => {
  const css = ANIMATIONS_CSS;
  const block = css.match(/@media \(prefers-reduced-motion: reduce\) \{[\s\S]*?--ic-on: 0 !important;[\s\S]*?\n\}/);
  assert.ok(block, 'the hover move is cancelled under the preference');
  // Killing the latch and the keyframe switch leaves every glyph in its rest pose —
  // which is also where every motion here ends, so the end state stays correct.
  assert.match(block[0], /animation-name: none !important;/);
  assert.match(block[0], /transition: none !important;/);
  // The rotate(180deg) that says "this panel is folded" is STATE. An `!important`
  // transform reset here would flatten it and the chevron would point the wrong way.
  assert.ok(!/transform: none !important/.test(block[0]),
    'nothing here outranks the fold chevrons’ own rotation');
});

test('animations.css: the canvas waits behind its own dust, both directions', () => {
  const css = ANIMATIONS_CSS;
  assert.match(css, /\.canvas-viewport\.canvas-clearing #canvas \{ opacity: 0; \}/,
    'hidden while the dust falls');
  assert.match(css, /\.canvas-viewport\.canvas-assembling #canvas \{ opacity: 0; transition: none; \}/,
    'and while the dust gathers — or the finished picture sits behind its own motes');
  // …and it must go out INSTANTLY. The shared transition below is for the fade back UP;
  // when it applied to the hide as well, a pasted image stood there at full strength for
  // its whole 280ms while its own dust flew at it — the blink in the bug report.
  assert.match(css, /\.canvas-viewport \.idle-create, \.canvas-viewport #canvas \{ transition: opacity/,
    'the fade back up is still a transition');
});

// ── Theme / accent swap ─────────────────────────────────────────────────────
// themeSwap runs `apply` exactly once and synchronously on EVERY path — the palette
// write is the contract, the wipe is decoration layered on top of it.
const withDoc = (doc, fn) => {
  const prior = globalThis.document;
  const priorMM = globalThis.matchMedia;
  globalThis.document = doc;
  globalThis.matchMedia = () => ({ matches: false });
  try { return fn(); } finally { globalThis.document = prior; globalThis.matchMedia = priorMM; }
};
const rootStub = () => {
  const classes = new Set();
  const props = {};
  let sawInstant = false;
  rootStub.lastAnimate = undefined;
  return {
    classList: {
      add: (c) => { classes.add(c); if (c === 'theme-instant') sawInstant = true; },
      remove: (c) => classes.delete(c), contains: (c) => classes.has(c),
    },
    style: { setProperty: (k, v) => { props[k] = v; } },
    get sawInstantDuringApply() { return sawInstant; },
    animate: (...args) => { rootStub.lastAnimate = args; },
    props,
    has: (c) => classes.has(c),
  };
};

test('swapRadius reaches the furthest viewport corner', () => {
  // From a corner the whole diagonal is needed; from the centre, half of it.
  assert.equal(swapRadius(0, 0, 300, 400), 500);
  assert.equal(swapRadius(300, 400, 300, 400), 500);
  assert.equal(swapRadius(150, 200, 300, 400), 250);
  // Off-centre: the far side wins on each axis independently.
  assert.equal(swapRadius(60, 400, 300, 400), Math.hypot(240, 400));
});

// The circle is handed over in PERCENTAGES of the viewport, because clip-path resolves
// them against the pseudo-element's own box: an engine that measures that box in device
// pixels paints a px origin at half its offset, which is the wipe blooming above and to
// the left of the button instead of out of it.
test('swapPercent expresses the circle as percentages of the viewport', () => {
  assert.deepEqual(swapPercent(0, 0, 300, 400), { x: 0, y: 0, r: 141.421 });
  assert.deepEqual(swapPercent(150, 200, 300, 400), { x: 50, y: 50, r: 70.711 });
  // A percentage radius resolves against sqrt(w² + h²) / √2 — 500px of a 300x400 box is
  // 141.421% of it, and covers the far corner exactly as the pixel radius did.
  const { r } = swapPercent(60, 400, 300, 400);
  assert.equal(Math.round((r / 100) * (Math.hypot(300, 400) / Math.SQRT2)),
    Math.round(swapRadius(60, 400, 300, 400)));
  // A viewport that cannot be measured (a stub) falls back to a centred, page-covering wipe.
  assert.deepEqual(swapPercent(10, 10, 0, 0), { x: 50, y: 50, r: 150 });
});

// ── The ring the dust rides ─────────────────────────────────────────────────
// swapEase is the JS evaluation of the wipe's own control points — the desktop solves
// the same bezier (themeSwapOverlay.hpp swapEase, pinned by themeSwapEase.headless.cpp).
// The dust is seeded off this curve, so it and the clip-path can never disagree.
test('swapEase walks the wipe’s own curve, easing in slightly and never backwards', () => {
  assert.ok(Math.abs(swapEase(0)) < 1e-6, 'starts at the origin');
  assert.ok(Math.abs(swapEase(1) - 1) < 1e-6, 'ends at the full radius');
  let prev = -1;
  for (let i = 0; i <= 100; i++) {
    const v = swapEase(i / 100);
    assert.ok(v >= prev - 1e-9, `never runs backwards (t=${i / 100})`);
    prev = v;
  }
  // The area-sweep shape: the radius eases IN slightly (see the note over
  // ::view-transition-new(root) in animations.css) — behind the diagonal early on…
  assert.ok(swapEase(0.2) > 0.1 && swapEase(0.2) < 0.2, `eases in slightly (got ${swapEase(0.2)})`);
  assert.ok(swapEase(0.5) < 0.5, 'still behind the diagonal at half time');
  // …and the control points are the CSS's, verbatim.
  const css = ANIMATIONS_CSS;
  assert.ok(/animation: themeSwapReveal var\(--swap-ms, 280ms\) cubic-bezier\(0\.4, 0\.25, 0\.95, 1\)/.test(css),
    'the JS curve and the declared reveal share one set of control points');
});

// ── The front ───────────────────────────────────────────────────────────────
// The clip is a polygon ring wearing the particle style (dustCloud.js edgeJitter).
// Coverage still rules: even the deepest dip must clear the furthest corner at the end.
test('swapEdgePolygon: a ring in the particle style that still covers the whole viewport', () => {
  const [x, y, w, h] = [90, 60, 1440, 900];
  const R = swapRadius(x, y, w, h);
  const parse = (poly) => [...poly.matchAll(/([\d.-]+)% ([\d.-]+)%/g)]
    .map((mch) => ({ x: (parseFloat(mch[1]) / 100) * w, y: (parseFloat(mch[2]) / 100) * h }));
  for (const style of [STYLE_DUST, STYLE_WATER, STYLE_FIRE]) {
    const to = parse(swapEdgePolygon(x, y, w, h, 1, style));
    assert.equal(to.length, SWAP_EDGE_POINTS);
    const radii = to.map((p) => Math.hypot(p.x - x, p.y - y));
    for (const r of radii) {
      assert.ok(r >= R - 0.05, `style ${style}: a vertex dips inside the corner reach (${r} < ${R})`);
      assert.ok(r <= R * 1.15, `style ${style}: a tongue overshoots wildly (${r})`);
    }
    const spread = Math.max(...radii) - Math.min(...radii);
    if (style === STYLE_DUST) assert.ok(spread < 0.1, 'dust: a perfect circle');
    else assert.ok(spread > R * 0.03, `style ${style}: visibly not a circle`);
    // The collapsed start: every vertex AT the origin, same count, so CSS interpolates.
    const from = parse(swapEdgePolygon(x, y, w, h, 0, style));
    assert.equal(from.length, SWAP_EDGE_POINTS);
    assert.ok(from.every((p) => Math.hypot(p.x - x, p.y - y) < 0.5));
    // Deterministic, and a stub viewport declines rather than throwing.
    assert.equal(swapEdgePolygon(x, y, w, h, 1, style), swapEdgePolygon(x, y, w, h, 1, style));
  }
  // Fire's tongues are sharp peaks with dips between; water's swells are smooth.
  const fire = parse(swapEdgePolygon(x, y, w, h, 1, STYLE_FIRE)).map((p) => Math.hypot(p.x - x, p.y - y));
  const above = fire.filter((r) => r > R * edgeBaseOf(STYLE_FIRE) * 1.04).length;
  assert.ok(above > 10 && above < fire.length * 0.6, `tongues reach well out on a minority of vertices (${above})`);
  assert.equal(swapEdgePolygon(0, 0, 0, 0, 1), '');
  // The default style is the live preference (particles → dust).
  assert.equal(swapEdgePolygon(x, y, w, h, 1), swapEdgePolygon(x, y, w, h, 1, STYLE_DUST));
});

test('swapDustSpecs seeds every mote just inside the ring, on screen, deterministically', () => {
  const [x, y, w, h] = [90, 60, 1440, 900];
  const specs = swapDustSpecs(x, y, w, h);
  assert.ok(specs.length > 30, `a real field of motes, not a sprinkle (got ${specs.length})`);
  assert.ok(specs.length <= SWAP_DUST_MOTES);
  assert.deepEqual(specs, swapDustSpecs(x, y, w, h), 'a hash, not Math.random');
  const R = swapRadius(x, y, w, h);
  for (const s of specs) {
    // Ignition rides the wipe's clock, clear of both ends.
    assert.ok(s.delay >= Math.floor(SWAP_DUST_MIN_T * THEME_SWAP_MS), `delay ${s.delay} too early`);
    assert.ok(s.delay <= Math.ceil(SWAP_DUST_MAX_T * THEME_SWAP_MS), `delay ${s.delay} too late`);
    // In the front's WAKE: when a mote lights up, even the deepest tooth of the torn
    // edge has already passed its spot — during a view transition anything outside the
    // clip simply is not rendered.
    const dist = Math.hypot(s.cx - x, s.cy - y);
    const wakeAtIgnite = swapEase((s.delay + 1) / THEME_SWAP_MS) * R;   // dust: the front is the circle itself
    assert.ok(dist <= wakeAtIgnite + 1, `mote at ${dist}px ahead of the wake band at ${wakeAtIgnite}px`);
    // On screen (a hair of margin), so no spec is spent where nobody can see it.
    assert.ok(s.cx >= -16 && s.cx <= w + 16, `off screen x ${s.cx}`);
    assert.ok(s.cy >= -16 && s.cy <= h + 16, `off screen y ${s.cy}`);
    assert.ok(s.size >= 2.5 && s.size <= 6, `grain out of range ${s.size}`);
    assert.ok(s.alpha >= 0.75 && s.alpha <= 1, `never faint ${s.alpha}`);
  }
  // Both grains are present: the old surface's own colour, and the departing accent.
  assert.ok(specs.some((s) => s.accent) && specs.some((s) => !s.accent));
  // A degenerate viewport seeds nothing rather than throwing (a stub environment).
  assert.deepEqual(swapDustSpecs(0, 0, 0, 0), []);
});

test('swapDustFrame flies a grain the way its keyframes did: flare, throw, shrink', () => {
  const s = { cx: 100, cy: 200, size: 4, dx: 12, dy: -8, alpha: 0.9, delay: 40, accent: false };
  const at = (p) => swapDustFrame(s, p);
  // Ignition and burn-out are both invisible; the flare peaks where the 18% stop was.
  assert.ok(at(0).alpha < 0.01 && at(1).alpha < 0.01, 'lights up out of nothing, leaves nothing');
  assert.ok(Math.abs(at(SWAP_DUST_FLARE).alpha - s.alpha) < 1e-6, 'full brightness at the flare stop');
  // Brightness climbs to the stop and falls away after it, monotonically either side.
  for (let p = 0.02; p < SWAP_DUST_FLARE; p += 0.02) assert.ok(at(p).alpha < at(p + 0.02).alpha);
  for (let p = SWAP_DUST_FLARE; p < 0.96; p += 0.02) assert.ok(at(p).alpha > at(p + 0.02).alpha);
  // The throw is the whole of --dx/--dy, and the grain shrinks to 0.3 of itself.
  assert.deepEqual([at(0).x, at(0).y], [s.cx, s.cy], 'starts home, untransformed');
  assert.ok(Math.abs(at(1).x - (s.cx + s.dx)) < 0.05 && Math.abs(at(1).y - (s.cy + s.dy)) < 0.05);
  assert.ok(Math.abs(at(0).r - s.size / 2) < 1e-6);
  assert.ok(Math.abs(at(1).r - (s.size / 2) * 0.3) < 0.02, 'scale(0.3) at the end');
  // The curve is ease-out: more than half the throw is spent in the first half.
  assert.ok(at(0.5).x - s.cx > s.dx * 0.5);
  // It writes into a caller's object — this runs once per grain per frame.
  const out = {};
  assert.strictEqual(swapDustFrame(s, 0.5, out), out);
});

test('themeSwap spawns the wake once the transition is ready, in the OLD palette', async () => {
  const root = rootStub();
  const bodyChildren = [];
  // A canvas that records what was actually painted: the colour of each fill, its alpha,
  // and how many arcs rode in it.
  const mkCanvas = () => {
    const fills = [];
    const el = {
      className: '', width: 0, height: 0, style: {}, removed: false,
      remove() { el.removed = true; },
    };
    let arcs = 0;
    const ctx = {
      fillStyle: '#000', globalAlpha: 1, cleared: 0,
      scale() {}, clearRect() { ctx.cleared++; }, beginPath() { arcs = 0; },
      moveTo() {}, arc() { arcs++; },
      fill() { fills.push({ colour: ctx.fillStyle, alpha: ctx.globalAlpha, arcs }); },
    };
    el.getContext = () => ctx;
    el.fills = fills;
    return el;
  };
  const doc = {
    documentElement: root,
    // The palette probe spans (dustCloud.js resolveColour) come and go on <body> too.
    createElement: (tag) => (tag === 'canvas' ? mkCanvas() : {
      style: { setProperty() {} }, appendChild() {},
      remove() { const i = bodyChildren.indexOf(this); if (i >= 0) bodyChildren.splice(i, 1); },
    }),
    body: { appendChild: (el2) => bodyChildren.push(el2) },
    startViewTransition: (cb) => { cb(); return { ready: Promise.resolve(), finished: Promise.resolve() }; },
  };
  const priorWin = globalThis.window;
  const priorCS = globalThis.getComputedStyle;
  const priorDoc = globalThis.document;
  const priorMM = globalThis.matchMedia;
  const priorRaf = globalThis.requestAnimationFrame;
  const priorCancel = globalThis.cancelAnimationFrame;
  globalThis.window = { innerWidth: 300, innerHeight: 400, devicePixelRatio: 2 };
  // The vars as they read BEFORE apply — the wake must bake these, not the new theme's.
  globalThis.getComputedStyle = () => ({
    getPropertyValue: (n) => ({ '--bg-page': '#111318', '--text-main': '#e8eaf0', '--accent': '#eab308' }[n] || ''),
  });
  // Installed for the WHOLE test, not via withDoc: the wake spawns from ready's
  // microtask, after a withDoc would already have restored the globals.
  globalThis.document = doc;
  globalThis.matchMedia = () => ({ matches: false });
  let pending = null;
  globalThis.requestAnimationFrame = (cb) => { pending = cb; return 1; };
  globalThis.cancelAnimationFrame = () => { pending = null; };
  try {
    themeSwap(() => {}, { x: 150, y: 200 });
    await Promise.resolve();   // let `ready` deliver
    await Promise.resolve();
    assert.equal(bodyChildren.length, 1, 'one dust layer on <body>');
    const stage = bodyChildren[0];
    assert.equal(stage.className, 'swap-dust');
    // Sized in device pixels, laid out in CSS ones — a hi-dpi wake is not a blurry one.
    assert.deepEqual([stage.width, stage.height], [600, 800]);
    assert.deepEqual([stage.style.width, stage.style.height], ['300px', '400px']);

    // Mid-wake: grains of the accent AND its shade are in the air, from the palette
    // resolved BEFORE the flip (the stub cannot compute a colour, so color-mix() strings
    // come through).
    const t0 = performance.now();
    pending(t0 + THEME_SWAP_MS / 2);
    const lit = stage.fills.filter((f) => f.arcs > 0);
    assert.ok(lit.reduce((n, f) => n + f.arcs, 0) > 30, 'a field of grains');
    assert.ok(lit.some((f) => f.colour === 'color-mix(in srgb, var(--accent) 100%, var(--accent-2))'), 'accent grains');
    assert.ok(lit.some((f) => f.colour === 'color-mix(in srgb, var(--accent) 0%, var(--accent-2))'), 'shade grains');
    // The whole point of the stage: batched fills — one per (stop, alpha step), each in
    // chunks of FILL_CHUNK grains (dustCloud.js) — not one per grain.
    const grains = lit.reduce((n, f) => n + f.arcs, 0);
    assert.ok(stage.fills.length <= 6 * DUST_ALPHA_LEVELS + Math.ceil(grains / FILL_CHUNK),
      `batched into ${stage.fills.length} fills, not ${grains}`);
    assert.ok(lit.every((f) => f.arcs <= FILL_CHUNK), 'no path longer than a chunk');
    assert.ok(lit.every((f) => f.alpha > 0 && f.alpha <= 1), 'every batch carries its own alpha');

    // …and it reaps itself once the last grain has burnt out.
    pending(t0 + THEME_SWAP_MS + SWAP_DUST_LIFE_MS + 1);
    assert.ok(stage.removed, 'the stage clears itself off the page');
  } finally {
    clearTimeout(root._swapDustTimer);   // reap the layer's own timer — tests must not linger
    globalThis.window = priorWin;
    globalThis.getComputedStyle = priorCS;
    globalThis.document = priorDoc;
    globalThis.matchMedia = priorMM;
    globalThis.requestAnimationFrame = priorRaf;
    globalThis.cancelAnimationFrame = priorCancel;
  }
});

test('themeSwap without View Transitions transitions the palette and still applies it', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const root = rootStub();
  let applied = 0;
  withDoc({ documentElement: root }, () => themeSwap(() => { applied++; }));
  assert.equal(applied, 1, 'the palette write happens exactly once');
  assert.ok(root.has(THEME_SWAP_CLASS), 'colour consumers get their one beat of transition');
  t.mock.timers.tick(THEME_SWAP_MS + 1);
  assert.ok(!root.has(THEME_SWAP_CLASS), 'and the class is cleaned up after');
});

test('themeSwap hands the circle to CSS as custom properties, not a scripted animation', async () => {
  // A view transition ends as soon as its pseudo-elements have no animations left, so
  // scripting one from ready.then() races the teardown and the wipe stops half way.
  // The keyframes live in CSS; this only supplies the origin and radius.
  const root = rootStub();
  let applied = 0;
  const doc = { documentElement: root,
    startViewTransition: (cb) => { cb(); return { ready: Promise.resolve(), finished: Promise.resolve() }; } };
  const priorWin = globalThis.window;
  globalThis.window = { innerWidth: 300, innerHeight: 400 };
  try {
    withDoc(doc, () => themeSwap(() => { applied++; }, { x: 0, y: 0 }));
  } finally { globalThis.window = priorWin; }
  assert.equal(applied, 1, 'the palette write happens exactly once');
  assert.equal(root.props['--swap-x'], '0%');
  assert.equal(root.props['--swap-y'], '0%');
  // 500px of a 300x400 viewport, as the percentage clip-path resolves against sqrt(w²+h²)/√2.
  assert.equal(root.props['--swap-r'], '141.421%', 'the radius reaches the furthest corner');
  assert.equal(root.props['--swap-ms'], `${THEME_SWAP_MS}ms`);
  // …and the clip the reveal actually plays: the ragged polygon pair, equal vertex
  // counts so the two interpolate.
  const count = (p) => (String(p).match(/%/g) || []).length / 2;
  assert.ok(String(root.props['--swap-clip-from']).startsWith('polygon('), 'a collapsed ragged start');
  assert.ok(String(root.props['--swap-clip-to']).startsWith('polygon('), 'a full ragged ring');
  assert.equal(count(root.props['--swap-clip-from']), SWAP_EDGE_POINTS);
  assert.equal(count(root.props['--swap-clip-to']), SWAP_EDGE_POINTS);
  assert.equal(rootStub.lastAnimate, undefined, 'nothing is animated from script');
  // Transitions are suppressed WHILE the snapshot is captured, or it records the old
  // colours mid-ease and the wipe reveals a half-changed page.
  assert.ok(root.sawInstantDuringApply, 'colour transitions are off while the new state is captured');
});

test('themeSwap still applies the palette when the document is a bare stub', () => {
  let applied = 0;
  withDoc({ documentElement: {} }, () => themeSwap(() => { applied++; }));
  assert.equal(applied, 1, 'decoration is optional; the write never is');
});

test('originOf resolves a control to its centre, and declines an unrendered one', () => {
  assert.deepEqual(originOf({ getBoundingClientRect: () => ({ left: 10, top: 20, width: 40, height: 10 }) }),
    { x: 30, y: 25 });
  assert.equal(originOf({ getBoundingClientRect: () => ({ left: 0, top: 0, width: 0, height: 0 }) }), null);
  assert.equal(originOf(null), null);
});

test('animations.css: the swap wipe is declarative, and the fallback transitions colours', () => {
  const css = ANIMATIONS_CSS;
  // Both default cross-fades are off — motion.js drives the clip itself.
  // Declarative, so the transition waits for it instead of tearing down mid-wipe.
  assert.ok(/::view-transition-old\(root\) \{ z-index: 0; animation: none; \}/.test(css),
    'the OLD snapshot keeps no default animation');
  assert.ok(/animation: themeSwapReveal var\(--swap-ms/.test(css), 'the reveal is a CSS animation');
  // The front is the ragged polygon pair motion.js supplies; the circle is only the
  // fallback for a write that never happened.
  assert.ok(/@keyframes themeSwapReveal \{[\s\S]*?clip-path: var\(--swap-clip-from, circle\(0%/.test(css),
    'collapsing from the ragged start, circle as fallback');
  assert.ok(/@keyframes themeSwapReveal \{[\s\S]*?clip-path: var\(--swap-clip-to, circle\(var\(--swap-r/.test(css),
    'growing to the ragged ring motion.js supplies');
  const fallback = css.slice(css.indexOf('html.theme-swapping'));
  assert.ok(/background-color 0\.45s/.test(fallback) && /color 0\.45s/.test(fallback)
    && /border-color 0\.45s/.test(fallback), 'every colour consumer eases, not just the body');
});

// ── The swap origin resolves to the element you can actually SEE ────────────
// The fullscreen layer clones the whole toolbar, duplicate ids and all, and never
// removes the clones on exit. getElementById returns document order, so a hidden copy
// can win the lookup — and the theme wipe then blooms from a stale pointer position
// instead of the button that was pressed.
test('originOfId skips a hidden duplicate and takes the on-screen one', () => {
  const mk = (w, h, left, top) => ({
    getBoundingClientRect: () => ({ width: w, height: h, left, top }),
  });
  const prior = globalThis.document;
  globalThis.document = {
    querySelectorAll: () => [mk(0, 0, 0, 0), mk(38, 38, 356, 304)],
  };
  try {
    assert.deepEqual(originOfId('theme-toggle'), { x: 375, y: 323 });
  } finally { globalThis.document = prior; }
});

// A rect is not the same thing as being ON SCREEN: `visibility: hidden` / `opacity: 0`
// leave one behind, and a parked clone keeps its size — bloom from either and the wipe
// comes out of a corner instead of the button.
test('originOf declines a control that is laid out but not visible', () => {
  const rect = () => ({ left: 10, top: 20, width: 40, height: 10 });
  assert.equal(originOf({ getBoundingClientRect: rect, checkVisibility: () => false }), null);
  assert.deepEqual(originOf({ getBoundingClientRect: rect, checkVisibility: () => true }), { x: 30, y: 25 });
});

// …and a control that is CLIPPED away — inside a collapsed panel, or a parked clone of the
// toolbar — is no more on screen than a hidden one. Its own style says `visible`; it is the
// ancestor's overflow that hides it, which is what put the wipe in the top-left corner.
test('originOf declines a control an ancestor clips away', () => {
  const priorCS = globalThis.getComputedStyle;
  const priorWin = globalThis.window;
  globalThis.window = { innerWidth: 1000, innerHeight: 800 };
  const panel = (h) => ({
    parentElement: null,
    getBoundingClientRect: () => ({ left: 0, top: 0, right: 400, bottom: h, width: 400, height: h }),
    __overflow: 'hidden',
  });
  const btn = (parent) => ({
    parentElement: parent,
    getBoundingClientRect: () => ({ left: 20, top: 120, right: 60, bottom: 152, width: 40, height: 32 }),
    checkVisibility: () => true,
  });
  globalThis.getComputedStyle = (n) => ({ overflow: n.__overflow || 'visible',
                                          overflowX: n.__overflow || 'visible',
                                          overflowY: n.__overflow || 'visible' });
  try {
    assert.equal(originOf(btn(panel(0))), null, 'collapsed to nothing — not on screen');
    assert.deepEqual(originOf(btn(panel(300))), { x: 40, y: 136 }, 'open — the button is real');
  } finally { globalThis.getComputedStyle = priorCS; globalThis.window = priorWin; }
});

test('originOf declines a control parked outside the viewport', () => {
  const prior = globalThis.window;
  globalThis.window = { innerWidth: 300, innerHeight: 400 };
  try {
    assert.equal(originOf({ getBoundingClientRect: () => ({ left: -80, top: 20, width: 40, height: 10 }) }), null);
    assert.equal(originOf({ getBoundingClientRect: () => ({ left: 400, top: 20, width: 40, height: 10 }) }), null);
    assert.deepEqual(originOf({ getBoundingClientRect: () => ({ left: 10, top: 20, width: 40, height: 10 }) }),
      { x: 30, y: 25 });
  } finally { globalThis.window = prior; }
});

// ── The origin is a control, or the centre — never the cursor ───────────────
// desktop/src/app/mainWindow.cpp learned this first: driving the change from a menu
// leaves the cursor near the screen corner, and the circle appears to come out of the
// window corner. A stale click in the page is exactly the same trap.
test('with no control to anchor to, themeSwap blooms from the viewport centre', () => {
  const root = rootStub();
  const doc = { documentElement: root,
    startViewTransition: (cb) => { cb(); return { ready: Promise.resolve(), finished: Promise.resolve() }; } };
  const priorWin = globalThis.window;
  globalThis.window = { innerWidth: 300, innerHeight: 400 };
  try {
    withDoc(doc, () => themeSwap(() => {}));
    assert.equal(root.props['--swap-x'], '50%');
    assert.equal(root.props['--swap-y'], '50%');
    // …and a control always outranks that (275/300, 12/400 of the viewport).
    withDoc(doc, () => themeSwap(() => {}, { x: 275, y: 12 }));
    assert.equal(root.props['--swap-x'], '91.667%');
    assert.equal(root.props['--swap-y'], '3%');
  } finally { globalThis.window = priorWin; }
});

// The wipe is one animation mirrored across the browser and the extension — the desktop
// runs the same length and curve from its Qt overlay, so these two are held to EACH
// OTHER. The curve matters as much as the duration: it must be a true ease-out (fast away
// from the button, still decelerating as it reaches the far corner) and must never ease
// IN, which is what made the circle appear to creep before it moved.
test('the wipe: browser and extension share one duration and one ease-out curve', () => {
  const decls = [['browser', ANIMATIONS_CSS],
                 ['extension', readFileSync(new URL('../../extension/src/lib/animations.css', import.meta.url), 'utf8')]]
    .map(([name, text]) => {
      const decl = /animation: themeSwapReveal var\(--swap-ms, (\d+)ms\) cubic-bezier\(([^)]*)\)/
        .exec(text);
      assert.ok(decl, `${name}: the reveal declaration`);
      return { name, ms: Number(decl[1]), curve: decl[2].split(',').map(Number) };
    });
  const [browser, extension] = decls;
  assert.deepEqual(extension, { ...extension, ms: browser.ms, curve: browser.curve },
    'the two stylesheets drifted apart');
  // The JS timer that clears the classes has to outlast the CSS, or the fallback
  // cross-fade is cut off mid-way.
  assert.equal(THEME_SWAP_MS, browser.ms, 'motion.js THEME_SWAP_MS is the CSS fallback');
  const ext = readFileSync(new URL('../../extension/src/lib/swapGeometry.js', import.meta.url), 'utf8');
  assert.equal(Number(/var SWAP_MS = (\d+)/.exec(ext)[1]), browser.ms, 'swapGeometry.js SWAP_MS agrees');

  // Judged on the AREA it sweeps, not on its control points. The wipe is a CIRCLE, so the
  // recoloured area grows as r²: an ease-OUT radius floods the screen early and then spends
  // its tail creeping over a sliver in the far corner, which is what "the animation stops
  // in the middle and finishes later" actually is. So the radius has to ease IN slightly.
  const [x1, y1, x2, y2] = browser.curve;
  const at = (t) => {                    // solve x(u) = t, then take y(u)
    let lo = 0, hi = 1, u = t;
    for (let i = 0; i < 40; i++) {
      u = (lo + hi) / 2;
      const x = 3 * (1 - u) ** 2 * u * x1 + 3 * (1 - u) * u * u * x2 + u ** 3;
      if (x < t) lo = u; else hi = u;
    }
    return 3 * (1 - u) ** 2 * u * y1 + 3 * (1 - u) * u * u * y2 + u ** 3;
  };
  assert.equal(y2, 1, `${browser.curve}: the sweep ends at the full radius`);
  // Fraction of a viewport covered by a circle of radius `r*R` about (cx, cy), sampled.
  const coveredAt = (t, cx, cy, w = 1440, h = 900) => {
    const R = Math.hypot(Math.max(cx, w - cx), Math.max(cy, h - cy));
    const r = at(t) * R;
    let inside = 0, n = 90;
    for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) {
      const x = (i + 0.5) * w / n, y = (j + 0.5) * h / n;
      if ((x - cx) ** 2 + (y - cy) ** 2 <= r * r) inside++;
    }
    return inside / (n * n);
  };
  // Every origin the app actually uses: a toolbar icon near a corner, and the centre
  // fallback for when no control is on screen.
  for (const [name, cx, cy] of [['toolbar icon', 93, 436], ['viewport centre', 720, 450]]) {
    // THE regression: nothing must be finished early, or the rest of the duration is dead
    // time with a stalled-looking crescent left in the corner.
    assert.ok(coveredAt(0.8, cx, cy) < 0.92, `${name}: still visibly moving at 80% of the time`);
    assert.ok(coveredAt(0.9, cx, cy) < 0.99, `${name}: and at 90%`);
    // …and the opposite trap: it must not creep at the start and then rush.
    assert.ok(coveredAt(0.4, cx, cy) > 0.15, `${name}: away from the button without creeping`);
    // Even growth: no decile may sweep more than a third of the screen on its own.
    let prev = 0;
    for (let t = 0.1; t <= 1.0001; t += 0.1) {
      const c = coveredAt(Math.min(t, 1), cx, cy);
      assert.ok(c >= prev, `${name}: the wipe never goes backwards`);
      assert.ok(c - prev < 0.34, `${name}: no decile floods a third of the screen at once`);
      prev = c;
    }
  }
});

// The toolbar fold and the coordinates-panel slide are a plain ease-out. They deliberately
// do NOT take the wipe's curve: a fold grows along one axis, so its area is simply its
// height and it wants the straightforward deceleration, while the wipe is a circle whose
// area grows as r² and needs the correction for that (see the note above its declaration).
test('the panel folds are an ease-out, and hold visibility for the whole fold', () => {
  const css = ANIMATIONS_CSS;
  const token = (name) => new RegExp(`--${name}:\\s*([^;]+);`).exec(css)?.[1].trim();
  const [, y1, , y2] = /cubic-bezier\(([^)]*)\)/.exec(token('fold-ease'))[1].split(',').map(Number);
  assert.ok(y1 > 0.5 && y2 === 1, `--fold-ease ${token('fold-ease')}: leaves at speed, settles at the end`);
  const foldMs = Number(/(\d+)ms/.exec(token('fold-ms'))[1]);
  assert.ok(foldMs >= 300, 'the fold is the slower, settling kind');
  // …and COLLAPSING is slower still: with no icon to shrink into, the fold itself is the
  // only thing that reads as the menu leaving, so a brisk exit registers as a snap.
  const foldOutMs = Number(/(\d+)ms/.exec(token('fold-out-ms'))[1]);
  assert.ok(foldOutMs > foldMs, `--fold-out-ms ${foldOutMs} outlasts the way back in`);
  // visibility is what takes the collapsed toolbar out of the tab order; released EARLY
  // it blinks out mid-fold, and the fold animates against nothing — so the delay has to be
  // the COLLAPSE's own duration, which is the one this rule is the after-change style for.
  const hidden = css.slice(css.indexOf('#controls-body.hidden {'));
  assert.match(hidden.slice(0, hidden.indexOf('}')), /visibility 0s linear var\(--fold-out-ms\)/,
    'the visibility delay must be the fold duration itself, not a copied constant');
  // Every collapsing part opts out under reduced motion.
  const reduced = css.slice(css.indexOf('@media (prefers-reduced-motion: reduce) {\n    #controls-body'));
  for (const sel of ['#controls-body', '.coordinates-panel', '#coord-body', '.coord-tabs'])
    assert.ok(reduced.slice(0, reduced.indexOf('}')).includes(sel), `${sel} opts out`);
});

test('motion.js keeps no pointer state for the swap to fall back to', () => {
  const src = motionSource();
  assert.ok(!/addEventListener\(\s*'pointerdown'/.test(src),
    'a remembered press is what put the wipe in the corner — the control is the only origin');
});

test('originOfId is null when every copy is hidden, and never throws on a stub', () => {
  const prior = globalThis.document;
  globalThis.document = { querySelectorAll: () => [{ getBoundingClientRect: () => ({ width: 0, height: 0, left: 0, top: 0 }) }] };
  try { assert.equal(originOfId('theme-toggle'), null); } finally { globalThis.document = prior; }
  globalThis.document = {};
  try { assert.equal(originOfId('theme-toggle'), null); } finally { globalThis.document = prior; }
});

test('animations.css: only the wipe drives the transition — no UA group/old default', () => {
  const css = ANIMATIONS_CSS;
  assert.match(css, /::view-transition-group\(root\) \{ animation: none; \}/,
    'the UA group animation would retime the pair under the wipe');
  assert.match(css, /::view-transition-old\(root\) \{ z-index: 0; animation: none; \}/);
});

// ── Incognito frame draws on, and retracts in reverse ───────────────────────
// The dashes have to appear in order (clockwise from the top-left) and leave in the
// opposite order. An `outline` can only do both at once, so the frame is four edges
// whose LENGTH animates — transform would stretch the dash pattern into stripes.
test('the incognito frame is four edges that grow, not a stretched outline', () => {
  const css = COMPONENTS_CSS;
  assert.ok(!/body\.incognito-mode \.canvas-viewport \{[^}]*outline:/.test(css),
    'the all-at-once outline is gone');
  const edge = css.slice(css.indexOf('.ig-edge {'), css.indexOf('\n}', css.indexOf('.ig-edge {')));
  assert.match(edge, /transition: width [^;]*, *\n? *height /, 'length is what animates');
  assert.ok(!/transform:/.test(edge), 'transform would smear the dashes into stripes');
  // Clockwise on the way in…
  for (const [sel, ms] of [['t', 0], ['r', 110], ['b', 220], ['l', 330]])
    assert.match(css, new RegExp(`body\\.incognito-mode \\.ig-${sel} \\{ --ig-delay: ${ms}ms; \\}`),
      `edge ${sel} draws at ${ms}ms`);
  // …and the rest state mirrors them, so the LAST edge drawn is the FIRST to retract.
  assert.match(css, /\.ig-t \{ --ig-delay: 330ms; \}/);
  assert.match(css, /\.ig-l \{ --ig-delay: 0ms; \}/);
  // The floating "Incognito — not saved" pill over the picture is gone: that fact now
  // rides the toolbar "?" bubble, and the frame alone marks the mode on the canvas.
  assert.ok(!css.includes('.ig-badge'), 'the on-canvas incognito pill is back');
});

test('the incognito frame markup ships all four edges, inside the canvas viewport', async () => {
  const src = readFileSync(new URL('../js/ui/mainContent.js', import.meta.url), 'utf8');
  for (const e of ['ig-t', 'ig-r', 'ig-b', 'ig-l'])
    assert.ok(src.includes(e), `mainContent renders .${e}`);
  assert.ok(!src.includes('ig-badge'), 'the on-canvas incognito pill is back');
  // Always in the DOM (the exit animation needs something to retract) and inside the
  // VIEWPORT, so it traces the whole visible canvas region rather than the picture.
  assert.ok(src.indexOf('canvas-viewport" id=') < src.indexOf('incognito-frame'),
    'the frame escaped the canvas viewport');
  assert.ok(src.indexOf('incognito-frame') < src.indexOf('canvas-container" id='),
    'the frame must precede the canvas container, not sit inside it');
});

// ── Appearance: system is the default, and stays followed ──────────────────
// The browser used to store only a RESOLVED light/dark, so one press of the toolbar
// toggle pinned the palette for good and the app never followed the OS again. It now
// keeps the same three-state mode the desktop (io/fileStore.hpp themeMode) and the
// extension (lib/shellTheme.js THEME_MODES) have, with 'system' as the default.
// Switching between two modes that resolve to the SAME palette repaints nothing — playing
// the wipe for it animates an unchanged screen.
test('theme mode: picking a mode that resolves to the painted palette does not animate', () => {
  const src = readFileSync(new URL('../js/ui/accentController.js', import.meta.url), 'utf8');
  const body = src.slice(src.indexOf('setThemeMode('), src.indexOf('get themeMode()'));
  assert.match(body, /resolveThemeMode\(next\) === painted/, 'the resolved palette is compared');
  // …and the setting is still stored + announced on that path, or the picker would snap back.
  const noop = body.slice(body.indexOf('=== painted'), body.indexOf('themeSwap('));
  assert.match(noop, /localStorage\.setItem\(THEME_STORAGE_KEY, next\)/, 'the mode is still stored');
  assert.match(noop, /EVENTS\.themeChanged/, 'and still announced');
  const ext = readFileSync(new URL('../../extension/src/lib/shellPrefs.js', import.meta.url), 'utf8');
  assert.match(ext, /var repaints = resolveTheme\(next\) !== resolveTheme\(readTheme\(\)\)/,
    'the extension makes the same check');
});

test('theme mode: three states, system by default, resolved against the OS', async () => {
  const { THEME_MODES, resolveThemeMode } = await import('../js/ui/accentController.js');
  assert.deepEqual(THEME_MODES, ['system', 'light', 'dark']);
  // An explicit mode is taken as-is, whatever the OS says.
  assert.equal(resolveThemeMode('dark', false), 'dark');
  assert.equal(resolveThemeMode('light', true), 'light');
  // 'system' — and anything unrecognised, including a missing setting — asks the OS.
  for (const mode of ['system', undefined, null, '', 'nonsense']) {
    assert.equal(resolveThemeMode(mode, true), 'dark', `${mode} follows a dark OS`);
    assert.equal(resolveThemeMode(mode, false), 'light', `${mode} follows a light OS`);
  }
});

test('theme mode: the pre-paint script and the app agree on what "system" means', () => {
  const pre = readFileSync(new URL('../js/prePaintTheme.js', import.meta.url), 'utf8');
  // Before first paint, only an explicit light/dark wins; everything else resolves against
  // the media query — otherwise a stored 'system' would paint the literal string.
  assert.match(pre, /savedTheme === 'dark' \|\| savedTheme === 'light'/,
    'the pre-paint script treats a stored mode, not a stored palette');
  assert.match(pre, /prefersDark \? 'dark' : 'light'/, 'and falls through to the OS');
  const binder = readFileSync(new URL('../js/ui/bindings/theme.js', import.meta.url), 'utf8');
  // The OS listener has to test the MODE: keyed on "nothing stored", it stopped following
  // the moment the toggle wrote a value.
  assert.match(binder, /themeMode !== 'system'/, 'the OS is followed while the mode is system');
  const vis = (f) => readFileSync(new URL(`../js/ui/${f}`, import.meta.url), 'utf8');
  assert.match(vis('visualsMarkup.js'), /id="vs-appearance"/, 'and there is a control to get back to system');
  assert.match(vis('visualsModal.js'), /setThemeMode\(appearance\.value/, 'which writes the mode');
});

// ── Filtering a list (createFilterAnimator) ─────────────────────────────────
// A filter is a QUESTION being re-answered, not a removal: what no longer matches is
// simply gone the moment the list rebuilds, and the whole effect belongs to the rows
// that are LEFT, which arrive as the new answer. render() runs exactly once per call,
// FIRST — the set you can see never depends on the decoration.

test('filterDelta: what a change drops, reveals, and whether it merely moved', async () => {
  const { filterDelta } = await import('../js/ui/motion.js');
  const d = filterDelta(['a', 'b', 'c'], ['b', 'd']);
  assert.deepEqual(d.leaving, ['a', 'c'], 'keys the new filter excludes');
  assert.deepEqual(d.entering, ['d'], 'keys it reveals');
  assert.ok(d.moved);
  // A sort switch: same membership, new order — nothing enters or leaves.
  const sorted = filterDelta(['a', 'b'], ['b', 'a']);
  assert.deepEqual(sorted.leaving, []);
  assert.deepEqual(sorted.entering, []);
  assert.ok(sorted.moved, 'but the list did move, so it is still worth playing');
  // An unchanged list must be recognisable as such (a keystroke that narrows nothing).
  const same = filterDelta(['a', 'b'], ['a', 'b']);
  assert.ok(!same.moved);
  assert.deepEqual(filterDelta(new Set(['a']), new Set(['a', 'b'])).entering, ['b'],
    'any iterable of keys, not just arrays');
});

// A tiny list rig: `state` is the key set the next render will show.
const filterRig = ({ shown = [], reduced = false } = {}) => {
  const timers = [];
  const rows = new Map();
  const rig = {
    renders: 0,
    keys: shown.slice(),
    nextKeys: shown.slice(),
    classesOf: (k) => rows.get(k)?.classes ?? new Set(),
    run: null,
    flush: () => { for (const t of timers.splice(0).sort((a, b) => a.ms - b.ms)) t.fn(); },
  };
  const makeRow = (k) => {
    const classes = new Set();
    return {
      classes,
      style: { setProperty(p, v) { this[p] = v; } },
      classList: { add: (...c) => c.forEach((x) => classes.add(x)), remove: (...c) => c.forEach((x) => classes.delete(x)) },
      getBoundingClientRect: () => ({ height: 40 }),
    };
  };
  for (const k of shown) rows.set(k, makeRow(k));
  rig.run = createFilterAnimator({
    keys: () => rig.keys,
    next: () => rig.nextKeys,
    // The rebuild: the shown set becomes the pending one, and every key gets a row.
    render: () => {
      rig.renders++;
      rig.keys = rig.nextKeys.slice();
      for (const k of rig.keys) if (!rows.has(k)) rows.set(k, makeRow(k));
    },
    find: (k) => rows.get(k) ?? null,
    setTimer: (fn, ms) => { timers.push({ fn, ms }); return timers.length; },
    reduced: () => reduced,
  });
  return rig;
};

test('a filter change re-renders FIRST, then plays the rows that are LEFT in', async () => {
  const rig = filterRig({ shown: ['a', 'b', 'c'] });
  rig.nextKeys = ['b', 'd'];
  await rig.run();
  assert.strictEqual(rig.renders, 1, 'exactly one rebuild, and nothing waited on it');
  assert.deepEqual(rig.keys, ['b', 'd'], 'the new answer is on screen at once');
  // Every row that is LEFT arrives — the filtered set is what changed, not just the
  // rows that happen to be new to it.
  assert.ok(rig.classesOf('d').has(FILTER_ENTERING_CLASS), 'the revealed row arrives');
  assert.ok(rig.classesOf('b').has(FILTER_ENTERING_CLASS), '…and so does the one that survived');
  rig.flush();
  assert.ok(!rig.classesOf('d').has(FILTER_ENTERING_CLASS), 'and the class is cleaned up after');
  assert.ok(!rig.classesOf('b').has(FILTER_ENTERING_CLASS));
});

test('what the filter DROPS never plays at all — it is simply not the answer any more', async () => {
  const rig = filterRig({ shown: ['a', 'b'] });
  rig.nextKeys = ['b'];
  await rig.run();
  const classes = rig.classesOf('a');
  assert.strictEqual(classes.size, 0, 'the excluded row is untouched: no exit to watch');
  assert.ok(!classes.has('leaving'), 'the destructive collapse belongs to a real removal');
  assert.ok(!classes.has('materializing'), 'and the dust gather to a real add');
  // What tells a filter apart from a removal is its SHAPE, not its speed: no
  // destructive scatter, no collapsing slot, nothing played on the way out at all —
  // so the arrival is free to take its own time, slow enough to actually read.
  assert.ok(FILTER_ENTER_MS >= 300, 'slow enough to read as a deliberate settle, not a flicker');
});

test('a filter that only REVEALS rows plays the whole set in too', async () => {
  const rig = filterRig({ shown: ['a'] });
  rig.nextKeys = ['a', 'b'];
  await rig.run();
  assert.strictEqual(rig.renders, 1);
  assert.ok(rig.classesOf('b').has(FILTER_ENTERING_CLASS));
  assert.ok(rig.classesOf('a').has(FILTER_ENTERING_CLASS));
});

test('a sort switch (same rows, new order) settles the whole list back in', async () => {
  const rig = filterRig({ shown: ['a', 'b'] });
  rig.nextKeys = ['b', 'a'];
  await rig.run();
  assert.strictEqual(rig.renders, 1);
  assert.ok(rig.classesOf('a').has(FILTER_ENTERING_CLASS), 're-sorted rows read as arriving');
  assert.ok(rig.classesOf('b').has(FILTER_ENTERING_CLASS));
});

test('a change that moves nothing just renders — no flash', async () => {
  const rig = filterRig({ shown: ['a', 'b'] });
  rig.nextKeys = ['a', 'b'];
  await rig.run();
  assert.strictEqual(rig.renders, 1, 'the rebuild still happens (the row CONTENT may differ)');
  assert.ok(!rig.classesOf('a').has(FILTER_ENTERING_CLASS));
  assert.ok(!rig.classesOf('b').has(FILTER_ENTERING_CLASS));
});

test('reduced motion: straight to the re-render, no classes at all', async () => {
  const rig = filterRig({ shown: ['a', 'b'], reduced: true });
  rig.nextKeys = ['b'];
  await rig.run();
  assert.strictEqual(rig.renders, 1);
  assert.deepEqual(rig.keys, ['b'], 'and the right set is on screen');
  assert.strictEqual(rig.classesOf('a').size, 0);
  assert.strictEqual(rig.classesOf('b').size, 0);
});

test('fast typing: every keystroke renders NOW, and never drops rows', async () => {
  const rig = filterRig({ shown: ['a', 'b', 'c'] });
  rig.nextKeys = ['a', 'b'];        // keystroke 1
  await rig.run();
  assert.strictEqual(rig.renders, 1, 'on screen immediately — there is no exit to wait on');
  rig.nextKeys = ['a'];             // keystroke 2, hard on its heels
  await rig.run();
  assert.strictEqual(rig.renders, 2, 'and so is the next one');
  assert.deepEqual(rig.keys, ['a'], 'the final rendered set is the last one asked for');
  rig.flush();                      // the enter clean-ups fire late, over nothing
  assert.deepEqual(rig.keys, ['a']);
});

test('animations.css: a filter has an arrival and no exit at all', () => {
  const css = ANIMATIONS_CSS;
  assert.ok(!/\.filter-leaving/.test(css), 'nothing plays a filtered-out row out any more');
  assert.ok(!/rowFilterOut/.test(css), '…and its keyframes are gone with it');
  assert.match(css, /\.filter-entering \{\s*animation: rowFilterIn 0\.34s/);
  assert.match(css, /@keyframes rowFilterIn \{\s*from \{ opacity: 0; transform: translateY\(-4px\); \}/,
    'the rows that are left arrive — that is the whole effect');
  assert.match(css, /@media \(prefers-reduced-motion: reduce\) \{\s*\.filter-entering \{ animation: none; \}/);
});

// ── No mote flies a straight line ───────────────────────────────────────────
// Every flight bends through a waypoint pushed off its own line (tileWaypoint), so a
// cloud churns instead of radiating in spokes — the same recipe on every surface.

test('tileWaypoint sits part-way along the throw, pushed sideways by its own noise', () => {
  const { mx, my } = tileWaypoint(100, 0, 0.9);
  assert.equal(mx, Math.round(100 * WAYPOINT_ALONG), 'along the throw');
  assert.ok(my > 0 && my <= SWIRL_MAX_PX, 'off the line, on the noise’s side');
  const other = tileWaypoint(100, 0, 0.1);
  assert.ok(other.my < 0, 'the other half of the noise bends the other way');
  assert.equal(other.mx, mx, 'the push is perpendicular — it never changes the reach');
  // A short throw bends by a SHARE of itself; a long one is capped, so a window's 400px
  // trip cannot swing its motes across half the page.
  const short = tileWaypoint(0, 40, 1);
  assert.equal(Math.abs(short.mx), Math.round(40 * SWIRL_SHARE));
  const long = tileWaypoint(0, 400, 1);
  assert.equal(Math.abs(long.mx), SWIRL_MAX_PX);
  // Dead centre of the noise is a straight line; a zero throw has nowhere to bend.
  assert.deepEqual(tileWaypoint(60, 30, 0.5), { mx: Math.round(60 * WAYPOINT_ALONG), my: Math.round(30 * WAYPOINT_ALONG) });
  assert.deepEqual(tileWaypoint(0, 0, 0.9), { mx: 0, my: 0 });
});

// A row wiped in the browser read as visibly longer than the same wipe on the desktop,
// on the same 0.9s clock: the sweep's per-mote delay was ADDED to a full-span flight, so
// the last grains were still going a quarter-second after the span was over. The desktop
// overlay flies each cell the window it has left (`t = (t - delay) / (1 - delay)`), and
// so does this now — the cloud is done AT the span, whatever the sweep.
test('the scatter fits inside its span: a late mote flies what is left of it, not more', () => {
  const src = motionSource();
  assert.match(src, /dur: gather \? gatherMs : Math\.max\(MIN_TILE_MS, span - m\.delay\)/,
    'the grain is given the remainder of the span, not the whole of it');
  // Every cell of a row scatter lands within DISINTEGRATE_MS (the floor is the only
  // exception, and it only ever applies to a flight far shorter than a row's).
  for (let cy = 0; cy < 16; cy++) {
    for (let cx = 0; cx < 34; cx += 7) {
      const { delay } = tileMotion(cx, cy, 34, 16);
      assert.ok(delay >= 0 && delay + Math.max(MIN_TILE_MS, DISINTEGRATE_MS - delay) <= DISINTEGRATE_MS,
        `cell ${cx},${cy} overruns the span`);
    }
  }
  // A gather is untouched: its flight is the short --gather-ms and the reversed sweep is
  // what fills the rest of the span, so it already landed on time.
  assert.ok(tileMotion(0, 0, 34, 16, true).delay >= 0);
  // …and the layer is torn down a beat after the last mote, not most of a second later.
  assert.match(src, /\}, span \+ 150\);/);
});

test('a row’s fall and a surface’s flight both carry the waypoint, deterministically', () => {
  const row = tileMotion(5, 3, 34, 16);
  assert.ok(Number.isInteger(row.mx) && Number.isInteger(row.my), 'pixel-rounded, like dx/dy');
  assert.deepEqual([row.mx, row.my], [tileMotion(5, 3, 34, 16).mx, tileMotion(5, 3, 34, 16).my], 'a hash, not Math.random');
  // The gather shares the waypoint with the scatter — the same bend, flown home.
  const back = tileMotion(5, 3, 34, 16, true);
  assert.deepEqual([back.mx, back.my], [row.mx, row.my]);
  // …and the bend scales with the throw, so a 15px tick bends as little as it flies.
  const mark = tileMotion(5, 3, 34, 16, false, 0.3);
  assert.ok(Math.hypot(mark.mx, mark.my) < Math.hypot(row.mx, row.my));
  const box = { left: 100, top: 100, width: 300, height: 200 };
  const s = surfaceMotion(2, 2, 10, 8, box, { x: 40, y: 20 });
  assert.ok(Number.isInteger(s.mx) && Number.isInteger(s.my));
  // Neighbouring cells bend to different sides: a third, decorrelated noise drives it.
  const sides = new Set();
  for (let cx = 0; cx < 10; cx++) {
    const m = surfaceMotion(cx, 2, 10, 8, box, { x: 40, y: 20 });
    // Sign of the perpendicular component, relative to the throw.
    sides.add(Math.sign(m.mx * m.dy - m.my * m.dx));
  }
  assert.ok(sides.has(1) && sides.has(-1), 'both sides of the line are used');
});

test('every flight bends through the waypoint on its own first leg, and the cloud is one canvas', () => {
  const css = ANIMATIONS_CSS;
  const grain = { x: 100, y: 200, dx: 60, dy: 80, mx: 30, my: 55, r: 4, s: 0.4, a: 1 };
  for (const name of ['scatter', 'gather', 'surfaceGather', 'surfaceScatter', 'fall']) {
    const f = FLIGHTS[name];
    // The mid keyframe: at `split` every grain is exactly at its waypoint…
    const bend = moteFrame(grain, name, f.split);
    assert.ok(Math.abs(bend.x - 130) < 1e-6 && Math.abs(bend.y - 255) < 1e-6, `${name} passes the waypoint`);
    // …at half its shrink, so nothing snaps at the bend.
    assert.ok(Math.abs(bend.r - 4 * (1 - (1 - 0.4) * 0.5)) < 1e-6, `${name}: half the shrink at the bend`);
    // The first leg carries its own curve, so the bend is a bend, not a stop-and-go
    // (a mark's fall rides one curve throughout, like the desktop's Sweep::Fall).
    if (name !== 'fall') assert.notEqual(f.leg(0.5), f.rest(0.5), `${name}: leg one eases on its own`);
  }
  // No node per grain any more: the layer holds ONE canvas (js/ui/dustCloud.js) and the
  // flights above are its table — nothing is left in the stylesheet per tile.
  assert.match(css, /\.disintegrate-host > canvas \{ position: absolute; display: block; \}/);
  assert.ok(!/disintegrate-tile/.test(css) && !/@keyframes tile/.test(css), 'no rule left per tile');
  // The theme wipe’s grains are the same round grain, but the STAGE draws them now
  // (js/ui/motion.js spawnSwapDust): no per-grain rule, and so no layer per grain.
  assert.ok(!/\.swap-dust-mote/.test(css) && !/swapDustMote/.test(css), 'no rule left per grain');
  const wake = css.match(/\.swap-dust \{([\s\S]*?)\n\}/)[1];
  assert.ok(!/will-change/.test(wake), 'one layer for the whole wake, not one promoted per grain');
});

test('canvas dust batches its grains: a few alpha steps, one fill per colour and step', () => {
  // Eight steps on a 3px grain are below what the eye resolves; fewer would band a
  // slow fade, more would multiply the fills the batching exists to avoid.
  assert.equal(DUST_ALPHA_LEVELS, 8);
  const motion = motionSource();
  const dust = motion.slice(motion.indexOf('const drawDust ='), motion.indexOf('const runDust ='));
  assert.match(dust, /fillGrains\(ctx, lvl\[l\], n, poly\)/, 'grains in the style\'s own shape, batched and chunked');
  assert.ok(!/drawImage\(snap, p\./.test(dust), 'never a per-grain blit of the picture');
  // The picture itself stands in for every cell still at home: one blit, then only the
  // departed cells are cleared out of it — so the front grinds, it does not pop.
  assert.match(dust, /ctx\.drawImage\(snap, sox, soy, sw \* cols, sh \* rows, 0, 0, dw \* cols, dh \* rows\)/);
  assert.match(dust, /ctx\.clearRect\(p\.x0, p\.y0, p\.x1 - p\.x0, p\.y1 - p\.y0\)/);
  // The outer edge of the grid rounds UP: the picture is blitted at its fractional size,
  // and a last column rounded down left an uncleared hairline of it down the right edge.
  const parts = motion.slice(motion.indexOf('const dustParts ='), motion.indexOf('const drawDust ='));
  assert.match(parts, /x1: cx === cols - 1 \? Math\.ceil\(cols \* dw\)/);
  assert.match(parts, /y1: cy === rows - 1 \? Math\.ceil\(rows \* dh\)/);
  assert.ok(dust.indexOf('clearRect') < dust.indexOf('const flush'), 'every clear lands before any grain is drawn');
});
