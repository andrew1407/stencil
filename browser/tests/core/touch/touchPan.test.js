// One-finger pan (js/core/touch/pan.js) and its place in the touch gesture machine
// (js/core/touch/input.js): a still tap stays a tap, a wander past the tolerance pans,
// grabs keep dragging, a drawing hold keeps drawing, and compare view only pans.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { beginTouchPan, touchPanTo, endTouchPan } from '../../../js/core/touch/pan.js';
import { touchHandlers } from '../../../js/core/touch/input.js';
import { TOUCH_DEFAULTS } from '../../../js/core/touch/gestures.js';
import { installGestureFlags } from '../../../js/core/pointer/gesture.js';

class Host { gesture = 'none'; }
installGestureFlags(Host.prototype);

const touch = (x, y, identifier = 1) => ({ clientX: x, clientY: y, identifier });
const ev = (touches, changed = touches) => ({ touches, changedTouches: changed, preventDefault() {} });

const rig = ({ point = null, segment = null, compare = false, drawing = false } = {}) => {
  const app = Object.assign(new Host(), {
    image: {},
    scale: 1,
    canvas: { width: 100, height: 100, style: {}, getBoundingClientRect: () => ({ left: 0, top: 0, width: 100, height: 100 }) },
    compareReadOnly: () => compare,
    findNearestPointWithIdx: () => point,
    findNearestSegmentWithIdx: () => segment,
    lines: [{ points: [{ x: 0, y: 0 }, { x: 50, y: 50 }] }],
  });
  const calls = [];
  const ctrl = {
    app,
    touchSession: null,
    holdDrawing: drawing,
    armHold: () => calls.push('armHold'),
    abandonHold: () => calls.push('abandonHold'),
    moveTapGesture: () => calls.push('moveTapGesture'),
    endTapGesture: () => calls.push('endTapGesture'),
  };
  const viewport = { scrollLeft: 200, scrollTop: 100 };
  return { app, ctrl, calls, viewport, h: touchHandlers(ctrl, viewport) };
};

const far = TOUCH_DEFAULTS.moveTol + 4;

test('pan maths: the viewport scrolls against the finger, 1:1, step by step', () => {
  const app = new Host();
  const st = { mode: 'tap', id: 1, startX: 10, startY: 10 };
  const viewport = { scrollLeft: 50, scrollTop: 50 };
  beginTouchPan(app, st);
  assert.equal(st.mode, 'pan');
  assert.equal(app.isPanning, true);
  touchPanTo(viewport, st, touch(30, 5));
  assert.deepEqual([viewport.scrollLeft, viewport.scrollTop], [30, 55]);
  touchPanTo(viewport, st, touch(25, 5));
  assert.deepEqual([viewport.scrollLeft, viewport.scrollTop], [35, 55]);
  endTouchPan(app);
  assert.equal(app.gesture, 'none');
});

test('a still tap on empty canvas arms the hold and ends as a tap', () => {
  const { ctrl, calls, h, viewport } = rig();
  h.onStart(ev([touch(10, 10)]));
  h.onMove(ev([touch(12, 11)]));
  h.onEnd(ev([], [touch(12, 11)]));
  assert.deepEqual(calls, ['armHold', 'moveTapGesture', 'endTapGesture']);
  assert.deepEqual([viewport.scrollLeft, viewport.scrollTop], [200, 100]);
  assert.equal(ctrl.touchSession, null);
});

test('a finger wandering past the tolerance pans, and never taps', () => {
  const { app, ctrl, calls, h, viewport } = rig();
  h.onStart(ev([touch(10, 10)]));
  h.onMove(ev([touch(10 + far, 10)]));
  assert.equal(ctrl.touchSession.mode, 'pan');
  assert.equal(app.isPanning, true);
  assert.deepEqual([viewport.scrollLeft, viewport.scrollTop], [200 - far, 100], 'the image stays under the finger');
  h.onMove(ev([touch(10 + far + 20, 30)]));
  assert.deepEqual([viewport.scrollLeft, viewport.scrollTop], [180 - far, 80]);
  h.onEnd(ev([], [touch(10 + far + 20, 30)]));
  assert.equal(app.isPanning, false);
  assert.deepEqual(calls, ['armHold', 'abandonHold']);
});

test('a hold that already draws keeps drawing instead of panning', () => {
  const { app, ctrl, calls, h, viewport } = rig({ drawing: true });
  h.onStart(ev([touch(10, 10)]));
  h.onMove(ev([touch(10 + far, 10)]));
  assert.equal(ctrl.touchSession.mode, 'tap');
  assert.equal(app.isPanning, false);
  assert.deepEqual(calls, ['armHold', 'moveTapGesture']);
  assert.deepEqual([viewport.scrollLeft, viewport.scrollTop], [200, 100]);
});

test('a finger on a point still drags the point, not the view', () => {
  const { app, ctrl, h, viewport } = rig({ point: { lineIdx: 0, ptIdx: 1 } });
  h.onStart(ev([touch(50, 50)]));
  assert.equal(ctrl.touchSession.mode, 'point');
  assert.equal(app.isDraggingPoint, true);
  assert.equal(app.isPanning, false);
  assert.deepEqual([viewport.scrollLeft, viewport.scrollTop], [200, 100]);
  h.onCancel();
});

test('compare view grabs nothing and arms no hold: a finger only pans', () => {
  const { app, ctrl, calls, h, viewport } = rig({ compare: true, point: { lineIdx: 0, ptIdx: 1 } });
  h.onStart(ev([touch(50, 50)]));
  assert.equal(ctrl.touchSession.mode, 'tap');
  assert.equal(app.isDraggingPoint, false);
  h.onMove(ev([touch(50 + far, 50)]));
  assert.equal(ctrl.touchSession.mode, 'pan');
  assert.ok(viewport.scrollLeft < 200);
  assert.ok(!calls.includes('armHold'));
});

test('a cancelled pan drops the pan flag', () => {
  const { app, ctrl, h } = rig();
  h.onStart(ev([touch(10, 10)]));
  h.onMove(ev([touch(10 + far, 10)]));
  assert.equal(app.isPanning, true);
  h.onCancel();
  assert.equal(app.isPanning, false);
  assert.equal(ctrl.touchSession, null);
});
