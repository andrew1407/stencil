// Drawing a stroke: the new vertex FLIES to where you put it (js/ui/motion.js stroke* + js/core/strokeFx.js),
// leaving the point it extends — or its foot on the segment it splits — and settling after a bowed path.
// Pinned: the flight arithmetic, the controller (identity survives a splice, the loop runs only while something
// is in the air, reduced motion plays nothing), and the wiring. Desktop twin: strokeGrowth.headless.cpp.
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
