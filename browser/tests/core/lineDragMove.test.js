// The whole-line move (dragMove, js/core/touch/dragGestures.js): a LINE drag translates ALL points by the
// drag delta whatever the live Shift state, while a SEGMENT drag keeps its Shift upgrade (held → whole line).
// The gesture naturally ends with Shift released a beat before the button, so reading `shiftKey` then snapped
// every point but the grabbed segment's endpoints back to the snapshot.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { dragMove } from '../../js/core/touch/dragGestures.js';

// Stand-in with just the collaborators dragMove touches (the removeSelectedLines.test.js
// approach): a plain object, no DOM; its canvas sits at the origin at 1:1, so client px are image px.
const makeApp = (lines) => {
  const app = {
    lines,
    coordLineIdx: -1,
    scale: 1,
    isDraggingSegment: false,
    draggingSegment: null,
    isDraggingLine: false,
    draggingLine: null,
    redraws: 0,
    canvas: { width: 100, height: 100, style: {}, getBoundingClientRect: () => ({ left: 0, top: 0, width: 100, height: 100 }) },
    renderer: { redraw() { app.redraws++; }, requestRedraw() { app.redraws++; } },
    coordTable: { update() {}, refreshRows() {}, refreshCoordRow() {} },
  };
  return app;
};

const snapshot = (line) => line.points.map(p => ({ x: p.x, y: p.y }));

test('a line drag translates EVERY point, even with Shift released', () => {
  const line = { points: [{ x: 0, y: 0 }, { x: 10, y: 0 }, { x: 20, y: 5 }, { x: 30, y: 9 }] };
  const app = makeApp([line]);
  app.isDraggingLine = true;
  app.draggingLine = { lineIdx: 0, startX: 0, startY: 0, origPoints: snapshot(line), multiOrig: null };

  dragMove(app, 7, 3, /*shiftKey=*/false);   // Shift already up — the buggy path
  assert.deepEqual(line.points, [
    { x: 7, y: 3 }, { x: 17, y: 3 }, { x: 27, y: 8 }, { x: 37, y: 12 },
  ], 'all points (including the LAST) carry the full drag delta');

  dragMove(app, 2, -1, /*shiftKey=*/true);   // Shift held must be identical
  assert.deepEqual(line.points, [
    { x: 2, y: -1 }, { x: 12, y: -1 }, { x: 22, y: 4 }, { x: 32, y: 8 },
  ], 'deltas derive from the snapshot — toggling Shift never accumulates');
});

test('a multi-select line drag translates every selected line fully', () => {
  const a = { points: [{ x: 0, y: 0 }, { x: 4, y: 4 }] };
  const b = { points: [{ x: 10, y: 10 }, { x: 14, y: 10 }, { x: 18, y: 12 }] };
  const app = makeApp([a, b]);
  app.isDraggingLine = true;
  app.draggingLine = {
    lineIdx: 0, startX: 0, startY: 0, origPoints: snapshot(a),
    multiOrig: [{ li: 0, pts: snapshot(a) }, { li: 1, pts: snapshot(b) }],
  };

  dragMove(app, 5, 5, false);
  assert.deepEqual(a.points, [{ x: 5, y: 5 }, { x: 9, y: 9 }]);
  assert.deepEqual(b.points, [{ x: 15, y: 15 }, { x: 19, y: 15 }, { x: 23, y: 17 }],
    'the second selected line moves whole too — no partial iteration');
});

test('a segment drag still upgrades to the whole line only while Shift is held', () => {
  const line = { points: [{ x: 0, y: 0 }, { x: 10, y: 0 }, { x: 20, y: 0 }] };
  const app = makeApp([line]);
  app.isDraggingSegment = true;
  app.draggingSegment = {
    lineIdx: 0, ptIdx1: 0, ptIdx2: 1, startX: 0, startY: 0,
    origPt1: { x: 0, y: 0 }, origPt2: { x: 10, y: 0 },
    origPoints: snapshot(line),
  };

  dragMove(app, 3, 2, false);
  assert.deepEqual(line.points, [{ x: 3, y: 2 }, { x: 13, y: 2 }, { x: 20, y: 0 }],
    'without Shift only the grabbed segment endpoints move');

  dragMove(app, 3, 2, true);
  assert.deepEqual(line.points, [{ x: 3, y: 2 }, { x: 13, y: 2 }, { x: 23, y: 2 }],
    'with Shift the whole line translates from the snapshot');
});
