import { canvasCoords } from '../pointer/canvasCoords.js';
import { canvasClick } from '../pointer/canvasClick.js';
import { beginSegmentDrag, movePointTo, endPointDrag, endSegmentDrag, dragMove } from './dragGestures.js';
import { nowMs } from '../draw/holdDrawView.js';
import constants from '../../../../common/config/constants.json' with { type: 'json' };
// Single-finger direct manipulation: what a finger grabs where it lands, how that grab
// follows it, what a release does, and the synthetic click a stationary tap becomes.

export const findTouchById = (touches, id) => {
  for (let i = 0; i < touches.length; i++) if (touches[i].identifier === id) return touches[i];
  return null;
};

// A finger landing on a point/segment grabs it; the mode taken, or null for empty space.
export const grabTouchTarget = (app, t) => {
  const { x, y } = canvasCoords(app, t.clientX, t.clientY);
  const nearPt = app.findNearestPointWithIdx(x, y);
  if (nearPt) {
    app.isDraggingPoint = true;
    app.draggingPoint = nearPt;
    return 'point';
  }
  const nearSeg = app.findNearestSegmentWithIdx(x, y);
  if (nearSeg) {
    beginSegmentDrag(app, nearSeg, x, y);
    return 'segment';
  }
  return null;
};

export const touchDragTo = (app, st, t) => {
  if (st.mode === 'point') {
    const { x, y } = canvasCoords(app, t.clientX, t.clientY);
    movePointTo(app, app.draggingPoint, x, y);
    return;
  }
  dragMove(app, t.clientX, t.clientY, false);
};

export const touchDragEnd = (app, st) => {
  if (st.mode === 'point') endPointDrag(app, app.draggingPoint, false);
  else endSegmentDrag(app, false);
};

// Lifted without moving: drop the grab so the press falls through as a tap instead.
export const releaseTouchGrab = (app, st) => {
  if (st.mode === 'point') { app.isDraggingPoint = false; app.draggingPoint = null; }
  else { app.isDraggingSegment = false; app.draggingSegment = null; }
};

// A tap's `detail` counts like a mouse click's: one more per tap within doubleTapMs and the slop.
const { doubleTapMs, pressSlopPx } = constants.POPOVER;
let lastTap = null;
const tapDetail = (x, y, t) => {
  const repeat = lastTap && t - lastTap.t <= doubleTapMs && Math.hypot(x - lastTap.x, y - lastTap.y) <= pressSlopPx;
  lastTap = { x, y, t, detail: repeat ? lastTap.detail + 1 : 1 };
  return lastTap.detail;
};

// A stationary tap is a modifier-free left click (canvasClick handles both cases).
export const tapClickAt = (app, e, st) => {
  const ct = (e.changedTouches && e.changedTouches[0]) || st;
  const clientX = ct.clientX ?? st.startX, clientY = ct.clientY ?? st.startY;
  canvasClick(app, {
    clientX, clientY, detail: tapDetail(clientX, clientY, nowMs()), pointerType: 'touch',
    altKey: false, shiftKey: false, ctrlKey: false, metaKey: false,
  });
};
