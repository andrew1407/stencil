import { test } from 'node:test';
import assert from 'node:assert';

// Opening a project row: the gesture → intent mapping and the deferred-single-click machine
// behind it (js/core/openGesture.js) — pure/injected, so the whole mouse+touch matrix
// runs without a DOM.
import {
  rowOpenIntent, createOpenGesture, DOUBLE_CLICK_MS, DRAG_SLOP_PX,
} from '../js/core/project/openGesture.js';

// A controllable clock: timers fire only when the test advances it.
const stubTimers = () => {
  const jobs = new Map();
  let seq = 0;
  return {
    setTimer: (fn, ms) => { jobs.set(++seq, { fn, ms }); return seq; },
    clearTimer: (id) => jobs.delete(id),
    get pending() { return jobs.size; },
    // Run every timer whose delay is <= ms (the only delays here are the two constants).
    advance(ms) {
      for (const [id, job] of [...jobs]) {
        if (job.ms <= ms) { jobs.delete(id); job.fn(); }
      }
    },
  };
};
const recorder = () => {
  const intents = [];
  return { intents, run: (i) => intents.push(`${i.confirm ? 'confirm' : 'now'}:${i.target}`) };
};

// ── The mapping ──
test('rowOpenIntent: the full mouse + touch matrix', () => {
  // Mouse: a single click confirms; a double click acts immediately; ⌘/Ctrl targets a new tab.
  assert.deepStrictEqual(rowOpenIntent({ type: 'click' }), { confirm: true, target: 'here' });
  assert.deepStrictEqual(rowOpenIntent({ type: 'dblclick' }), { confirm: false, target: 'here' });
  assert.deepStrictEqual(rowOpenIntent({ type: 'click', metaKey: true }), { confirm: true, target: 'newtab' });
  assert.deepStrictEqual(rowOpenIntent({ type: 'click', ctrlKey: true }), { confirm: true, target: 'newtab' });
  assert.deepStrictEqual(rowOpenIntent({ type: 'dblclick', metaKey: true }), { confirm: false, target: 'newtab' });
  assert.deepStrictEqual(rowOpenIntent({ type: 'dblclick', ctrlKey: true }), { confirm: false, target: 'newtab' });
  // Keyboard activation behaves like the single click it replaces.
  assert.deepStrictEqual(rowOpenIntent({ type: 'key' }), { confirm: true, target: 'here' });
  assert.deepStrictEqual(rowOpenIntent({ type: 'key', ctrlKey: true }), { confirm: true, target: 'newtab' });
  // Touch: a tap opens, and nothing maps to a long press — the list's press-and-hold picks the
  // row up for reordering (drag.js), so "open in a new tab" is the ⋯ menu's item there.
  assert.deepStrictEqual(rowOpenIntent({ type: 'tap' }), { confirm: true, target: 'here' });
  assert.deepStrictEqual(rowOpenIntent({ type: 'longpress' }), { confirm: true, target: 'here' },
    'no long-press mapping: it degrades to the safe default, it never targets a new tab');
  // Unknown/absent type degrades to the safest thing: confirm, this tab.
  assert.deepStrictEqual(rowOpenIntent(), { confirm: true, target: 'here' });
  assert.deepStrictEqual(rowOpenIntent({}), { confirm: true, target: 'here' });
});

// ── The crux: a double click must never flash the confirmation modal ──
test('a single click is deferred and CANCELLED by the double click that follows', () => {
  const timers = stubTimers();
  const rec = recorder();
  const g = createOpenGesture({ run: rec.run, touch: () => false, ...timers });

  g.click({});                     // first click of the pair — nothing runs yet
  assert.deepStrictEqual(rec.intents, [], 'no action before the double-click window closes');
  assert.ok(g.pendingClick);
  g.click({});                     // second click re-arms, still nothing
  assert.deepStrictEqual(rec.intents, []);
  g.dblclick({});                  // …and the dblclick cancels the pending single click
  assert.deepStrictEqual(rec.intents, ['now:here'], 'exactly one action: the immediate open');
  assert.strictEqual(g.pendingClick, false);
  timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(rec.intents, ['now:here'], 'the deferred click never fires afterwards');
  assert.strictEqual(timers.pending, 0, 'no timer left behind');
});

test('a lone click acts after the double-click interval', () => {
  const timers = stubTimers();
  const rec = recorder();
  const g = createOpenGesture({ run: rec.run, touch: () => false, ...timers });
  g.click({ metaKey: true });
  assert.deepStrictEqual(rec.intents, []);
  timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(rec.intents, ['confirm:newtab'], '⌘+click → confirmation, new tab');
  // ⌘+double click: immediate, new tab, and only once.
  g.click({ ctrlKey: true });
  g.dblclick({ ctrlKey: true });
  timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(rec.intents, ['confirm:newtab', 'now:newtab']);
});

test('cancel() drops a pending click (the rename editor uses it)', () => {
  const timers = stubTimers();
  const rec = recorder();
  const g = createOpenGesture({ run: rec.run, touch: () => false, ...timers });
  g.click({});
  g.cancel();
  timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(rec.intents, [], 'nothing opens behind the rename input');
  assert.strictEqual(g.pendingClick, false);
});

// ── Touch ──
test('touch: a tap acts at once (no double-click wait) and confirms in this tab', () => {
  const timers = stubTimers();
  const rec = recorder();
  const g = createOpenGesture({ run: rec.run, touch: () => true, ...timers });
  g.pressStart({ x: 10, y: 10 });
  assert.strictEqual(g.pressEnd(), false, 'a quick lift in place is a tap, not a drag');
  g.click({});
  assert.deepStrictEqual(rec.intents, ['confirm:here'], 'tap → confirmation, this tab');
  assert.strictEqual(timers.pending, 0, 'touch never arms the double-click timer');
  // A double click can't happen on touch; if one is synthesized it is ignored.
  g.dblclick({});
  assert.deepStrictEqual(rec.intents, ['confirm:here']);
});

test('touch: a hold is the list\'s REORDER pickup — it never opens anything', () => {
  const timers = stubTimers();
  const rec = recorder();
  const g = createOpenGesture({ run: rec.run, touch: () => true, ...timers });
  // Press and hold in place, well past any threshold, then release: the touch drag
  // engine owns this (it picks the row up at 280ms and swallows the trailing click).
  g.pressStart({ x: 40, y: 80 });
  g.dragStart();                                    // drag.js onStart fires
  assert.strictEqual(g.pressEnd(), false);
  assert.deepStrictEqual(rec.intents, [], 'a hold never opens a project');
  // The click a drop may synthesize is swallowed…
  g.click({});
  assert.deepStrictEqual(rec.intents, []);
  // …and only that one: the next tap works normally.
  g.click({});
  assert.deepStrictEqual(rec.intents, ['confirm:here']);
});

test('MOVEMENT WINS: past the slop the gesture is a drag/scroll — the open is dropped', () => {
  const timers = stubTimers();
  const rec = recorder();
  const g = createOpenGesture({ run: rec.run, touch: () => true, ...timers });

  // A finger that travels (reorder drag, or a vertical scroll over the row).
  g.pressStart({ x: 40, y: 80 });
  assert.strictEqual(g.pressMove({ x: 40, y: 80 + DRAG_SLOP_PX }), false, 'within the slop: still a tap');
  assert.strictEqual(g.pressMove({ x: 40, y: 80 + DRAG_SLOP_PX + 1 }), true, 'past it: now a drag');
  assert.ok(g.dragging);
  assert.strictEqual(g.pressEnd(), true, 'the release reports a drag, not a tap');
  assert.deepStrictEqual(rec.intents, [], 'a completed drag opens nothing');
  g.click({});                                      // a drop-synthesized click…
  assert.deepStrictEqual(rec.intents, [], '…is swallowed too');

  // Horizontal travel (drag out to a zone) counts the same.
  g.pressStart({ x: 40, y: 80 });
  g.pressMove({ x: 40 + DRAG_SLOP_PX + 1, y: 80 });
  g.pressEnd();
  g.click({});
  assert.deepStrictEqual(rec.intents, []);
});

test('MOUSE: movement wins too — a deferred click dies when the drag starts', () => {
  const timers = stubTimers();
  const rec = recorder();
  const g = createOpenGesture({ run: rec.run, touch: () => false, ...timers });
  // Press, click (arms the 250ms deferral), then the HTML5 reorder drag begins.
  g.pressStart({ x: 10, y: 10 });
  g.click({});
  assert.ok(g.pendingClick);
  g.dragStart();
  assert.strictEqual(g.pendingClick, false, 'the pending open is cancelled by the pickup');
  timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(rec.intents, [], 'reordering never opens a project');
  // Same via pure travel, without a dragstart event.
  g.pressStart({ x: 10, y: 10 });
  g.click({});
  g.pressMove({ x: 10, y: 10 + DRAG_SLOP_PX + 1 });
  timers.advance(DOUBLE_CLICK_MS);
  assert.deepStrictEqual(rec.intents, []);
});

test('a slow, still press is just a click on either input', () => {
  for (const touch of [false, true]) {
    const timers = stubTimers();
    const rec = recorder();
    const g = createOpenGesture({ run: rec.run, touch: () => touch, ...timers });
    g.pressStart({ x: 0, y: 0 });
    assert.strictEqual(g.pressEnd(), false, 'no travel → not a drag');
    g.click({});
    timers.advance(DOUBLE_CLICK_MS);
    assert.deepStrictEqual(rec.intents, ['confirm:here'], `${touch ? 'touch' : 'mouse'}: a plain open`);
  }
});

