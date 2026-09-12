// Drawing a stroke: the new vertex FLIES to where you put it
// (js/ui/motion.js stroke* + js/core/strokeFx.js).
//
// A point now leaves where it came from — the point it extends, or its foot on the
// segment it splits — and travels to the click on a bowed path before it settles.
// Pinned here: the flight arithmetic, the controller (identity survives a splice, the
// loop runs only while something is in the air, reduced motion plays nothing), and the
// wiring. Desktop twin: desktop/tests/strokeGrowth.headless.cpp.
import test from 'node:test';
import assert from 'node:assert';
import { readFileSync } from 'node:fs';

import {
  strokeFlyMs, strokeFlyEase, strokeFlyPoint, strokeBow, strokeBowSign, strokeFlyRadius,
  strokePopScale, strokeRipple, strokeSpark, strokeWake, strokePhase, strokeVertexScale,
  strokeFoot,
  STROKE_FLY_MIN_MS, STROKE_FLY_MAX_MS, STROKE_FLY_PX_PER_MS, STROKE_BOW_MAX,
  STROKE_POP_PEAK, STROKE_FLY_R0, STROKE_RIPPLE_MS, STROKE_RIPPLE_REACH, STROKE_WAKE_ALPHA,
} from '../js/ui/motion.js';
import { StrokeFx } from '../js/core/strokeFx.js';
import { recordingCtx, argsOf, indexOf } from './helpers/recordingCtx.js';

const read = (p) => readFileSync(new URL(p, import.meta.url), 'utf8');
const drawingAppJs = read('../js/core/drawingApp.js');
const inputJs = read('../js/core/inputController.js');
const shapeJs = read('../js/core/shapeBuilder.js');   // insert / rect routes
const clickJs = read('../js/core/canvasClick.js');   // the click router
const exportJs = read('../js/core/exportService.js');

// ── 1. The arithmetic ───────────────────────────────────────────────────────

test('a flight is as long as the trip, floored and capped', () => {
  assert.equal(strokeFlyMs(0), STROKE_FLY_MIN_MS);
  assert.equal(strokeFlyMs(300), STROKE_FLY_MIN_MS + 300 / STROKE_FLY_PX_PER_MS);
  assert.equal(strokeFlyMs(4000), STROKE_FLY_MAX_MS);
  assert.ok(strokeFlyMs(80) < strokeFlyMs(400), 'further takes longer');
});

test('the ease is exact at both ends and overshoots in between', () => {
  assert.equal(strokeFlyEase(0), 0);
  assert.equal(strokeFlyEase(1), 1);
  assert.equal(strokeFlyEase(-0.4), 0, 'clamped below');
  assert.equal(strokeFlyEase(3), 1, 'clamped above');
  let peak = 0;
  for (let i = 0; i <= 100; i++) peak = Math.max(peak, strokeFlyEase(i / 100));
  assert.ok(peak > 1 && peak < 1.12, `overshoots, but only just (${peak})`);
});

test('the vertex starts on its anchor, lands on the click, and bows in between', () => {
  const a = { x: 10, y: 10 };
  const b = { x: 210, y: 10 };
  assert.deepEqual(strokeFlyPoint(a, b, 0, 1), { x: 10, y: 10 });
  assert.deepEqual(strokeFlyPoint(a, b, 1, 1), { x: 210, y: 10 });
  const mid = strokeFlyPoint(a, b, 0.5, 1);
  assert.ok(Math.abs(mid.y - 10) > 1, 'mid-flight it is off the straight line');
  const other = strokeFlyPoint(a, b, 0.5, -1);
  assert.ok((mid.y - 10) * (other.y - 10) < 0, 'the sign picks the side');
  assert.equal(strokeFlyPoint(a, b, 0.5, 0).y, 10, 'no bow → dead straight');
  assert.equal(strokeBow(1000), STROKE_BOW_MAX, 'the bow is capped on a long trip');
  assert.ok(Math.abs(strokeBowSign(31, 74)) <= 1);
  assert.equal(strokeBowSign(31, 74), strokeBowSign(31, 74), 'a hash, so it is reproducible');
});

test('the vertex swells in flight and settles after landing — with no jump between', () => {
  assert.equal(strokeFlyRadius(0), STROKE_FLY_R0, 'it leaves small');
  assert.equal(strokeFlyRadius(1), STROKE_POP_PEAK, 'and arrives at the peak');
  assert.equal(strokePopScale(0), STROKE_POP_PEAK, 'the settle starts where the flight ended');
  assert.equal(strokePopScale(1), 1, 'and ends at the size the point really is');
});

test('the ring and the spark are nothing at their ends and plenty in the middle', () => {
  assert.equal(strokeRipple(0).scale, 1);
  assert.equal(strokeRipple(1).alpha, 0);
  const half = strokeRipple(0.5);
  assert.ok(half.scale > 1 && half.scale < STROKE_RIPPLE_REACH);
  assert.equal(strokeSpark(0).alpha, 0, 'it must not smudge the anchor it left');
  assert.equal(strokeSpark(1).alpha, 0, 'nor the point it became');
  assert.ok(strokeSpark(0.5).alpha > 0.4, 'brightest mid-trip');
  assert.equal(strokeWake(0), STROKE_WAKE_ALPHA, 'the wake burns as the vertex leaves');
  assert.equal(strokeWake(1), 0, 'and is out once it has landed');
});

test('the settle and the ring both start the moment the flight ends', () => {
  const mid = strokePhase(50, 200);
  assert.equal(mid.fly, 0.25);
  assert.equal(mid.land, 0);
  assert.equal(mid.done, false);
  const landed = strokePhase(320, 200);
  assert.equal(landed.fly, 1);
  assert.equal(landed.land, 0.5, '120ms into a 240ms settle');
  assert.ok(strokePhase(200 + STROKE_RIPPLE_MS, 200).done, 'finished once the ring has gone');
  assert.equal(strokeVertexScale(strokePhase(0, 200)), STROKE_FLY_R0);
  assert.equal(strokeVertexScale(strokePhase(1e4, 200)), 1, 'and rests at its real size');
});

test('an inserted vertex comes out of its own foot on the segment it split', () => {
  const a = { x: 0, y: 0 };
  const b = { x: 100, y: 0 };
  assert.deepEqual(strokeFoot(a, b, 40, 25), { x: 40, y: 0 });
  assert.deepEqual(strokeFoot(a, b, -80, 5), { x: 0, y: 0 }, 'clamped to the segment');
  assert.deepEqual(strokeFoot(a, a, 9, 9), { x: 0, y: 0 }, 'a degenerate segment is its own foot');
  assert.deepEqual(strokeFoot(null, b, 7, 8), { x: 7, y: 8 }, 'no segment → no flight');
});

// ── 2. The controller ───────────────────────────────────────────────────────

// A clock and a frame scheduler under the test's control, plus a renderer that only
// counts. No DOM, no timers.
const harness = () => {
  let now = 0;
  const frames = [];
  const app = { pointSize: 4, redraws: 0, renderer: { redraw() { app.redraws++; } } };
  const fx = new StrokeFx(app, { now: () => now, schedule: (fn) => frames.push(fn) });
  return {
    app, fx, frames,
    at(t) { now = t; },
    frame() { const fns = frames.splice(0); fns.forEach((fn) => fn()); },
  };
};

const lineOf = (...pts) => ({ points: pts.map(([x, y]) => ({ x, y })), color: '#f00', thickness: 3, pointSize: 4 });

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

// ── 3. The wiring ───────────────────────────────────────────────────────────

test('every route that adds a point sends it flying', () => {
  // Five click/insert routes, plus the Alt+Ctrl pull-out, which adds a point too.
  const flights = (src) => (src.match(/(this|app)\.strokeFx\.flyIn\(/g) || []).length;
  assert.equal(flights(drawingAppJs) + flights(shapeJs) + flights(clickJs), 6, 'every route');
  assert.match(drawingAppJs, /this\.strokeFx\.flyIn\(line, idx, \{ x, y \}\)/,
    'a pulled-out point flies out of the spot it was pulled from');
  // …and all THREE rect routes: appended to a continued line, appended to the selected
  // line, and a standalone one drawn on empty space.
  assert.equal((shapeJs.match(/app\.strokeFx\.flyInRange\(/g) || []).length, 3,
    'shapeBuilder: every rect draws itself corner by corner');
  assert.match(shapeJs, /app\.strokeFx\.flyInRange\(rect, 0, corners\.length\)/,
    'a standalone rect starts at its own first corner');
  assert.match(shapeJs, /flyIn\(line, insertIdx, strokeFoot\(/,
    'an inserted vertex comes out of its foot on the segment');
  // the hold-draw seed, a hold drop on a continued line, and one on a fresh stroke
  assert.equal((inputJs.match(/app\.strokeFx\.flyIn\(/g) || []).length, 3,
    'inputController: the hold-to-draw and touch drops');
});

test('a restored or wiped set of lines grounds every flight', () => {
  // A flight holds the point OBJECT it belongs to; undo/redo swap in a snapshot's own
  // points and a wipe drops them all, so nothing in the air still has a vertex to be.
  assert.equal((drawingAppJs.match(/this\.strokeFx\.cancel\(\)/g) || []).length, 3,
    'undo, redo and clear-all');
  const h = harness();
  const line = lineOf([0, 0], [100, 0]);
  h.fx.flyIn(line, 1);
  h.fx.cancel();
  assert.equal(h.fx.active, false);
  assert.equal(h.fx.pointsOf(line), line.points);
});

test('the renderer draws the flown positions, at their flown size', async () => {
  const { Renderer } = await import('../js/core/renderer.js');
  const { ctx, calls } = recordingCtx();
  const line = lineOf([0, 0], [100, 0]);
  const flown = [{ x: 7, y: 8 }, { x: 40, y: 9 }];
  const seen = [];
  const at = (tag) => (c, l, pts) => seen.push([tag, c, l, pts, calls.length]);
  const fx = { pointsOf: () => flown, scaleAt: () => 3, paintUnder: at('under'), paintOver: at('over') };
  new Renderer({ ctx, strokeFx: fx, showPoints: true, pointSize: 4, listHoverLineIdx: -1 })
    .drawLine(line, false, 0);
  // A geometry pass that read the resting array, or a radius that skipped scaleAt, lands here.
  const geom = [...argsOf(calls, 'moveTo'), ...argsOf(calls, 'lineTo')];
  assert.equal(geom.length, line.points.length, 'one stroke pass, every vertex');
  for (const [x, y] of geom) assert.ok(flown.some((f) => f.x === x && f.y === y),
    `drew (${x},${y}) — no geometry pass may read the resting array`);
  assert.deepEqual(argsOf(calls, 'arc').map((a) => a[2]), [12, 12], 'pointSize x scaleAt');
  const stroke = indexOf(calls, 'stroke');
  assert.deepEqual(seen.map(([tag, c, l, pts]) => [tag, c, l, pts]),
    [['under', ctx, line, flown], ['over', ctx, line, flown]], 'both overlays, on the app ctx');
  assert.ok(seen[0][4] <= stroke && seen[1][4] > stroke, 'the wake under the stroke, the spark over');
});

test('an export is the resting picture — never a vertex caught mid-air', () => {
  assert.match(exportJs, /app\.strokeFx\.suspend\(\)/);
  assert.match(exportJs, /app\.strokeFx\.resume\(\)/, 'and it is put back afterwards');
});
