// The shared face swap (js/ui/motion.js swapContent): decoration over a synchronous write, so
// rapid toggling leaves no stale glyph, doubled label or stuck animation. From drawToggle.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { installDom, createStubElement } from './helpers/dom.js';
import { swapContent, SWAP_CLASS, SWAP_GHOST_CLASS, SWAP_MS } from '../js/ui/motion.js';

installDom();

// A button whose querySelectorAll actually resolves its ghost children (the stub's
// default returns []), so the "drop a ghost still in flight" path is exercised.
const makeBtn = () => {
  const el = createStubElement('button');
  el.querySelectorAll = () => el.children.filter((c) => c.classes?.has?.(SWAP_GHOST_CLASS));
  return el;
};
const ghostsOf = (el) => el.children.filter((c) => c.classes?.has?.(SWAP_GHOST_CLASS));
// Collects the helper's timers so a test can fire them when it chooses.
const timerBox = () => {
  const q = [];
  return { setTimer: (fn) => q.push(fn), flush: () => { const all = q.splice(0); all.forEach((fn) => fn()); } };
};

test('the first paint just writes the face — nothing to swap from', () => {
  const btn = makeBtn();
  const t = timerBox();
  assert.equal(swapContent(btn, '<span>Start</span>', { key: 'start', setTimer: t.setTimer }), false);
  assert.equal(btn.innerHTML, '<span>Start</span>');
  assert.equal(btn.classes.has(SWAP_CLASS), false);
  assert.equal(ghostsOf(btn).length, 0);
});

test('a changed face writes FIRST, then plays — the old face leaves as a ghost', () => {
  const btn = makeBtn();
  const t = timerBox();
  swapContent(btn, '<span>Start</span>', { key: 'start', setTimer: t.setTimer });
  assert.equal(swapContent(btn, '<span>Stop</span>', { key: 'stop', setTimer: t.setTimer }), true);
  assert.equal(btn.innerHTML, '<span>Stop</span>', 'the DOM is never behind the state');
  assert.equal(btn.classes.has(SWAP_CLASS), true);
  const [ghost] = ghostsOf(btn);
  assert.equal(ghost.innerHTML, '<span>Start</span>', 'the ghost carries the face that left');
  assert.equal(ghost.getAttribute('aria-hidden'), 'true', 'a duplicate label must not reach a screen reader');
  t.flush();
  assert.equal(btn.classes.has(SWAP_CLASS), false, 'the animation class is cleaned up');
  assert.equal(ghostsOf(btn).length, 0, 'and so is the ghost');
});

test('an unchanged face is a no-op — updateButtons runs on every redraw', () => {
  const btn = makeBtn();
  const t = timerBox();
  swapContent(btn, '<span>Start</span>', { key: 'start', setTimer: t.setTimer });
  btn.innerHTML = 'TOUCHED';   // a rewrite would clobber this; a no-op leaves it
  for (let i = 0; i < 5; i++) {
    assert.equal(swapContent(btn, '<span>Start</span>', { key: 'start', setTimer: t.setTimer }), false);
  }
  assert.equal(btn.innerHTML, 'TOUCHED');
  assert.equal(ghostsOf(btn).length, 0, 'no ghost, no animation, no churn');
});

test('a fresh element (a re-rendered toolbar) paints rather than being skipped as unchanged', () => {
  const t = timerBox();
  const first = makeBtn();
  swapContent(first, '<span>Start</span>', { key: 'start', setTimer: t.setTimer });
  const rebuilt = makeBtn();
  swapContent(rebuilt, '<span>Start</span>', { key: 'start', setTimer: t.setTimer });
  assert.equal(rebuilt.innerHTML, '<span>Start</span>');
});

test('reduced motion: the new face, instantly, with no ghost and no class', () => {
  const btn = makeBtn();
  const t = timerBox();
  const opts = { setTimer: t.setTimer, reduced: () => true };
  swapContent(btn, '<span>Start</span>', { key: 'start', ...opts });
  assert.equal(swapContent(btn, '<span>Stop</span>', { key: 'stop', ...opts }), false);
  assert.equal(btn.innerHTML, '<span>Stop</span>', 'the final state is still correct');
  assert.equal(btn.classes.has(SWAP_CLASS), false);
  assert.equal(ghostsOf(btn).length, 0);
});

test('rapid toggling: ghosts never stack, and a stale timer cannot strip a newer swap', () => {
  const btn = makeBtn();
  const t = timerBox();
  const face = (on) => swapContent(btn, `<span>${on ? 'Stop' : 'Start'}</span>`,
    { key: on ? 'stop' : 'start', setTimer: t.setTimer });
  face(false);
  for (let i = 0; i < 12; i++) {
    face(i % 2 === 1);
    assert.ok(ghostsOf(btn).length <= 1, 'at most one ghost is ever in flight');
  }
  // The 11 superseded timers fire late — the newest swap must survive them...
  assert.equal(btn.classes.has(SWAP_CLASS), true);
  t.flush();
  // ...and once the last one has run, nothing is left playing or hanging around.
  assert.equal(btn.innerHTML, '<span>Stop</span>', 'the final face is the last one asked for');
  assert.equal(btn.classes.has(SWAP_CLASS), false, 'no stuck animation');
  assert.equal(ghostsOf(btn).length, 0, 'no leftover ghost');
});

test('swapContent survives a bare element (no ghost to make, no throw)', () => {
  const t = timerBox();
  const bare = { innerHTML: '', classList: { add() {}, remove() {} } };
  assert.doesNotThrow(() => {
    swapContent(bare, 'a', { key: 'a', setTimer: t.setTimer });
    swapContent(bare, 'b', { key: 'b', setTimer: t.setTimer });
  });
  assert.equal(bare.innerHTML, 'b');
  assert.equal(SWAP_MS > 0, true);
});
