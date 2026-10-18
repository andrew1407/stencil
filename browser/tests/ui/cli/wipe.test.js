// The accent wipe (js/ui/cli/wipe.js): 14 jumps at 61 ms, letters at 115 ms on the same clock,
// the rule ahead of the seam, a cancel settling everything at once.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { wipeSchedule, runWipe, ruleReach, wipeFrame } from '../../../js/ui/cli/wipe.js';

test('the schedule interleaves 14 wash jumps at 61 ms with letters at 115 ms', () => {
  const ev = wipeSchedule(70, 7);
  const washes = ev.filter((e) => e.kind === 'wash');
  assert.equal(washes.length, 13, '70 columns in jumps of 5: the last jump is the settle');
  assert.deepEqual(washes.slice(0, 3).map((e) => [e.at, e.x]), [[0, 5], [61, 10], [122, 15]]);
  assert.deepEqual(ev.filter((e) => e.kind === 'letter').map((e) => e.at), [0, 115, 230, 345, 460, 575, 690]);
  assert.ok(ev.every((e, i) => i === 0 || e.at >= ev[i - 1].at), 'sorted by due time');
});

test('runWipe paints the schedule through the timers and settles once; a cancel settles at once', () => {
  const timers = [];
  const schedule = (fn, ms) => { timers.push([fn, ms]); return timers.length; };
  const log = [];
  const paint = { wash: (x) => log.push(`w${x}`), letter: (i) => log.push(`l${i}`), settle: () => log.push('settle') };
  const cancel = runWipe({ reach: 30, letters: 2, paint, schedule, cancelTimer: () => log.push('cancelled') });
  while (timers.length) timers.shift()[0]();
  assert.deepEqual(log[0], 'w3', 'the first wash at t=0 (30 columns in 14 jumps is 3 a jump)');
  assert.ok(log.includes('l0') && log.includes('l1'));
  assert.equal(log.filter((l) => l === 'settle').length, 1);
  cancel();
  assert.equal(log.filter((l) => l === 'settle').length, 1, 'a cancel after the end settles nothing twice');
  const early = [];
  const stop = runWipe({ reach: 30, letters: 1, paint: { wash() {}, letter() {}, settle: () => early.push('settle') }, schedule: () => 1, cancelTimer: () => early.push('cancelled') });
  stop();
  assert.deepEqual(early, ['cancelled', 'settle']);
  const instant = [];
  runWipe({ reach: 30, letters: 1, paint: { wash() {}, letter() {}, settle: () => instant.push(1) }, instant: true });
  assert.deepEqual(instant, [1]);
});

test('the rule runs ahead of the seam, capped at the width; a frame names the header cells past the seam', () => {
  assert.equal(ruleReach(100, 10, 40), 44);
  assert.equal(ruleReach(100, 40, 40), 100);
  const cells = wipeFrame(10, 80, 80, 80);
  assert.ok(cells.some(([r, c0, c1]) => r === 1 && c0 === 0 && c1 >= 21));
});
