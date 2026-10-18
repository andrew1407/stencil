// The skins' clock and hues (js/ui/cli/skin/table.js): a frame every 80 ms, never backwards; the
// rainbow walks the hue wheel by column and the bifrost slides it; the fairy lights take turns.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createSkinClock, hueAt, FRAME_MS } from '../../../../js/ui/cli/skin/table.js';

test('the clock moves a frame every 80 ms, never backwards', () => {
  assert.equal(FRAME_MS, 80);
  const c = createSkinClock();
  c.start(1000);
  assert.equal(c.syncClock(1079), false);
  assert.equal(c.syncClock(1080), true);
  assert.equal(c.frame(), 1);
  assert.equal(c.syncClock(1100), false);
  assert.equal(c.syncClock(1000 + 80 * 7), true);
  assert.equal(c.syncClock(500), true, 'a clock before the start holds frame 0');
  assert.equal(c.frame(), 0);
});

test('the rainbow walks the hue wheel by column; the bifrost slides one column a frame', () => {
  assert.equal(hueAt('rainbow', 0, 0), 'rgb(255, 64, 64)');
  assert.notEqual(hueAt('rainbow', 0, 0), hueAt('rainbow', 3, 0));
  assert.equal(hueAt('bifrost', 5, 1), hueAt('bifrost', 4, 0));
  assert.equal(hueAt('rainbow', 5, 1), hueAt('rainbow', 5, 0), 'the rainbow stands still');
});

test('a fairy light stays lit for its turn, then the next one comes', () => {
  const c = createSkinClock();
  c.start(0);
  const first = c.nextBulb();
  assert.ok(first);
  assert.equal(c.nextBulb(), null);
  for (let i = 0; i < 29; i++) c.tick();
  assert.equal(c.nextBulb(), null);
  c.tick();
  assert.notEqual(c.nextBulb(), first);
});
