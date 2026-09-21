// The touchscreen flow's gesture machine: which of tap / point / segment / pinch a press
// became, and how each follows and ends. The hold-draw half stays in inputController.js.
import { TOUCH_DEFAULTS } from './touchGestures.js';
import { nowMs } from '../draw/holdDrawView.js';
import { beginPinch, pinchTo, applyPinchFrame, endPinch } from './touchPinch.js';
import { findTouchById, grabTouchTarget, touchDragTo, touchDragEnd,
         releaseTouchGrab, tapClickAt } from './touchDrag.js';

export const touchHandlers = (ctrl, viewport) => {
  const app = ctrl.app;
  const moveTol = TOUCH_DEFAULTS.moveTol;
  let longPressTimer = null;

  const clearLongPress = () => {
    if (longPressTimer) { clearTimeout(longPressTimer); longPressTimer = null; }
  };
  const dropSingle = () => {
    clearLongPress();
    ctrl.abandonHold();
    app.isDraggingPoint = false; app.draggingPoint = null;
    app.isDraggingSegment = false; app.draggingSegment = null;
  };
  // Pinch DOM writes are coalesced into one rAF; onMove just stashes the latest scale+midpoint.
  const applyPinch = () => {
    const st = ctrl.touchSession;
    if (!st || st.mode !== 'pinch' || !st.pending) { if (st) st.raf = null; return; }
    applyPinchFrame(app, viewport, st);
  };

  // Empty-space holds belong to hold-to-draw.
  const armGeometryLongPress = (t) => {
    longPressTimer = setTimeout(() => {
      longPressTimer = null;
      if (!ctrl.touchSession || ctrl.touchSession.id !== t.identifier) return;
      dropSingle();
      ctrl.touchSession = { mode: 'done', id: t.identifier };
      app.canvas.dispatchEvent(new MouseEvent('contextmenu',
        { clientX: t.clientX, clientY: t.clientY, bubbles: true, cancelable: true }));
    }, TOUCH_DEFAULTS.longPressMs);
  };

  const onStart = e => {
    if (!app.image) return;

    // Two fingers abandon any in-flight single-finger gesture.
    if (e.touches.length >= 2) {
      e.preventDefault();
      dropSingle();
      ctrl.touchSession = beginPinch(app, viewport, e.touches[0], e.touches[1]);
      return;
    }
    if (e.touches.length !== 1) return;
    e.preventDefault();
    const t = e.touches[0];

    const grabbed = grabTouchTarget(app, t);
    if (grabbed) {
      ctrl.touchSession = { mode: grabbed, id: t.identifier, startX: t.clientX, startY: t.clientY };
      armGeometryLongPress(t);
      return;
    }

    ctrl.touchSession = { mode: 'tap', id: t.identifier, startX: t.clientX, startY: t.clientY, startT: nowMs() };
    ctrl.armHold(t.clientX, t.clientY);
  };

  const onMove = e => {
    const st = ctrl.touchSession;
    if (!st) return;

    if (st.mode === 'pinch') {
      if (e.touches.length < 2) return;
      e.preventDefault();
      pinchTo(app, st, e.touches[0], e.touches[1]);
      st.raf ??= requestAnimationFrame(applyPinch);
      return;
    }

    const t = findTouchById(e.touches, st.id);
    if (!t) return;
    e.preventDefault();
    const moved = Math.hypot(t.clientX - st.startX, t.clientY - st.startY);

    if (st.mode === 'point' || st.mode === 'segment') {
      if (moved <= moveTol) return;
      clearLongPress();
      st.dragged = true;
      touchDragTo(app, st, t);
      return;
    }
    if (st.mode === 'tap') {
      st.moved = Math.max(st.moved || 0, moved);
      ctrl.moveTapGesture(t.clientX, t.clientY);
    }
  };

  const onEnd = e => {
    const st = ctrl.touchSession;
    if (!st) return;

    if (st.mode === 'pinch') {
      // Ignore the lone remaining finger until all are up, so lifting one doesn't start a stray drag.
      endPinch(app, st, applyPinch);
      ctrl.touchSession = e.touches.length === 0 ? null : { mode: 'done', id: -1 };
      return;
    }

    if (e.touches.length > 0) return;
    clearLongPress();

    if (st.mode === 'point' || st.mode === 'segment') {
      if (st.dragged) touchDragEnd(app, st);
      else { releaseTouchGrab(app, st); tapClickAt(app, e, st); }
    } else if (st.mode === 'tap') {
      ctrl.endTapGesture(st, () => tapClickAt(app, e, st));
    }
    ctrl.touchSession = null;
  };

  const onCancel = () => {
    if (ctrl.touchSession && ctrl.touchSession.raf) cancelAnimationFrame(ctrl.touchSession.raf);
    dropSingle();
    ctrl.touchSession = null;
  };

  return { onStart, onMove, onEnd, onCancel };
};
