// The gather (js/ui/motion.js materialize = the removal reversed): the same flight path on an
// inverted sweep, the collapse's own timer, and the animations.css veil that outranks it.
import { test } from 'node:test';
import assert from 'node:assert';
import {
  tileMotion, materialize, MATERIALIZE_CLASS, MATERIALIZE_VEIL_CLASS, LEAVE_MS, DISINTEGRATE_MS,
  TILE_JITTER_SHARE,
} from '../js/ui/motion.js';
import { FLIGHTS, moteFrame, alphaAt } from '../js/ui/dust/dustCloud.js';
import { ANIMATIONS_CSS } from './helpers/css.js';
import { rows } from './helpers/connectModalRig.js';

// ── The gather (materialize = the removal reversed) ─────────────────────────

test('tileMotion reverse: same flight path, inverted sweep', () => {
  const cols = 34;
  const rows = 16;
  const outTop = tileMotion(3, 0, cols, rows);
  const backTop = tileMotion(3, 0, cols, rows, true);
  // The path home is the scatter path played backwards — identical displacement.
  assert.strictEqual(outTop.dx, backTop.dx);
  assert.strictEqual(outTop.dy, backTop.dy);
  assert.strictEqual(outTop.rot, backTop.rot);
  assert.strictEqual(outTop.scale, backTop.scale);
  // Scatter: the top row leaves first. Gather: the first mote out is the LAST one home.
  const outBottom = tileMotion(3, rows - 1, cols, rows);
  const backBottom = tileMotion(3, rows - 1, cols, rows, true);
  assert.ok(outTop.delay < outBottom.delay, 'scatter sweeps top→bottom');
  assert.ok(backTop.delay > backBottom.delay, 'gather sweeps bottom→top');
  // The reversed sweep spans the same window: the gather's 0.4 share plus a mote's jitter,
  // itself a SHARE of the span (motion.js TILE_JITTER_SHARE), never a flat 60ms.
  assert.ok(backTop.delay <= DISINTEGRATE_MS * (0.4 + TILE_JITTER_SHARE));
});

test('materialize: expands on the collapse’s own timer; no veil without dust', async () => {
  // No DOM here, so the gather builds no tiles (disintegrate bails) — the row must
  // then expand un-veiled on LEAVE_MS, never hide behind a veil nothing will lift.
  const classes = new Set();
  const el = {
    classList: {
      add: (...c) => c.forEach((x) => classes.add(x)),
      remove: (...c) => c.forEach((x) => classes.delete(x)),
      contains: (c) => classes.has(c),
    },
  };
  const started = Date.now();
  const p = materialize(el);
  assert.ok(classes.has(MATERIALIZE_CLASS), 'the box-expand class goes on immediately');
  assert.ok(!classes.has(MATERIALIZE_VEIL_CLASS), 'no dust → no veil (nothing would lift it)');
  await p;
  assert.ok(!classes.has(MATERIALIZE_CLASS), 'cleaned up once the expansion is over');
  assert.ok(Date.now() - started >= LEAVE_MS - 20, 'the expansion runs the collapse’s duration');
});

test('materialize: a missing element resolves without touching anything', async () => {
  await materialize(null);   // must not throw — the add never depends on the animation
});

// The classes motion.js toggles must exist in animations.css with the reversed shapes: the
// expand mirrors rowLeave's collapse, the gather mirrors tileScatter.

test('animations.css: materialize is the leave reversed, veil outranks keyframes', () => {
  const css = ANIMATIONS_CSS;
  assert.match(css, /\.materializing \{[^}]*animation: rowMaterialize 0\.22s/,
    'the box expands on the collapse’s own 220ms timer');
  assert.match(css, /@keyframes rowMaterialize \{\s*from \{ opacity: 0;[^}]*max-height: 0/,
    'the expansion starts from the collapsed end-state of rowLeave');
  assert.match(css, /\.materialize-veil \{ opacity: 0 !important; \}/,
    'the veil must outrank rowMaterialize’s animated opacity (author !important beats keyframes)');
  // The transition lives on the lift, not the veil, and the box keyframes leave opacity to
  // it — a transition never starts on a property an animation is holding.
  assert.match(css, /\.materialize-veil\.materialize-lift \{ opacity: 1 !important; transition: opacity var\(--veil-fade, 0ms\) linear; \}/);
  assert.match(css, /\.materializing\.materialize-veil \{ animation-name: rowMaterializeBox; \}/);
  assert.ok(!/@keyframes rowMaterializeBox \{[^}]*opacity/.test(css), 'the box keyframes carry no alpha');
  assert.match(css, /\.disintegrate-host\.dust-forming \{ animation: dustHostOut var\(--host-ms, var\(--gather-ms, 420ms\)\)/,
    'the forming host lives the whole span');
  // The gather is the scatter reversed on the one canvas (js/ui/dustCloud.js): a grain starts
  // where the scatter would have flung it and flies home to identity — tileGather, as numbers.
  assert.equal(FLIGHTS.gather.from, 'far', 'a gather grain starts where the scatter would have flung it');
  const grain = { x: 10, y: 20, dx: 30, dy: 40, mx: 18, my: 25, r: 3, s: 0.5, a: 1 };
  const flung = moteFrame(grain, 'gather', 0);
  assert.deepEqual([flung.x, flung.y], [40, 60], 'the far end of the throw');
  const home = moteFrame(grain, 'gather', 1);
  assert.deepEqual([home.x, home.y, home.r], [10, 20, 3], 'and flies home to identity');
  assert.equal(alphaAt(FLIGHTS.gather.alpha, 0), 0, 'invisible as it sets off');
  assert.equal(alphaAt(FLIGHTS.gather.alpha, 1), 1, 'full once home');
});
