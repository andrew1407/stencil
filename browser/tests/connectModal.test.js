import { test } from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

// layout() transitively imports every ui component, including the connect modal.
import { layout } from '../js/ui/layout.js';
import {
  createListHold, emptyStateVisible, tileMotion, materialize,
  MATERIALIZE_CLASS, MATERIALIZE_VEIL_CLASS, LEAVE_MS, DISINTEGRATE_MS,
} from '../js/ui/motion.js';
import { canRefreshList } from '../js/ui/projectsModal.js';

const markup = layout();
const count = (needle) => markup.split(needle).length - 1;

// ── Static markup ───────────────────────────────────────────────────────────
// The connections list is runtime-built; nothing that only render() may decide —
// rows or the "No servers connected." placeholder — may exist statically, or it
// would flash before the first render (and beneath a removal's falling dust).

test('each connect-modal id appears exactly once', () => {
  for (const id of ['connect-modal-overlay', 'connect-close', 'connect-url', 'connect-token',
    'connect-add', 'connect-reconnect', 'connect-list', 'connect-batch-bar']) {
    assert.strictEqual(count(`id="${id}"`), 1, `id="${id}" should appear exactly once`);
  }
});

test('#connect-list is empty/comment-only in static markup', () => {
  assert.ok(/<div id="connect-list"><!-- filled by JS --><\/div>/.test(markup),
    'empty comment-only #connect-list present');
  assert.strictEqual(count('connect-row'), 0, 'no connection rows statically');
  assert.strictEqual(count('No servers connected'), 0,
    'the empty state is render()’s call — it must never pre-exist the first render');
});

// ── The refresh gate ────────────────────────────────────────────────────────
// The connections list gates its out-of-band re-render (stencil:connections-changed)
// on the SAME helper the projects modal uses: never mid-drag, never while a wipe
// holds — that render is exactly the rebuild that cuts the leave short and pops the
// empty state in beneath the still-falling dust.

test('connections refresh gate: canRefreshList holds off renders mid-wipe', () => {
  assert.strictEqual(canRefreshList({ open: true, dragging: false, removing: false }), true);
  assert.strictEqual(canRefreshList({ open: true, removing: true }), false,
    'a removal in flight defers the re-render to the settle');
  assert.strictEqual(canRefreshList({ open: true, dragging: true }), false);
  assert.strictEqual(canRefreshList({ open: false }), false, 'a closed modal never re-renders');
});

// ── The wipe hold (createListHold) ──────────────────────────────────────────
// One hold per playing leave/materialize; the settle waits out the FULL wipe
// (wipeDurationMs, not the 220ms collapse) and runs the deferred render exactly once.

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
  assert.strictEqual(hold.holding, false);
  const settle = hold.begin();
  assert.strictEqual(hold.holding, true, 'a wipe in flight gates refreshes');
  assert.strictEqual(settles, 0, 'no settle render before the dust has landed');
  const p = settle();
  assert.strictEqual(t.queue[0].ms, 900, 'waits the FULL wipe, not the short collapse');
  assert.strictEqual(settles, 0, 'starting the settle timer is not settling');
  t.run();
  await p;
  assert.strictEqual(settles, 1);
  assert.strictEqual(hold.holding, false);
});

test('createListHold: overlapping holds keep gating until the LAST one settles', async () => {
  const t = stubTimers();
  let settles = 0;
  const hold = createListHold({ settle: () => settles++, wait: () => 900, setTimer: t.setTimer });
  const a = hold.begin();
  const b = hold.begin();
  const pa = a();
  t.run();
  await pa;
  assert.strictEqual(hold.holding, true, 'the second wipe still holds');
  const pb = b();
  t.run();
  await pb;
  assert.strictEqual(hold.holding, false);
  assert.strictEqual(settles, 2, 'each hold settles its own deferred render');
});

test('createListHold: finalizeAll settles pending holds NOW; late timers no-op', async () => {
  const t = stubTimers();
  let settles = 0;
  const hold = createListHold({ settle: () => settles++, wait: () => 900, setTimer: t.setTimer });
  const a = hold.begin();
  hold.begin();
  const pa = a();               // one settle already scheduled…
  hold.finalizeAll();           // …when the modal closes mid-animation
  assert.strictEqual(settles, 2, 'a close finalizes every pending removal immediately');
  assert.strictEqual(hold.holding, false, 'nothing half-removed can reappear on reopen');
  t.run();                      // the late wipe timer fires after the close
  await pa;
  assert.strictEqual(settles, 2, 'a finalized hold’s timer settles nothing twice');
});

// ── No early empty state ────────────────────────────────────────────────────
// While a hold is pending the "No servers connected." placeholder may not appear,
// even with zero connections left — it waits for the settle render.

test('emptyStateVisible: never during a wipe, only once truly settled', () => {
  assert.strictEqual(emptyStateVisible(0, false), true, 'empty + idle → placeholder');
  assert.strictEqual(emptyStateVisible(0, true), false,
    'empty but mid-wipe → the placeholder must wait for the settle render');
  assert.strictEqual(emptyStateVisible(3, false), false);
  assert.strictEqual(emptyStateVisible(3, true), false);
  assert.strictEqual(emptyStateVisible(0), true, 'holding defaults to false');
});

test('the removal timeline never shows the empty state early', async () => {
  // The confirmDisconnect sequence, replayed over the hold: begin → leave (collapse)
  // → disconnect → settle. At every step before the settle the placeholder is gated.
  const t = stubTimers();
  let placeholderShown = false;
  let connections = 1;
  const hold = createListHold({
    settle: () => { placeholderShown = emptyStateVisible(connections, hold.holding); },
    wait: () => 900,
    setTimer: t.setTimer,
  });
  const settle = hold.begin();                       // removal begins, height pinned
  assert.strictEqual(emptyStateVisible(connections, hold.holding), false);
  connections = 0;                                   // disconnect landed mid-wipe
  assert.strictEqual(emptyStateVisible(connections, hold.holding), false,
    'zero rows but dust still falling → no placeholder yet');
  const p = settle();
  t.run();
  await p;
  assert.strictEqual(placeholderShown, true, 'the settle render finally shows it');
});

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
  // The reversed sweep spans the same window as the forward one (same duration budget).
  assert.ok(backTop.delay <= DISINTEGRATE_MS * 0.4 + 60);
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

// ── The CSS contract ────────────────────────────────────────────────────────
// The classes motion.js toggles must exist in animations.css with the reversed
// shapes: the expand mirrors rowLeave's collapse, the gather mirrors tileScatter.

test('animations.css: materialize is the leave reversed, veil outranks keyframes', () => {
  const css = readFileSync(new URL('../css/animations.css', import.meta.url), 'utf8');
  assert.match(css, /\.materializing \{[^}]*animation: rowMaterialize 0\.22s/,
    'the box expands on the collapse’s own 220ms timer');
  assert.match(css, /@keyframes rowMaterialize \{\s*from \{ opacity: 0;[^}]*max-height: 0/,
    'the expansion starts from the collapsed end-state of rowLeave');
  assert.match(css, /\.materialize-veil \{ opacity: 0 !important; \}/,
    'the veil must outrank rowMaterialize’s animated opacity (author !important beats keyframes)');
  assert.match(css, /\.reintegrate-tile \{\s*animation: tileGather/,
    'gather tiles override the scatter animation on the shared tile class');
  assert.match(css, /@keyframes tileGather \{\s*0%\s+\{ opacity: 0;\s*transform: translate\(var\(--dx/,
    'a gather tile starts where the scatter would have flung it');
  assert.match(css, /@keyframes tileGather \{[\s\S]*?100% \{ opacity: 1; transform: none; \}/,
    'and flies home to identity');
  // The gather rides the same fixed layer, so reduced motion hides it with the scatter.
  assert.ok(css.indexOf('.reintegrate-tile') > css.indexOf('.disintegrate-tile'),
    'declared after .disintegrate-tile so the gather animation wins');
});
