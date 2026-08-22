// Scroll reveal + drop landing (src/lib/motion.js) and the CSS that drives them —
// the extension half of the shared contract (browser/tests/motion.test.js is the twin).
// Node has no IntersectionObserver, so the observer is pinned against stub observers
// over a hand-rolled DOM; the CSS contract is read straight out of the stylesheet.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

import {
  observeReveal, flashLanding, revealDissolve, revealGrain,
  createListHold, emptyStateVisible, tileMotion, materialize,
  MATERIALIZE_CLASS, MATERIALIZE_VEIL_CLASS, LEAVE_MS, DISINTEGRATE_MS,
} from '../src/lib/motion.js';

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

// ── The reveal ramp (browser parity) ───────────────────────────────────────
// Dissolve tracks ONLY the share of a row the scroller already clips. The earlier
// design dissolved anything inside a "band" of the visible area, which turned a
// fully-readable message into an unreadable dot screen — the grain is finer than a
// glyph's strokes. Decoration must never cost legibility.
const H = 800;

test('a row you can see in FULL is never dissolved at all', () => {
  assert.equal(revealDissolve(0, 60, H), 0);
  assert.equal(revealDissolve(300, 400, H), 0);
  assert.equal(revealDissolve(H - 50, H, H), 0);
});

test('dissolve equals the clipped share, at either edge', () => {
  assert.equal(revealDissolve(-50, 50, H), 0.5);
  assert.equal(revealDissolve(H - 50, H + 50, H), 0.5);
});

test('a row fully out of view is fully dissolved, both ways', () => {
  assert.equal(revealDissolve(-200, -50, H), 1);
  assert.equal(revealDissolve(H + 20, H + 120, H), 1);
});

test('degenerate inputs never dissolve anything', () => {
  assert.equal(revealDissolve(0, 50, 0), 0);
  assert.equal(revealDissolve(50, 50, H), 0);
});

// The grain covers the whole row, so a message TALLER than the scroller — clipped by
// definition, however you scroll it — must not be speckled while you are reading it.
test('a row taller than the viewport gets no grain while it fills the view', () => {
  assert.equal(revealGrain(-400, 1600, H), 0);   // 2000px row, viewport full of it
  assert.equal(revealGrain(0, 2000, H), 0);
  assert.ok(revealDissolve(0, 2000, H) > 0.5);   // still clipped: the soft edge stays
});

test('grain matches the clipped share for rows that fit', () => {
  assert.equal(revealGrain(0, 60, H), 0);
  assert.equal(revealGrain(-50, 50, H), 0.5);
  assert.equal(revealGrain(H - 50, H + 50, H), 0.5);
});

test('a tall row grains as it leaves, and is fully grained once gone', () => {
  assert.equal(revealGrain(-1800, 200, H), 0.75);   // only 200 of 800 still showing
  assert.equal(revealGrain(-2200, -200, H), 1);
  assert.equal(revealGrain(0, 2000, 0), 0);
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

test('animations.css: reveal rest state, drop landing, drag cue, reduced motion', () => {
  const css = readFileSync(new URL('../src/lib/animations.css', import.meta.url), 'utf8');
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

test('animations.css: only the wipe drives the theme transition', () => {
  const css = readFileSync(new URL('../src/lib/animations.css', import.meta.url), 'utf8');
  assert.match(css, /::view-transition-group\(root\) \{ animation: none; \}/,
    'the UA group default would retime the snapshots under the wipe');
});

test('popup.css keeps just-pinned distinct from the drop landing', () => {
  const css = readFileSync(new URL('../src/popup/popup.css', import.meta.url), 'utf8');
  assert.ok(/\.row\.just-pinned \{ animation: stencil-pin-flash/.test(css),
    'pinning an EXISTING row keeps the plainer flash');
  // The transcript's per-entry entrance moved to the shared reveal; a leftover
  // `animation: … both` here would out-rank it.
  assert.ok(!/#chat-transcript > \* \{ animation:/.test(css), 'transcript entries ride the reveal');
});

// ── The wipe hold + refresh gate (browser connectModal.test.js twin) ────────
// The options page's server-connections list defers its storage.onChanged rebuild
// and its "No servers connected yet." empty state while a row's leave (or a new
// row's materialize) is still playing — createListHold is that gate.

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
  assert.ok(backTop.delay <= DISINTEGRATE_MS * 0.4 + 60, 'same sweep window as the scatter');
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

test('animations.css: materialize is the leave reversed, veil outranks keyframes', () => {
  const css = readFileSync(new URL('../src/lib/animations.css', import.meta.url), 'utf8');
  assert.match(css, /\.materializing \{[^}]*animation: stRowMaterialize 0\.22s/,
    'the box expands on the collapse’s own 220ms timer');
  assert.match(css, /@keyframes stRowMaterialize \{\s*from \{ opacity: 0;[^}]*max-height: 0/,
    'the expansion starts from the collapsed end-state of stRowLeave');
  assert.match(css, /\.materialize-veil \{ opacity: 0 !important; \}/,
    'the veil must outrank stRowMaterialize’s animated opacity');
  assert.match(css, /\.reintegrate-tile \{\s*animation: stTileGather/,
    'gather tiles override the scatter animation on the shared tile class');
  assert.match(css, /@keyframes stTileGather \{\s*0%\s+\{ opacity: 0;\s*transform: translate\(var\(--dx/,
    'a gather tile starts where the scatter would have flung it');
  assert.match(css, /@keyframes stTileGather \{[\s\S]*?100% \{ opacity: 1; transform: none; \}/,
    'and flies home to identity');
  assert.ok(css.indexOf('.reintegrate-tile') > css.indexOf('.disintegrate-tile'),
    'declared after .disintegrate-tile so the gather animation wins');
});
