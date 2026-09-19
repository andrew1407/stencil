// Single-finger direct manipulation: what a finger grabs where it lands, how that grab
// follows it, what a release does, and the synthetic click a stationary tap becomes.

export const findTouchById = (touches, id) => {
  for (let i = 0; i < touches.length; i++) if (touches[i].identifier === id) return touches[i];
  return null;
};

// A finger landing on a point/segment grabs it; the mode taken, or null for empty space.
export const grabTouchTarget = (app, t) => {
  const { x, y } = app.canvasCoords(t.clientX, t.clientY);
  const nearPt = app.findNearestPointWithIdx(x, y);
  if (nearPt) {
    app.isDraggingPoint = true;
    app.draggingPoint = nearPt;
    return 'point';
  }
  const nearSeg = app.findNearestSegmentWithIdx(x, y);
  if (nearSeg) {
    app.beginSegmentDrag(nearSeg, x, y);
    return 'segment';
  }
  return null;
};

export const touchDragTo = (app, st, t) => {
  if (st.mode === 'point') {
    const { x, y } = app.canvasCoords(t.clientX, t.clientY);
    app.movePointTo(app.draggingPoint, x, y);
    return;
  }
  app.dragMove(t.clientX, t.clientY, false);
};

export const touchDragEnd = (app, st) => {
  if (st.mode === 'point') app.endPointDrag(app.draggingPoint, false);
  else app.endSegmentDrag(false);
};

// Lifted without moving: drop the grab so the press falls through as a tap instead.
export const releaseTouchGrab = (app, st) => {
  if (st.mode === 'point') { app.isDraggingPoint = false; app.draggingPoint = null; }
  else { app.isDraggingSegment = false; app.draggingSegment = null; }
};

// A stationary tap is a modifier-free left click (canvasClick handles both cases).
export const tapClickAt = (app, e, st) => {
  const ct = (e.changedTouches && e.changedTouches[0]) || st;
  app.canvasClick({
    clientX: ct.clientX ?? st.startX, clientY: ct.clientY ?? st.startY,
    altKey: false, shiftKey: false, ctrlKey: false, metaKey: false,
  });
};
