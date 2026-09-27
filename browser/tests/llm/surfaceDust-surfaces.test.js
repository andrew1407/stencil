// The surface-dust CSS contract: the flights, the hand-over, the layer, and the clocks. Each
// surface's origin point is in surfaceDust-origins.test.js. Split from surfaceDust.test.js.
import test from 'node:test';
import assert from 'node:assert';
import { createStubElement } from '../helpers/dom.js';
import { installDustDom, rect, boxEl } from '../helpers/dustCloudRig.js';
import { FLIGHTS, alphaAt } from '../../js/ui/dust/cloud.js';
import { ANIMATIONS_CSS } from '../helpers/css.js';

const { doc } = installDustDom();
const {
  tileNoise, SURFACE_IN_MS, SURFACE_OUT_MS, TILE_GATHER_SHARE, disintegrate, cancelDust,
} = await import('../../js/ui/motion.js');

const animCss = ANIMATIONS_CSS;

// ── 5. The CSS contract ─────────────────────────────────────────────────────

test('the surface flights are the row’s scatter, re-timed and re-aimed', () => {
  // Same grain, same waypoint arithmetic as a row's (cloud.js FLIGHTS): a gather
  // starts at the far end and flies home, a scatter the other way.
  assert.equal(FLIGHTS.surfaceGather.from, 'far');
  assert.equal(FLIGHTS.surfaceScatter.from, 'home');
  // A surface's motes are visible from the first frame — they ARE the window.
  assert.equal(alphaAt(FLIGHTS.surfaceGather.alpha, 0), 0.55);
  assert.equal(alphaAt(FLIGHTS.surfaceScatter.alpha, 0), 1);
  // …and their ease-out covers most of the trip early, so the bend sits early too.
  assert.ok(FLIGHTS.surfaceGather.split <= 0.2 && FLIGHTS.surfaceScatter.split <= 0.2);
  assert.ok(FLIGHTS.surfaceGather.rest(0.3) > 0.8, 'ease-out: most of the trip in the first third');
  // Both ride the surface's own clock: --dust-ms / --gather-ms on the host, each grain's too.
  const hostOf = (opts) => { const el = boxEl(rect(0, 0, 200, 100)); disintegrate(el, { ms: 600, ...opts }); return el.__dustHost; };
  const toward = hostOf({ gather: true, toward: { x: 0, y: 0 } });
  assert.equal(toward.style['--dust-ms'], '600ms');
  assert.equal(toward.style['--gather-ms'], '600ms', 'a surface gather spends the whole span arriving');
  assert.ok(toward.__cloud.motes.every((m) => m.dur === 600));
  assert.equal(hostOf({ gather: false }).style['--gather-ms'], '600ms');
  const row = hostOf({ gather: true });
  assert.equal(row.style['--gather-ms'], `${Math.round(600 * TILE_GATHER_SHARE)}ms`, 'a row gather keeps the default share');
  assert.ok(row.__cloud.motes.every((m) => m.dur === Math.round(600 * TILE_GATHER_SHARE)));
});

test('the surface waits behind its dust, and hands over to it on the way out', () => {
  assert.match(animCss, /@keyframes surfaceForm\s+\{ 0%, \d+% \{ opacity: 0; \} 100% \{ opacity: 1; \} \}/);
  assert.match(animCss, /@keyframes surfaceLeave \{ 0% \{ opacity: 1; \} \d+%, 100% \{ opacity: 0; \} \}/);
  assert.match(animCss, /\.dust-driven\.surface-forming \{ animation: surfaceForm var\(--dust-ms, \d+ms\) linear both !important; \}/);
  assert.match(animCss, /\.dust-driven\.surface-leaving \{ animation: surfaceLeave var\(--dust-ms, \d+ms\) linear both !important; \}/);
  // The cloud cross-fades with the surface at each hand-over, so a gather cannot end on
  // a flat slab of the panel's colour and a scatter cannot start on a hard cut.
  assert.match(animCss, /\.disintegrate-host\.dust-leaving \{ animation: dustHostIn /);
  assert.match(animCss, /\.disintegrate-host\.dust-forming \{ animation: dustHostOut /);
});

test('the layer can never take a click or hold focus, and never moves the page', () => {
  const host = animCss.match(/\.disintegrate-host \{([\s\S]*?)\n\}/)[1];
  assert.match(host, /position: fixed;/, 'out of flow — no reflow, ever');
  assert.match(host, /pointer-events: none;/);
  // The motes are pixels on ONE canvas inside the layer (cloud.js) — no node per
  // grain, nothing with text or a tabindex, so there is nothing focusable at all.
  const ctx = new Proxy({}, { get: () => () => {} });
  const make = doc.createElement;
  doc.createElement = (tag) => (tag === 'canvas' ? createStubElement('canvas', { getContext: () => ctx }) : make(tag));
  const el = boxEl(rect(10, 10, 200, 100));
  try { assert.equal(disintegrate(el, {}), true); } finally { doc.createElement = make; }
  const [canvas, ...rest] = el.__dustHost.children;
  assert.equal(canvas?.tagName, 'CANVAS');
  assert.equal(rest.length, 0, 'one canvas, and no node per grain');
  assert.match(canvas.style.cssText, /^position:absolute;.*pointer-events:none;$/);
  // The layer never waits on the paint: a document without a 2D canvas (tests) still
  // gets the bookkeeping, and the loop is stopped before the layer goes.
  const bare = boxEl(rect(10, 10, 200, 100));
  assert.equal(disintegrate(bare, {}), true);
  assert.equal(bare.__dustHost.children.length, 0);
  assert.ok(bare.__dustHost.__cloud.motes.length > 0);
  const layer = bare.__dustHost;
  let stoppedWhileUp = null;
  layer.__stop = () => { stoppedWhileUp = !!layer.parentNode; };
  cancelDust(bare);
  assert.equal(stoppedWhileUp, true, 'the loop stops first');
  assert.equal(layer.parentNode, null, '…then the layer goes');
});

test('reduced motion: no cloud, no veil — the surface simply is, or is not', () => {
  assert.match(animCss, /@media \(prefers-reduced-motion: reduce\) \{\s*\n\s*\.disintegrate-host \{ display: none; \}/);
  assert.match(animCss, /\.dust-driven, \.dust-driven\.surface-forming, \.dust-driven\.surface-leaving \{ animation: none !important; \}/);
});

test('a FOLD is the exception: it leaves slower than it arrives', async () => {
  const { FOLD_DUST_OUT_MS } = await import('../../js/ui/motion.js');
  assert.ok(FOLD_DUST_OUT_MS > SURFACE_OUT_MS,
    'a fold has no icon to shrink into — the fold IS the close, so it may not be brisk');
  const token = (name) => Number(/(\d+)ms/.exec(new RegExp(`--${name}:\\s*([^;]+);`).exec(animCss)[1])[1]);
  // The sand and the CSS fold must scale together, or one outlives the other.
  assert.equal((FOLD_DUST_OUT_MS / SURFACE_OUT_MS).toFixed(2),
               (token('fold-out-ms') / token('fold-ms')).toFixed(2));
});

test('a surface forms slower than it leaves — arriving is the half you watch', () => {
  assert.ok(SURFACE_IN_MS > SURFACE_OUT_MS, 'the gather is the slower half');
  assert.ok(SURFACE_IN_MS >= 560 && SURFACE_IN_MS <= 800, 'slow enough to read as sand gathering, brisk enough not to wait on');
  // tileNoise is the shared hash — the surface flight is the row's, not a second system.
  assert.equal(typeof tileNoise(1, 2), 'number');
  assert.equal(tileNoise(1, 2), tileNoise(1, 2));
});
