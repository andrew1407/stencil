// lib/pollClock.js — one interval for every poll-while-open job in the panel (shared pins
// + editor previews), so editor mode doesn't run a second timer on the same 8s tick.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createPollClock, POLL_MS } from '../../src/lib/pollClock.js';

// Fake timers: record what was scheduled, fire on demand.
const fakeTimers = () => {
  const live = new Map();
  let next = 1;
  return {
    live,
    setInterval: (fn, ms) => { live.set(next, { fn, ms }); return next++; },
    clearInterval: (id) => { live.delete(id); },
    tick: () => { for (const { fn } of [...live.values()]) fn(); },
  };
};

test('one timer serves both jobs, and every job runs on the tick', () => {
  const t = fakeTimers();
  const clock = createPollClock(POLL_MS, t);
  const ran = [];
  clock.add(() => ran.push('shared'));
  clock.add(() => ran.push('editors'));
  assert.equal(t.live.size, 1);
  assert.equal([...t.live.values()][0].ms, POLL_MS);
  t.tick();
  assert.deepEqual(ran, ['shared', 'editors']);
});

test('the timer only exists while a job is registered', () => {
  const t = fakeTimers();
  const clock = createPollClock(POLL_MS, t);
  const a = () => {}, b = () => {};
  assert.equal(clock.running, false);
  clock.add(a);
  clock.add(b);
  clock.remove(a);
  assert.equal(t.live.size, 1, 'one job left — still ticking');
  clock.remove(b);
  assert.equal(t.live.size, 0);
  assert.equal(clock.running, false);
});

test('adding the same job twice still ticks it once', () => {
  const t = fakeTimers();
  const clock = createPollClock(POLL_MS, t);
  let n = 0;
  const job = () => { n++; };
  clock.add(job);
  clock.add(job);
  assert.equal(clock.size, 1);
  t.tick();
  assert.equal(n, 1);
});

test('stop() drops every job and its timer (pagehide)', () => {
  const t = fakeTimers();
  const clock = createPollClock(POLL_MS, t);
  clock.add(() => { throw new Error('must not run'); });
  clock.stop();
  assert.equal(t.live.size, 0);
  t.tick();
});
