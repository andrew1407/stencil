// The two ways back out of a closed shape (js/core/touch/dragGestures.js ringPoints / openRingAt /
// unchainLine / pullOutPoint): the selection panel's Unchain, and Alt+Ctrl/⌘+drag, which pulls a
// new point out of the line and breaks the area open at the spot pulled. A rect is the same thing
// with no closing duplicate. Closing is chainEdit-close, the wiring chainEdit-wiring.
import test from 'node:test';
import assert from 'node:assert';
import { ringPoints, openRingAt, unchainLine, pullOutPoint } from '../../../js/core/touch/dragGestures.js';
import { fillState } from '../../../js/core/layout.js';

const P = (...xy) => xy.map(([x, y]) => ({ x, y }));
// A shape closed by clicking its first point: the closing DUPLICATE at the end.
const closedShape = () => ({ locked: true, points: P([0, 0], [10, 0], [10, 10], [0, 0]) });
// A rect: locked, four corners, no duplicate.
const rect = () => ({ locked: true, points: P([0, 0], [10, 0], [10, 10], [0, 10]) });

// ── The ring ────────────────────────────────────────────────────────────────

test('ringPoints drops the closing duplicate, and leaves a rect alone', () => {
  assert.equal(ringPoints(closedShape().points).length, 3);
  assert.equal(ringPoints(rect().points).length, 4, 'a rect closes without a duplicate');
  assert.deepEqual(ringPoints(P([1, 2], [3, 4])), P([1, 2], [3, 4]), 'an open line is its own ring');
  assert.deepEqual(ringPoints([]), [], 'and an empty one does not throw');
});

test('openRingAt re-roots the ring so the seam is where you pulled', () => {
  const opened = openRingAt(closedShape().points, 1);
  assert.deepEqual(opened, P([10, 0], [10, 10], [0, 0], [10, 0]),
    'starts at vertex 1, runs all the way round, ends on a copy of it');
  assert.equal(opened.length, 4, 'three ring points plus the free end');
  assert.deepEqual(openRingAt(closedShape().points, 0), P([0, 0], [10, 0], [10, 10], [0, 0]),
    'breaking at point 0 is the shape it already looked like — now open');
  assert.deepEqual(openRingAt(rect().points, 3), P([0, 10], [0, 0], [10, 0], [10, 10], [0, 10]));
});

test('openRingAt copies its points — the opened line never aliases the closed one', () => {
  const shape = closedShape();
  const opened = openRingAt(shape.points, 1);
  opened[0].x = 999;
  assert.equal(shape.points[1].x, 10, 'the original ring is untouched');
});

// ── Unchaining ──────────────────────────────────────────────────────────────

test('unchainLine turns an area back into an open line and clears its fill', () => {
  const shape = closedShape();
  shape.fillColor = '#3399ff';
  assert.equal(unchainLine(shape), true);
  assert.equal(shape.locked, false);
  assert.deepEqual(shape.points, P([0, 0], [10, 0], [10, 10]), 'the closing duplicate is gone');
  // An open line has no area to paint, so the fill goes to 'transparent' — the app's own "no fill",
  // which fillState reads as unchecked — and re-closing brings the Fill field up CLEARED.
  assert.equal(shape.fillColor, 'transparent');
  assert.equal(fillState(shape, '#3399ff').enabled, false, 'the panel shows no fill');
});

test('unchainLine opens a rect too, and refuses anything that is not an area', () => {
  const r = rect();
  assert.equal(unchainLine(r), true);
  assert.deepEqual(r.points, P([0, 0], [10, 0], [10, 10], [0, 10]), 'all four corners kept');
  assert.equal(unchainLine({ locked: false, points: P([0, 0], [1, 1]) }), false);
  assert.equal(unchainLine(null), false);
});

// ── Pulling a new point out ─────────────────────────────────────────────────

test('pull-out on an open line duplicates the vertex you grabbed', () => {
  const line = { locked: false, points: P([0, 0], [10, 0], [20, 0]) };
  const idx = pullOutPoint(line, { kind: 'point', ptIdx: 1 }, 11, 4);
  assert.equal(idx, 2, 'the copy sits right after the point it came from');
  assert.deepEqual(line.points, P([0, 0], [10, 0], [10, 0], [20, 0]));
});

test('pull-out on a segment body puts the new point under the cursor', () => {
  const line = { locked: false, points: P([0, 0], [20, 0]) };
  const idx = pullOutPoint(line, { kind: 'segment', ptIdx: 0, ptIdx2: 1 }, 9, 5);
  assert.equal(idx, 1);
  assert.deepEqual(line.points, P([0, 0], [9, 5], [20, 0]));
});

test('pull-out on an AREA breaks it open at the vertex pulled', () => {
  const shape = closedShape();
  shape.fillColor = '#3399ff';
  const idx = pullOutPoint(shape, { kind: 'point', ptIdx: 1 }, 11, 4);
  assert.equal(shape.locked, false, 'it stops being an area');
  assert.equal(shape.fillColor, 'transparent', 'and its fill goes with the shape');
  assert.equal(idx, shape.points.length - 1, 'the free end is what you drag');
  assert.deepEqual(shape.points, P([10, 0], [10, 10], [0, 0], [10, 0]),
    'the seam is at vertex 1, not at point 0');
});

test('pull-out on an area SEGMENT breaks it and the loose end follows the cursor', () => {
  const r = rect();
  const idx = pullOutPoint(r, { kind: 'segment', ptIdx: 1, ptIdx2: 2 }, 14, 6);
  assert.equal(r.locked, false);
  assert.deepEqual(r.points[idx], { x: 14, y: 6 }, 'the break follows the pointer');
  assert.deepEqual(r.points.slice(0, idx), P([10, 10], [0, 10], [0, 0], [10, 0]),
    'and the rest of the ring runs from the vertex it broke at');
});

test('pull-out declines when there is nothing under the cursor', () => {
  assert.equal(pullOutPoint(null, { kind: 'point', ptIdx: 0 }, 0, 0), -1);
  assert.equal(pullOutPoint({ points: [] }, null, 0, 0), -1);
  assert.equal(pullOutPoint({ points: P([0, 0]) }, { kind: 'point', ptIdx: 7 }, 0, 0), -1);
});
