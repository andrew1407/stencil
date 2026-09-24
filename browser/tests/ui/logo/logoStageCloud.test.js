// The stage cloud (js/ui/logo/stageCloud.js) hugs the mark evenly in every style: water's sag and
// fire's lift push a grain out of the mark, never down or up the screen. Desktop twin: drifted().
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createStageCloud, driftedStageMote } from '../../../js/ui/logo/stageCloud.js';
import { styleFrame, PARTICLE_STYLES } from '../../../js/ui/dust/flight.js';

// Deterministic, so the centroid is the same run to run.
const seeded = (seed) => () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 2 ** 32);

const centroidOf = (style, placeOf) => {
  const cloud = createStageCloud(style), rnd = seeded(7);
  for (let i = 0; i < 90; i++) cloud.step(16.7, 200, 1, { rnd });
  let sx = 0, sy = 0, n = 0;
  for (let frame = 0; frame < 30; frame++) {
    for (let i = 0; i < 4; i++) cloud.step(16.7, 200, 1, { rnd });
    for (const at of placeOf(cloud, frame * 60)) { sx += at.x; sy += at.y; n++; }
  }
  return { x: sx / n, y: sy / n, n };
};

test('every style\'s cloud is centred on the mark within 2px', () => {
  for (const style of ['dust', 'water', 'fire']) {
    const c = centroidOf(style, (cloud, t) => cloud.placed(t));
    assert.ok(c.n > 10000, style);
    assert.ok(Math.abs(c.x) < 2 && Math.abs(c.y) < 2, `${style}: centroid (${c.x.toFixed(2)}, ${c.y.toFixed(2)})`);
  }
});

test('the drift points out of the mark by the size of the sag or lift, its sway along the edge', () => {
  const f = { sx: 3, sy: -5 };
  assert.deepEqual(driftedStageMote({ x: 10, y: 0 }, f), { x: 15, y: -3 });
  assert.deepEqual(driftedStageMote({ x: 0, y: -10 }, f), { x: -3, y: -15 });
  assert.deepEqual(driftedStageMote({ x: 0, y: 0 }, f), { x: 3, y: -5 }, 'at the centre it falls back to the screen');
  const code = PARTICLE_STYLES.water, frame = styleFrame(code, 0.5, 0.5, 0.3, 100, 0, {});
  const m = { x: 0, y: -40 };
  assert.ok(driftedStageMote(m, frame).y <= m.y, 'water above the mark rises away from it, never sinks into it');
});
