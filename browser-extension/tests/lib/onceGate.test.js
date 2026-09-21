// Tests for src/lib/onceGate.js — the guard that makes a drag released on a menu item
// run its action EXACTLY once, whatever else the release produces (a synthesized click
// on top of the drop, a listener that sees the event twice on its way to the document).
// The clock is injected, so no timers here.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { DISPATCH_WINDOW_MS, createOnceGate } from '../../src/lib/onceGate.js';

const stubClock = () => {
  const c = { t: 1000 };
  c.now = () => c.t;
  c.advance = (ms) => { c.t += ms; };
  return c;
};

test('the first call of a release wins and the rest are refused', () => {
  const clock = stubClock();
  const gate = createOnceGate({ now: clock.now });
  assert.equal(gate.allow(), true, 'the drop dispatches');
  assert.equal(gate.allow(), false, 'a second path for the same release does not');
  clock.advance(10);                       // a click synthesized right after the drop
  assert.equal(gate.allow(), false);
  assert.equal(gate.suppressed(), true);
});

test('a later, genuinely separate release dispatches again', () => {
  const clock = stubClock();
  const gate = createOnceGate({ now: clock.now });
  assert.equal(gate.allow(), true);
  clock.advance(DISPATCH_WINDOW_MS);       // the window is exclusive at its edge
  assert.equal(gate.suppressed(), false);
  assert.equal(gate.allow(), true);
});

test('suppressed() reports the window without consuming the gate', () => {
  const clock = stubClock();
  const gate = createOnceGate({ now: clock.now, windowMs: 100 });
  assert.equal(gate.suppressed(), false, 'nothing dispatched yet');
  assert.equal(gate.allow(), true);
  assert.equal(gate.suppressed(), true);
  clock.advance(99);
  assert.equal(gate.suppressed(), true);
  clock.advance(1);
  assert.equal(gate.suppressed(), false);
  assert.equal(gate.allow(), true, 'suppressed() never armed the gate itself');
});

test('reset re-arms it immediately', () => {
  const clock = stubClock();
  const gate = createOnceGate({ now: clock.now });
  gate.allow();
  gate.reset();
  assert.equal(gate.suppressed(), false);
  assert.equal(gate.allow(), true);
});

test('the default window is short enough to be invisible, long enough to catch a click', () => {
  assert.equal(DISPATCH_WINDOW_MS, 300);
  const gate = createOnceGate();
  assert.equal(gate.allow(), true);
  assert.equal(gate.allow(), false);        // real clock: the two calls are microseconds apart
});
