// The connect modal's static markup and render gate (js/ui/connectModal.js): every id once,
// a runtime-built list, and createListHold deferring a re-render until the wipe settles.
import { test } from 'node:test';
import assert from 'node:assert';
import { createListHold, emptyStateVisible, materialize } from '../js/ui/motion.js';
import { canRefreshList } from '../js/core/projectOpenGesture.js';
import { rows, markup, count } from './helpers/connectModalRig.js';

// The connections list is runtime-built: nothing render() decides — rows, the
// "No servers connected." placeholder — may exist statically, or it flashes pre-render.

test('each connect-modal id appears exactly once', () => {
  for (const id of ['connect-modal-overlay', 'connect-close', 'connect-url', 'connect-token',
    'connect-add', 'connect-reconnect', 'connect-list', 'connect-batch-bar', 'connect-filter']) {
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

// The out-of-band re-render (stencil:connections-changed) is gated on the same helper the
// projects modal uses: never mid-drag, never while a wipe holds.

test('connections refresh gate: canRefreshList holds off renders mid-wipe', () => {
  assert.strictEqual(canRefreshList({ open: true, dragging: false, removing: false }), true);
  assert.strictEqual(canRefreshList({ open: true, removing: true }), false,
    'a removal in flight defers the re-render to the settle');
  assert.strictEqual(canRefreshList({ open: true, dragging: true }), false);
  assert.strictEqual(canRefreshList({ open: false }), false, 'a closed modal never re-renders');
});

// createListHold: one hold per playing leave/materialize; the settle waits out the FULL
// wipe (wipeDurationMs, not the 220ms collapse) and runs the deferred render once.

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

// While a hold is pending the "No servers connected." placeholder may not appear, even with
// zero connections left — it waits for the settle render.

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
