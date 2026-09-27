// The app-side view of a hold-draw stroke: whether a press may become one, the ghost vertex
// under the finger, where the preview line emanates from, and the monotonic clock both input
// paths time holds against.
import { canvasGestureActive } from '../pointer/gesture.js';

export const nowMs = () => (typeof performance !== 'undefined' ? performance.now() : Date.now());

// A burst of moves paints the ghost once, on the next frame; clearing it paints now.
export const setHoldPreview = (app, x, y) => {
  app.holdPreview = { x, y };
  app.renderer.requestRedraw();
};

export const clearHoldPreview = (app) => {
  if (app.holdPreview) { app.holdPreview = null; app.renderer.redraw(); }
};

// The last point of the in-progress line, or the current tail of the line being extended.
export const holdAnchor = (app, prepend) => {
  if (app.currentLine && app.currentLine.points.length)
    return app.currentLine.points[app.currentLine.points.length - 1];
  if (app.continueLineIdx >= 0 && app.lines[app.continueLineIdx]) {
    const pts = app.lines[app.continueLineIdx].points;
    // Prepend: the current head; forward: the point just before the insertion tail.
    if (prepend) return pts[app.continueInsertIdx] ?? pts[0] ?? null;
    return pts[app.continueInsertIdx - 1] ?? pts[pts.length - 1] ?? null;
  }
  return null;
};

// A hold needs an image, drawing mode off, no other gesture, and not the rect tool (a hold
// there would seed a freehand line). Desktop twin: mousePressEvent's eligibleHold.
export const holdDrawEligible = (app) =>
  !!app?.image && !app.isDrawing && app.drawMode !== 'rect' && !canvasGestureActive(app);
