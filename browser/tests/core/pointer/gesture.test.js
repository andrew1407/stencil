// The seven exclusive drag/pan flags are one Gesture state: raising a flag ends any other,
// lowering one ends only itself, and the move and release handlers are picked by that state.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { GESTURE_FLAGS, installGestureFlags, activeGesture, canvasGestureActive } from '../../../js/core/pointer/gesture.js';
import { releasePointer } from '../../../js/core/pointer/release.js';

class Host { gesture = 'none'; }
installGestureFlags(Host.prototype);

test('the flags are one state: two can never both be set', () => {
  const h = new Host();
  h.isPanning = true;
  assert.equal(h.gesture, 'pan');
  h.isDraggingPoint = true;
  assert.equal(h.gesture, 'point');
  assert.equal(h.isPanning, false, 'raising a flag ends the gesture before it');
  h.isPanning = false;
  assert.equal(h.gesture, 'point', 'lowering a flag that is not the gesture changes nothing');
  h.isDraggingPoint = false;
  assert.equal(h.gesture, 'none');
  assert.equal(Object.keys(GESTURE_FLAGS).filter((f) => h[f]).length, 0);
});

test('a rig with plain booleans answers through the same flags', () => {
  assert.equal(activeGesture({}), 'none');
  assert.equal(activeGesture({ isDraggingSegment: true }), 'segment');
  assert.equal(canvasGestureActive({ isRectDrawDragging: true }), true);
  assert.equal(canvasGestureActive({ isDraggingCompareSplit: true }), false, 'the divider is not a canvas drag');
});

// The releases are the real drag ends (touch/dragGestures.js): each clears its own drag, saves one
// step, and hands the Alt state to the cursor — 'grab' with Alt held, 'crosshair' without.
test('the release is picked by the gesture', () => {
  const calls = [];
  const segment = { lineIdx: 0, ptIdx1: 0, ptIdx2: 1 };
  const app = new Host();
  Object.assign(app, {
    draggingPoint: { lineIdx: 0, ptIdx: 1 },
    draggingSegment: segment,
    canvas: { style: { cursor: 'move' } },
    saveHistory: () => calls.push(['save']),
  });
  releasePointer(app, { altKey: true });
  assert.deepEqual([calls, app.canvas.style.cursor, app.dragJustEnded], [[], 'move', undefined],
    'no gesture, no release');
  app.isDraggingPoint = true;
  releasePointer(app, { altKey: true });
  assert.deepEqual([calls, app.draggingPoint, app.canvas.style.cursor, app.dragJustEnded],
    [[['save']], null, 'grab', true], 'the point release ends the point drag, with the Alt it saw');
  assert.equal(app.draggingSegment, segment, 'and never the segment release');
  app.isDraggingLine = true;
  app.draggingLine = { lineIdx: 0 };
  releasePointer(app, { altKey: false });
  assert.deepEqual([calls, app.draggingLine, app.canvas.style.cursor], [[['save'], ['save']], null, 'crosshair']);
  assert.equal(app.gesture, 'none', 'the line release ends its gesture');
});
