// The StrokeFx controller (js/core/strokeFx.js): identity survives a splice, the loop runs only
// while something is in the air, and reduced motion plays nothing. From strokeGrowth.test.js.
import test from 'node:test';
import assert from 'node:assert';

import { STROKE_FLY_R0 } from '../../../js/ui/motion.js';
import { StrokeFx } from '../../../js/core/line/strokeFx.js';
import { harness, lineOf } from '../../helpers/strokeGrowthHarness.js';


test('an appended vertex leaves the point it extends, and lands exactly where it was put', () => {
  const h = harness();
  const line = lineOf([0, 0], [100, 0]);
  h.fx.flyIn(line, 1);
  assert.ok(h.fx.has(line));
  assert.deepEqual(h.fx.pointsOf(line)[0], line.points[0], 'the point it left does not move');
  assert.deepEqual(h.fx.pointsOf(line)[1], { x: 0, y: 0 }, 'the new one starts on its anchor');
  h.at(1e4);
  assert.deepEqual(h.fx.pointsOf(line)[1], line.points[1], 'and ends on the click');
});

test('a prepended vertex leaves the head it now hangs off', () => {
  const h = harness();
  const line = lineOf([50, 50], [0, 0]);   // the new point is index 0
  h.fx.flyIn(line, 0);
  assert.deepEqual(h.fx.pointsOf(line)[0], { x: 0, y: 0 }, 'from the point after it');
});

test("a line's first point has nowhere to come from — it only pops", () => {
  const h = harness();
  const line = lineOf([30, 30]);
  h.fx.flyIn(line, 0);
  assert.deepEqual(h.fx.pointsOf(line)[0], line.points[0], 'it never leaves its own spot');
  assert.ok(h.fx.scaleAt(line.points[0]) < 1, 'but it is still growing into place');
});

test('a flight stays with its own vertex when a later insert splices the array', () => {
  const h = harness();
  const line = lineOf([0, 0], [100, 0]);
  const flown = line.points[1];
  h.fx.flyIn(line, 1);
  line.points.splice(1, 0, { x: 20, y: 20 });   // every later index shifts
  h.at(1);
  assert.deepEqual(h.fx.pointsOf(line)[1], { x: 20, y: 20 }, 'the spliced point is at rest');
  assert.notDeepEqual(h.fx.pointsOf(line)[2], flown, 'and the flight followed its own vertex');
});

test('sending the same vertex again replaces its flight instead of stacking clocks', () => {
  const h = harness();
  const line = lineOf([0, 0], [100, 0]);
  h.fx.flyIn(line, 1);
  h.at(80);
  h.fx.flyIn(line, 1);
  assert.equal(h.fx.scaleAt(line.points[1]), STROKE_FLY_R0, 'the new clock starts at zero');
});

test('the frame loop runs only while something is in the air', () => {
  const h = harness();
  const line = lineOf([0, 0], [100, 0]);
  h.fx.flyIn(line, 1);
  assert.equal(h.frames.length, 1, 'one frame is queued, not a running timer');
  h.at(10);
  h.frame();
  assert.equal(h.app.redraws, 1);
  assert.equal(h.frames.length, 1, 'still flying → another frame');
  h.at(1e4);
  h.frame();
  assert.equal(h.app.redraws, 2, 'the resting picture is the last frame drawn');
  assert.equal(h.frames.length, 0, 'and then the loop stops');
  assert.equal(h.fx.active, false);
});

test("a rect's corners are staggered, so the shape draws itself edge by edge", () => {
  const h = harness();
  const line = lineOf([0, 0], [10, 0], [10, 10], [0, 10]);
  h.fx.flyInRange(line, 1, 3);
  const pts = h.fx.pointsOf(line);
  assert.deepEqual(pts[1], { x: 0, y: 0 }, 'the first corner is on the move');
  assert.deepEqual(pts[3], { x: 10, y: 10 }, 'the last is still parked on the corner before it');
});

test('a standalone rect starts at its head, so that corner pops instead of flying backwards', () => {
  const h = harness();
  const line = lineOf([0, 0], [10, 0], [10, 10], [0, 10]);
  h.fx.flyInRange(line, 0, 4);
  const pts = h.fx.pointsOf(line);
  assert.deepEqual(pts[0], { x: 0, y: 0 }, 'the head never leaves its own spot');
  assert.ok(h.fx.scaleAt(line.points[0]) < 1, 'it only grows into place');
  assert.deepEqual(pts[1], { x: 0, y: 0 }, 'and the next corner leaves it');
});

test('nothing flies at all under reduced motion', () => {
  const h = harness();
  const line = lineOf([0, 0], [100, 0]);
  globalThis.matchMedia = () => ({ matches: true });
  try {
    assert.equal(h.fx.flyIn(line, 1), null);
    assert.equal(h.fx.has(line), false);
    assert.equal(h.frames.length, 0, 'and no frame is ever queued');
  } finally {
    delete globalThis.matchMedia;
  }
});

test("the host's own rAF is called on the host, not on the StrokeFx", () => {
  // Stored bare and called as `this.#schedule(step)`, requestAnimationFrame gets the
  // StrokeFx as its receiver and the whole flight dies on "Illegal invocation".
  const seen = [];
  globalThis.requestAnimationFrame = function (fn) { seen.push(this); return 1; };
  try {
    const fx = new StrokeFx({ renderer: { redraw() {} } }, { now: () => 0 });
    fx.flyIn(lineOf([0, 0], [100, 0]), 1);
    assert.equal(seen.length, 1);
    assert.ok(!(seen[0] instanceof StrokeFx), 'the scheduler is never called on the effect itself');
  } finally {
    delete globalThis.requestAnimationFrame;
  }
});

test('a host with no frame scheduler at all simply draws the resting picture', () => {
  const app = { renderer: { redraw() {} } };
  const fx = new StrokeFx(app, { now: () => 0, schedule: null });
  const line = lineOf([0, 0], [100, 0]);
  assert.equal(fx.flyIn(line, 1), null);
  assert.equal(fx.pointsOf(line), line.points, 'the array itself, untouched');
  assert.equal(fx.scaleAt(line.points[1]), 1);
});

test('a line with nothing in the air is not copied, and paints no overlay', () => {
  const h = harness();
  const line = lineOf([0, 0], [100, 0]);
  assert.equal(h.fx.pointsOf(line), line.points, 'the same array, not a clone');
  let calls = 0;
  const ctx = new Proxy({}, { get: () => () => { calls++; } });
  h.fx.paintUnder(ctx, line, line.points);
  h.fx.paintOver(ctx, line, line.points);
  assert.equal(calls, 0);
});
