// js/net/redial.js: the delay ladder doubles to its cap, a feed that held a whole cap restarts it,
// and a cancel leaves nothing scheduled.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { Redial, redialDelay, REDIAL_BASE_MS, REDIAL_CAP_MS } from '../../js/net/redial.js';

test('redialDelay doubles from the base and stays at the cap', () => {
  assert.deepEqual([0, 1, 2, 3, 4, 5, 6, 40].map(redialDelay), [1000, 2000, 4000, 8000, 16000, 30000, 30000, 30000]);
  assert.equal(redialDelay(0), REDIAL_BASE_MS);
  assert.equal(redialDelay(99), REDIAL_CAP_MS);
});

test('dropped schedules one run per drop; a feed up for a whole cap restarts the ladder', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout', 'Date'] });
  let runs = 0;
  const r = new Redial(() => { runs++; });
  assert.deepEqual([r.dropped(), r.dropped(), r.dropped()], [1000, 2000, 4000]);
  assert.equal(r.pending, true);
  t.mock.timers.tick(4000);
  assert.equal(runs, 1, 'a later drop replaces the scheduled run');
  r.opened();
  t.mock.timers.tick(REDIAL_CAP_MS - 1);
  assert.equal(r.dropped(), 8000, 'a short-lived feed keeps climbing');
  r.opened();
  t.mock.timers.tick(REDIAL_CAP_MS);
  assert.equal(r.dropped(), 1000);
  assert.equal(runs, 2, 'the 8 s run fired while the feed was up');
  r.cancel();
  assert.equal(r.pending, false);
  t.mock.timers.tick(60_000);
  assert.equal(runs, 2, 'a cancelled retry never runs');
});
