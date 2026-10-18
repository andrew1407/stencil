// The reveal pace (js/ui/console/reveal.js), the port of reveal.zig: the default speed keeps the
// tuned numbers, a slower one stretches every window to its floor and ceiling, 1 is instant,
// and a burst sweeps in fewer jumps.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { pace, createReveal } from '../../../js/ui/console/reveal.js';

test('pace: 0.5 is the tuned tempo, the default three times it; slower stretches, the max is instant', () => {
  assert.deepEqual(pace(0.5), { stepMs: 13, quietMs: 250, burstMaxMs: 900, instant: false });
  assert.deepEqual(pace(0.25), { stepMs: 39, quietMs: 750, burstMaxMs: 2700, instant: false }, 'the default 0.25: factor 3');
  const slow = pace(0.1);
  assert.ok(slow.stepMs > 13 && slow.quietMs > 250 && slow.burstMaxMs > 900);
  assert.equal(pace(0.01).stepMs, 1287);
  assert.equal(pace(0.01).quietMs, 5000, 'the ceiling');
  const fast = pace(0.9);
  assert.equal(fast.quietMs, 250, 'the quiet window never shrinks under its floor');
  assert.equal(pace(1).instant, true);
  assert.equal(pace('junk').stepMs, 39, 'junk reads as the default');
});

test('a lone line sweeps in 8 jumps, a burst in 2, a pouring burst lands at once', () => {
  const r = createReveal({ speed: () => 0.5 });
  assert.deepEqual(r.plan(1000), { jumps: 8, stepMs: 13 });
  assert.deepEqual(r.plan(1100), { jumps: 2, stepMs: 13 });
  for (const t of [1300, 1500, 1700, 1900]) assert.deepEqual(r.plan(t), { jumps: 2, stepMs: 13 });
  assert.deepEqual(r.plan(2000), { jumps: 0, stepMs: 0 }, 'past burstMax of continuous output');
  assert.deepEqual(r.plan(3000), { jumps: 8, stepMs: 13 }, 'after a pause the long form is back');
  r.done(3400);
  assert.deepEqual(r.plan(3600), { jumps: 2, stepMs: 13 }, 'the quiet window counts from the end of a sweep (reveal.zig stamps it on the way out)');
  assert.deepEqual(createReveal({ speed: () => 1 }).plan(0), { jumps: 0, stepMs: 0 });
});
