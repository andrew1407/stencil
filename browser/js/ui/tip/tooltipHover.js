// Which target the cursor is pointing at, and the wake-up delay before it appears.
// Split out of ui/tooltip.js, which owns the box these decisions reveal.

// Shared by mousemove and the Shift/Ctrl key-refresh. `immediate` skips the reveal delay: a
// modifier changing what is shown for the SAME hover is a live update, not a fresh hover.
export function decideHover(tip, clientX, clientY, x, y, mods, immediate = false) {
  if (mods.altKey) {
    tip.hide();
    return;
  }
  // In a comparison view only what the EDITED half shows may be labelled, and each branch is
  // judged by the coordinates it is about to display (drawingApp.compareShowsPoint).
  const visible = (px, py) => tip.app.compareShowsPoint(px, py);
  // Ctrl held → show the live cursor-position coordinates
  if ((mods.ctrlKey || mods.metaKey) && !mods.shiftKey) {
    if (visible(x, y)) tip.scheduleShow('coords', () => tip.show(clientX, clientY, x, y), immediate);
    else tip.hide();
    return;
  }
  const point = tip.app.findNearestPoint(x, y);
  if (point) {
    // The POINT's own coordinates decide: one just across the divider from the
    // pointer is not visible, however close the cursor is to it.
    if (visible(point.x, point.y)) {
      tip.scheduleShow(`point:${point.x}:${point.y}`,
        () => tip.show(clientX, clientY, point.x, point.y), immediate);
    } else tip.hide();
    return;
  }
  const lineIdx = tip.app.findLineAt(x, y);
  // A line is hit-tested AT the cursor, so the cursor is the part of it being pointed
  // at — a line straddling the divider answers for its visible half only.
  if (lineIdx !== -1 && visible(x, y)) {
    tip.scheduleShow(`line:${lineIdx}:${mods.shiftKey}`,
      () => tip.showLine(clientX, clientY, tip.app.lines[lineIdx], mods.shiftKey), immediate);
  } else tip.hide();
}

// Re-run the tooltip logic at the last known cursor position with given modifiers.
// Lets Shift (full points) / Ctrl (cursor coords) tooltips appear immediately on keypress.
export function refreshHover(tip, mods) {
  if (!tip.app.mouseOverCanvas || !tip.app.image) return;
  if (tip.app.isPanning || tip.app.isDraggingPoint || tip.app.isDraggingSegment ||
      tip.app.isDraggingLine || tip.app.isZoomRectDragging || tip.app.isRectDrawDragging ||
      tip.app.input.holdEngaged) return;
  const { x, y } = tip.app.canvasCoords(tip.app.lastMouseClientX, tip.app.lastMouseClientY);
  tip.applyHover(tip.app.lastMouseClientX, tip.app.lastMouseClientY, x, y, mods, /* immediate */ true);
}

// Debounced by the target hovered, not by the mouse event: a target change re-arms the delay,
// the SAME target just feeds fresher content and position.
export function scheduleReveal(tip, key, revealFn, immediate) {
  if (immediate) {
    clearTimeout(tip.showTimer);
    tip.showTimer = null;
    tip.pendingKey = null;
    tip.pendingReveal = null;
    tip.shownKey = key;
    revealFn();
    return;
  }
  if (tip.shownKey === key) { revealFn(); return; }
  if (tip.pendingKey === key) { tip.pendingReveal = revealFn; return; }
  // A different target: if one is actually ON SCREEN, take it down (dust and all) —
  // otherwise nothing has appeared yet, so there's only a timer to drop, not a hide.
  if (tip.shownKey != null) tip.hide();
  else { clearTimeout(tip.showTimer); tip.showTimer = null; }
  tip.pendingKey = key;
  tip.pendingReveal = revealFn;
  tip.showTimer = setTimeout(() => {
    tip.showTimer = null;
    tip.shownKey = tip.pendingKey;
    tip.pendingKey = null;
    const fn = tip.pendingReveal;
    tip.pendingReveal = null;
    fn?.();
  }, tip.constructor.SHOW_DELAY_MS);
}
