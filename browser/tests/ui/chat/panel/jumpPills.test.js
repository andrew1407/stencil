// The chat panel's jump pills (ui/chat/panel/jumpPills.js), driven for real over stub elements:
// the pills follow the scroll position, and a burst of transcript mutations reads layout once per frame.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStubElement } from '../../../helpers/dom.js';
import { wireJumpPills } from '../../../../js/ui/chat/panel/jumpPills.js';

// A transcript whose geometry reads are counted; the observer and rAF are captured, not run.
const rig = () => {
  const reads = { n: 0 };
  const transcript = createStubElement('div', { scrollTop: 0, clientHeight: 100, contains: () => true });
  // After creation: the factory spreads its overrides, which would read a getter once and freeze it.
  Object.defineProperty(transcript, 'scrollHeight', { get() { reads.n++; return 400; } });
  const jumps = createStubElement('div');
  const pills = [createStubElement('button'), createStubElement('button')];
  const observers = [];
  const frames = [];
  const saved = { MutationObserver: globalThis.MutationObserver, requestAnimationFrame: globalThis.requestAnimationFrame };
  globalThis.MutationObserver = class { constructor(cb) { this.cb = cb; observers.push(this); } observe() {} };
  globalThis.requestAnimationFrame = (fn) => { frames.push(fn); return frames.length; };
  wireJumpPills({ transcript, jumps, jumpPills: pills });
  const restore = () => Object.assign(globalThis, saved);
  const flush = () => { for (const fn of frames.splice(0)) fn(); };
  return { transcript, jumps, reads, observers, flush, restore };
};

test('the pills follow the scroll: ⌄ below the top, ⌃ once scrolled past the beginning', () => {
  const r = rig();
  try {
    r.transcript.dispatch('scroll');
    assert.ok(r.jumps.classList.contains('can-down') && !r.jumps.classList.contains('can-up'));
    r.transcript.scrollTop = 300;
    r.transcript.dispatch('scroll');
    assert.ok(r.jumps.classList.contains('can-up') && !r.jumps.classList.contains('can-down'));
  } finally { r.restore(); }
});

test('a burst of transcript mutations measures the transcript once, on the next frame', () => {
  const r = rig();
  try {
    assert.equal(r.observers.length, 1, 'one observer on the transcript');
    for (let i = 0; i < 5; i++) r.observers[0].cb([], r.observers[0]);
    assert.equal(r.reads.n, 0, 'nothing is measured inside the mutation batch');
    r.flush();
    assert.equal(r.reads.n, 1, 'five batches, one layout read');
    assert.ok(r.jumps.classList.contains('can-down'), 'and the pills are synced by it');
  } finally { r.restore(); }
});
