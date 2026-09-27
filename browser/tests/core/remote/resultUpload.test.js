// The rendered result trails the layout pushes: after the edits go quiet, or after the gap of
// steady editing, never twice inside the gap, and at once on flush.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ResultUploader } from '../../../js/core/remote/resultUpload.js';

const rig = () => {
  let t = 0;
  const timers = new Map();
  let seq = 0;
  const runs = [];
  const up = new ResultUploader(() => runs.push(t), {
    idleMs: 2000, minGapMs: 10000, now: () => t,
    setTimer: (fn, ms) => { timers.set(++seq, { at: t + ms, fn }); return seq; },
    clearTimer: (id) => timers.delete(id),
  });
  const advance = (ms) => {
    const end = t + ms;
    for (;;) {
      const next = [...timers.entries()].sort((a, b) => a[1].at - b[1].at)[0];
      if (!next || next[1].at > end) break;
      timers.delete(next[0]);
      t = next[1].at;
      next[1].fn();
    }
    t = end;
  };
  return { up, runs, advance };
};

test('a burst of edits uploads once, when it goes quiet', () => {
  const { up, runs, advance } = rig();
  for (let i = 0; i < 50; i++) { up.markDirty(); advance(100); }
  assert.deepEqual(runs, [], 'nothing while the edits keep coming inside the idle window');
  advance(2000);
  assert.deepEqual(runs, [6900], 'the idle window after the last edit');
  assert.equal(up.pending, false);
});

test('steady editing uploads at most once per gap', () => {
  const { up, runs, advance } = rig();
  for (let i = 0; i < 300; i++) { up.markDirty(); advance(100); }   // 30 s, never idle
  assert.ok(runs.length >= 2 && runs.length <= 3, `${runs.length} uploads in 30 s`);
  for (let i = 1; i < runs.length; i++) assert.ok(runs[i] - runs[i - 1] >= 10000, 'never inside the gap');
});

test('an edit right after an upload waits out the gap; flush and cancel', () => {
  const { up, runs, advance } = rig();
  up.markDirty(); advance(2000);
  assert.equal(runs.length, 1);
  up.markDirty(); advance(2000);
  assert.equal(runs.length, 1, 'quiet, but inside the gap');
  advance(8000);
  assert.deepEqual(runs, [2000, 12000], 'at the end of the gap');
  up.markDirty();
  up.flush();
  assert.equal(runs.length, 3, 'flush sends at once');
  up.flush();
  assert.equal(runs.length, 3, 'nothing pending, nothing sent');
  up.markDirty(); up.cancel(); advance(60000);
  assert.equal(runs.length, 3, 'cancelled');
});

// A browser's setTimeout/clearTimeout throw "Illegal invocation" when called with a foreign `this`.
test('the injected clock and timers are called bare, as the native ones must be', () => {
  const bare = (fn) => function (...args) {
    if (this !== undefined) throw new TypeError('Illegal invocation');
    return fn(...args);
  };
  let fired = 0;
  const up = new ResultUploader(() => { fired++; }, {
    idleMs: 0, minGapMs: 0, now: bare(() => 0),
    setTimer: bare((fn) => { fn(); return 1; }), clearTimer: bare(() => {}),
  });
  up.markDirty();
  up.markDirty();
  up.cancel();
  assert.equal(fired, 2);
});
