// Scroll reveal, the drop landing and the gather (src/lib/motion.js) plus the CSS that drives
// them — the extension half of the shared contract (browser/tests/motion.test.js is the twin).
import test from 'node:test';
import assert from 'node:assert';
import { animationsCss, popupCss } from './helpers/sources.js';

// The stylesheet the CSS half of this contract lives in, read once.
const css = animationsCss();

import {
  observeReveal, flashLanding,
  createListHold, emptyStateVisible, tileMotion, materialize,
  MATERIALIZE_CLASS, MATERIALIZE_VEIL_CLASS, LEAVE_MS, DISINTEGRATE_MS, TILE_JITTER_SHARE,
} from '../src/lib/motion.js';
import { FLIGHTS, moteFrame, alphaAt } from '../src/lib/dust/cloud.js';
import { classEl as el } from './helpers/listDom.js';

// revealDissolve/revealGrain are shared with the app to the letter (portParity.test.js pins them),
// so their cases are the browser suite's.

test('observeReveal is inert without requestAnimationFrame', () => {
  const prior = globalThis.requestAnimationFrame;
  delete globalThis.requestAnimationFrame;
  try {
    const stop = observeReveal({ addEventListener() {} }, '.row');
    assert.equal(typeof stop, 'function');
    stop();
  } finally { globalThis.requestAnimationFrame = prior; }
});

test('flashLanding replays on a second drop and clears itself', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const row = el();
  flashLanding(row);
  assert.ok(row.has('just-dropped'), 'defaults to the drop-landing class');
  flashLanding(row, 'just-dropped', 900);
  t.mock.timers.tick(899);
  assert.ok(row.has('just-dropped'), 'the first timer was cancelled, not left to fire early');
  t.mock.timers.tick(2);
  assert.ok(!row.has('just-dropped'));
});

test('animations/: reveal rest state, drop landing, drag cue, reduced motion', () => {
  const rest = css.slice(css.indexOf('.reveal-item.reveal-masked {'),
                         css.indexOf('\n}', css.indexOf('.reveal-item.reveal-masked {')));
  const base = css.slice(css.indexOf('.reveal-item {'), css.indexOf('\n}', css.indexOf('.reveal-item {')));
  assert.ok(!/mask-image/.test(base), 'a settled row composites no mask at all');
  // A mask dissolve, not per-particle clones: scrolling is continuous, so the reveal
  // must not churn DOM the way a one-shot removal can (browser parity).
  assert.ok(/--dissolve: 1/.test(base), 'out-of-view rows rest fully dissolved');
  assert.ok(/var\(--vis-start/.test(rest) && /var\(--vis-end/.test(rest),
    'the wipe is anchored to the still-visible span, so a readable row is untouched');
  assert.ok(/@property --dissolve \{ syntax: "<number>"/.test(css), '--dissolve is registered, so it can transition');
  assert.ok(/transition: --dissolve/.test(base) && /transform: none/.test(base));
  assert.ok(/mask-composite: add/.test(rest), 'grain UNIONed with the wipe, so a settled row is solid');
  assert.ok(/\.reveal-item\.reveal-in \{ --dissolve: 0; transform: none; \}/.test(css));
  // Rows must NOT also carry `animation: stRowIn … both`: the filled end state would
  // out-rank the reveal's transform and strand every row at its rest state.
  assert.ok(!/\.list \.row \{ animation: stRowIn/.test(css), 'list rows ride the reveal, not stRowIn');
  // A row a DROP created lands in and pulses the accent ring.
  assert.ok(/\.row\.just-dropped \{[\s\S]*?animation: stDropLand[\s\S]*?stDropRing/.test(css));
  assert.ok(/\.list\.drag-over \{ animation: stDragCue/.test(css), 'the drop cue breathes while a drag hovers');
  assert.ok(/\.reveal-item \{ --dissolve: 0 !important;[\s\S]{0,160}?mask-image: none !important; \}/
    .test(css.slice(css.indexOf('@media (prefers-reduced-motion: reduce)'))),
    'reduced motion shows every row whole, mask and all');
});

test('animations/: only the wipe drives the theme transition', () => {
  assert.match(css, /::view-transition-group\(root\) \{ animation: none; \}/,
    'the UA group default would retime the snapshots under the wipe');
});

test('popup.css keeps just-pinned distinct from the drop landing', () => {
  const css = popupCss();   // shadows the animations/ read above
  assert.ok(/\.row\.just-pinned \{ animation: stencil-pin-flash/.test(css),
    'pinning an EXISTING row keeps the plainer flash');
  // The transcript's per-entry entrance moved to the shared reveal; a leftover
  // `animation: … both` here would out-rank it.
  assert.ok(!/#chat-transcript > \* \{ animation:/.test(css), 'transcript entries ride the reveal');
});

// The options page's connections list defers its storage.onChanged rebuild and its empty state
// while a leave or materialize is still playing — createListHold is that gate.

const stubTimers = () => {
  const queue = [];
  return {
    queue,
    setTimer: (fn, ms) => { queue.push({ fn, ms }); return queue.length; },
    run: () => { for (const t of queue.splice(0)) t.fn(); },
  };
};

test('createListHold: holds until the wipe is over, then settles exactly once', async () => {
  const t = stubTimers();
  let settles = 0;
  const hold = createListHold({ settle: () => settles++, wait: () => 900, setTimer: t.setTimer });
  assert.equal(hold.holding, false);
  const settle = hold.begin();
  assert.equal(hold.holding, true, 'a wipe in flight gates the onChanged re-render');
  assert.equal(settles, 0, 'no settle render before the dust has landed');
  const p = settle();
  assert.equal(t.queue[0].ms, 900, 'waits the FULL wipe, not the short collapse');
  t.run();
  await p;
  assert.equal(settles, 1);
  assert.equal(hold.holding, false);
});

test('createListHold: finalizeAll settles pending holds NOW; late timers no-op', async () => {
  const t = stubTimers();
  let settles = 0;
  const hold = createListHold({ settle: () => settles++, wait: () => 900, setTimer: t.setTimer });
  const a = hold.begin();
  hold.begin();
  const pa = a();
  hold.finalizeAll();
  assert.equal(settles, 2, 'a view closing mid-animation finalizes immediately');
  assert.equal(hold.holding, false);
  t.run();
  await pa;
  assert.equal(settles, 2, 'a finalized hold’s timer settles nothing twice');
});

test('emptyStateVisible: never during a wipe, only once truly settled', () => {
  assert.equal(emptyStateVisible(0, false), true, 'empty + idle → placeholder');
  assert.equal(emptyStateVisible(0, true), false,
    'empty but mid-wipe → the placeholder waits for the settle render');
  assert.equal(emptyStateVisible(2, false), false);
  assert.equal(emptyStateVisible(2, true), false);
  assert.equal(emptyStateVisible(0), true, 'holding defaults to false');
});

// ── The gather (materialize = the removal reversed) ─────────────────────────

test('tileMotion reverse: same flight path, inverted sweep', () => {
  const cols = 22;
  const rows = 11;
  const outTop = tileMotion(3, 0, cols, rows);
  const backTop = tileMotion(3, 0, cols, rows, true);
  assert.equal(outTop.dx, backTop.dx);
  assert.equal(outTop.dy, backTop.dy);
  assert.equal(outTop.rot, backTop.rot);
  assert.equal(outTop.scale, backTop.scale);
  const outBottom = tileMotion(3, rows - 1, cols, rows);
  const backBottom = tileMotion(3, rows - 1, cols, rows, true);
  assert.ok(outTop.delay < outBottom.delay, 'scatter sweeps top→bottom');
  assert.ok(backTop.delay > backBottom.delay, 'gather sweeps bottom→top');
  // The gather's 0.4 share plus a mote's own jitter, which is a SHARE of the span too (motion.js
  // TILE_JITTER_SHARE) — a flat literal goes wrong the moment the span changes.
  assert.ok(backTop.delay <= DISINTEGRATE_MS * (0.4 + TILE_JITTER_SHARE),
    'same sweep window as the scatter');
});

test('materialize: expands on the collapse’s own timer; no veil without dust', async () => {
  // No DOM here, so the gather builds no tiles (disintegrate bails) — the row must
  // then expand un-veiled on LEAVE_MS, never hide behind a veil nothing will lift.
  const row = el();
  const started = Date.now();
  const p = materialize(row);
  assert.ok(row.has(MATERIALIZE_CLASS), 'the box-expand class goes on immediately');
  assert.ok(!row.has(MATERIALIZE_VEIL_CLASS), 'no dust → no veil (nothing would lift it)');
  await p;
  assert.ok(!row.has(MATERIALIZE_CLASS), 'cleaned up once the expansion is over');
  assert.ok(Date.now() - started >= LEAVE_MS - 20, 'the expansion runs the collapse’s duration');
});

test('materialize: a missing element resolves without touching anything', async () => {
  await materialize(null);   // must not throw — the add never depends on the animation
});

test('animations/: materialize is the leave reversed, veil outranks keyframes', () => {
  assert.match(css, /\.materializing \{[^}]*animation: stRowMaterialize 0\.32s cubic-bezier\(0\.16, 1, 0\.3, 1\)/,
    'the box opens on one expo-out curve — 220ms of ease-out read as a pop');
  assert.match(css, /@keyframes stRowMaterialize \{\s*from \{ opacity: 0;[^}]*max-height: 0/,
    'the expansion starts from the collapsed end-state of stRowLeave');
  assert.match(css, /\.materialize-veil \{ opacity: 0 !important; \}/,
    'the veil must outrank stRowMaterialize’s animated opacity');
  // The gather is the scatter reversed on the one canvas (lib/cloud.js): a grain starts where
  // the scatter would have flung it and flies home to identity — stTileGather, as numbers.
  assert.equal(FLIGHTS.gather.from, 'far', 'a gather grain starts where the scatter would have flung it');
  const grain = { x: 10, y: 20, dx: 30, dy: 40, mx: 18, my: 25, r: 3, s: 0.5, a: 1 };
  assert.deepEqual([moteFrame(grain, 'gather', 0).x, moteFrame(grain, 'gather', 0).y], [40, 60]);
  const home = moteFrame(grain, 'gather', 1);
  assert.deepEqual([home.x, home.y, home.r], [10, 20, 3], 'and flies home to identity');
  assert.equal(alphaAt(FLIGHTS.gather.alpha, 0), 0);
  assert.equal(alphaAt(FLIGHTS.gather.alpha, 1), 1);
});
