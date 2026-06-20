import { test } from 'node:test';
import assert from 'node:assert/strict';
import { HoldDrawController, holdDrawTarget } from '../js/core/holdDraw.js';

// ── holdDrawTarget: what an initial hold over (x,y) targets ──────
// Mirrors core/tests/holdDraw.test.cpp.

const L = (...pts) => ({ points: pts.map(([x, y]) => ({ x, y })) });

test('holdDrawTarget: empty space → new line', () => {
  assert.deepEqual(holdDrawTarget([], 5, 5), { kind: 'new', lineIdx: -1, ptIdx: -1, ptIdx2: -1 });
  const lines = [L([0, 0], [10, 0])];
  assert.equal(holdDrawTarget(lines, 200, 200).kind, 'new');
});

test('holdDrawTarget: near a point → continue that line from it', () => {
  const lines = [L([0, 0], [100, 0]), L([10, 10], [50, 50])];
  const t = holdDrawTarget(lines, 11, 9); // within 12px of lines[1].points[0]
  assert.equal(t.kind, 'point');
  assert.equal(t.lineIdx, 1);
  assert.equal(t.ptIdx, 0);
});

test('holdDrawTarget: point beats segment when both are in range', () => {
  const lines = [L([0, 0], [100, 0])];
  // (2,0) is on the segment AND within 12px of the endpoint (0,0) → point wins.
  assert.equal(holdDrawTarget(lines, 2, 0).kind, 'point');
});

test('holdDrawTarget: on a segment body (not a point) → insert', () => {
  const lines = [L([0, 0], [100, 0])];
  const t = holdDrawTarget(lines, 50, 3); // mid-segment, far from both endpoints
  assert.equal(t.kind, 'segment');
  assert.equal(t.lineIdx, 0);
  assert.equal(t.ptIdx, 0);
  assert.equal(t.ptIdx2, 1);
});

test('holdDrawTarget: topmost (last) line wins for an overlapping point', () => {
  const lines = [L([0, 0], [10, 0]), L([0, 0], [10, 0])];
  assert.equal(holdDrawTarget(lines, 0, 0).lineIdx, 1);
});

// ── HoldDrawController: pure, time-injected gesture state machine ─

test('controller: quick release before holdDelay never starts drawing (a click)', () => {
  const c = new HoldDrawController({ holdDelay: 500 });
  assert.equal(c.pointerDown(10, 10, 0).type, 'armed');
  assert.equal(c.state, 'armed');
  assert.equal(c.tick(200), null);          // not yet
  assert.equal(c.pointerUp(300), null);     // released early → no commit
  assert.equal(c.state, 'idle');
});

test('controller: moving past tolerance while armed aborts (drag/click, not hold)', () => {
  const c = new HoldDrawController({ holdDelay: 500, moveTolerance: 6 });
  c.pointerDown(10, 10, 0);
  assert.equal(c.pointerMove(13, 11, 50), null);      // within tolerance
  assert.equal(c.pointerMove(30, 10, 100).type, 'abort'); // moved away
  assert.equal(c.state, 'aborted');
  assert.equal(c.tick(600), null);                    // no start after abort
  assert.equal(c.pointerUp(700), null);               // no commit
});

test('controller: hold fires start at the press point after holdDelay', () => {
  const c = new HoldDrawController({ holdDelay: 500 });
  c.pointerDown(20, 30, 0);
  assert.equal(c.tick(400), null);
  const s = c.tick(500);
  assert.deepEqual(s, { type: 'start', x: 20, y: 30 });
  assert.equal(c.active, true);
});

test('controller: move emits preview while drawing', () => {
  const c = new HoldDrawController({ holdDelay: 500 });
  c.pointerDown(0, 0, 0);
  c.tick(500); // start
  assert.deepEqual(c.pointerMove(40, 0, 510), { type: 'preview', x: 40, y: 0 });
});

test('controller: dwell after moving away drops a point at the rest spot', () => {
  const c = new HoldDrawController({ holdDelay: 500, moveTolerance: 6, rearmDistance: 10 });
  c.pointerDown(0, 0, 0);
  c.tick(500);                 // start → first point at (0,0)
  c.pointerMove(40, 0, 510);   // moved away (past rearm) and to a new rest spot
  assert.equal(c.tick(900), null);          // only 390ms still
  const d = c.tick(1010);                   // 500ms since rest at t=510
  assert.deepEqual(d, { type: 'drop', x: 40, y: 0 });
});

test('controller: no repeat drop without moving away (rearm gate)', () => {
  const c = new HoldDrawController({ holdDelay: 500, moveTolerance: 6, rearmDistance: 10 });
  c.pointerDown(0, 0, 0);
  c.tick(500);                 // start
  c.pointerMove(40, 0, 510);
  assert.equal(c.tick(1010).type, 'drop');  // first drop at (40,0)
  // Staying put → no second drop even after another holdDelay
  c.pointerMove(41, 0, 1100);  // within tolerance of last rest, within rearm of last drop
  assert.equal(c.tick(2000), null);
});

test('controller: release after drawing commits', () => {
  const c = new HoldDrawController({ holdDelay: 500 });
  c.pointerDown(0, 0, 0);
  c.tick(500);
  assert.deepEqual(c.pointerUp(800), { type: 'commit' });
  assert.equal(c.state, 'idle');
});

test('controller: setHoldDelay updates the threshold; cancel resets', () => {
  const c = new HoldDrawController({ holdDelay: 500 });
  c.setHoldDelay(200);
  assert.equal(c.holdDelay, 200);
  c.pointerDown(0, 0, 0);
  assert.equal(c.tick(200).type, 'start');
  c.cancel();
  assert.equal(c.state, 'idle');
});
