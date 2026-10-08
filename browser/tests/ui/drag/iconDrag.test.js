// The toolbar-control drag machine (js/ui/drag/iconDrag.js): the slop, a refused start, a release
// back over the control cancelling, the drop reporting where it landed, and the count of drags
// started. Pure — no DOM.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createIconDrag, dragsStarted, DRAG_SLOP_PX } from '../../../js/ui/drag/iconDrag.js';

const ORIGIN = { left: 100, top: 10, right: 128, bottom: 38 };
const rig = (over = {}) => {
  const calls = [];
  const m = createIconDrag({
    originRect: () => ORIGIN,
    targetAt: (x, y) => ({ id: `el@${x},${y}` }),
    start: (p) => { calls.push(['start', p.from.x, p.from.y]); return over.refuse ? false : undefined; },
    move: (p) => calls.push(['move', p.x, p.y, p.overOrigin]),
    drop: (p) => calls.push(['drop', p.x, p.y, p.target.id]),
    cancel: () => calls.push(['cancel']),
  });
  return { m, calls };
};

test('a move inside the slop is still a press', () => {
  const { m, calls } = rig();
  m.press(110, 20);
  assert.equal(m.move(110 + DRAG_SLOP_PX, 20), false);
  assert.equal(m.active, false);
  assert.equal(m.release(110 + DRAG_SLOP_PX, 20), false, 'the click stays the press\'s');
  assert.deepEqual(calls, []);
});

test('past the slop the drag starts once and every move reports where it is', () => {
  const { m, calls } = rig();
  m.press(110, 20);
  assert.equal(m.move(110, 20 + DRAG_SLOP_PX + 1), true);
  assert.equal(m.move(300, 400), true);
  assert.equal(m.active, true);
  assert.deepEqual(calls, [
    ['start', 110, 20],
    ['move', 110, 20 + DRAG_SLOP_PX + 1, true],
    ['move', 300, 400, false],
  ]);
});

test('released away from the control drops on what is under the pointer', () => {
  const { m, calls } = rig();
  m.press(110, 20);
  m.move(300, 400);
  assert.equal(m.release(320, 410), true);
  assert.deepEqual(calls.at(-1), ['drop', 320, 410, 'el@320,410']);
  assert.equal(m.active, false);
});

test('released back over the control it left, the drag cancels', () => {
  const { m, calls } = rig();
  m.press(110, 20);
  m.move(300, 400);
  m.move(112, 22);
  assert.equal(m.release(112, 22), true, 'the click is still swallowed');
  assert.deepEqual(calls.at(-1), ['cancel']);
  assert.ok(!calls.some(([k]) => k === 'drop'));
});

test('a refused start leaves the press alone for the rest of the gesture', () => {
  const { m, calls } = rig({ refuse: true });
  m.press(110, 20);
  assert.equal(m.move(300, 400), false);
  assert.equal(m.move(320, 420), false, 'not asked again');
  assert.equal(m.release(320, 420), false);
  assert.deepEqual(calls, [['start', 110, 20]]);
});

test('abort cancels a live drag and is a no-op otherwise', () => {
  const { m, calls } = rig();
  assert.equal(m.abort(), false);
  m.press(110, 20);
  m.move(300, 400);
  assert.equal(m.abort(), true);
  assert.equal(m.release(300, 400), false, 'the release after an abort is nobody\'s');
  assert.deepEqual(calls.at(-1), ['cancel']);
});

test('only a drag that starts counts as one', () => {
  const before = dragsStarted();
  const { m } = rig();
  m.press(110, 20);
  m.move(110 + DRAG_SLOP_PX, 20);
  m.release(110 + DRAG_SLOP_PX, 20);
  assert.equal(dragsStarted(), before, 'a press inside the slop is no drag');
  const refused = rig({ refuse: true }).m;
  refused.press(110, 20);
  refused.move(300, 400);
  assert.equal(dragsStarted(), before, 'nor is a refused start');
  m.press(110, 20);
  m.move(300, 400);
  m.move(310, 410);
  assert.equal(dragsStarted(), before + 1, 'a started drag counts once, however far it goes');
});
