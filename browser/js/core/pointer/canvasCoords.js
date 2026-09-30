// Client px → canvas px, and the compare-divider grab band that shares the measurement.
// Both read the canvas box, which is why the rect is cached for the frame.
import constants from '../../../../common/config/constants.json' with { type: 'json' };

const { grabSlackPx: DIVIDER_GRAB_PX } = constants.COMPARE_DIVIDER;

// Measured ONCE PER FRAME: this runs 2-3x per mouse-move and getBoundingClientRect forces
// a layout each time (read/write/read thrash). No rAF ⇒ every call.
export const canvasCoords = (app, clientX, clientY) => {
  let rect = app.canvasRect;
  const sized = app.canvas.style?.width;
  if (rect && app.canvasRectAt !== sized) rect = null;
  if (!rect) {
    rect = app.canvas.getBoundingClientRect();
    if (typeof requestAnimationFrame === 'function') {
      app.canvasRect = rect;
      app.canvasRectAt = sized;
      requestAnimationFrame(() => { app.canvasRect = null; });
    }
  }
  const cssX = clientX - rect.left;
  const cssY = clientY - rect.top;
// Through the LIVE on-screen size, not app.scale: mid zoom-transition the two disagree.
  const sx = (rect.width > 0 && app.canvas.width > 0) ? rect.width / app.canvas.width : app.scale;
  const sy = (rect.height > 0 && app.canvas.height > 0) ? rect.height / app.canvas.height : app.scale;
  // A press just past the picture's edge lands on it: image px never leave [0, width] × [0, height].
  const w = app.canvas.width || Infinity;
  const h = app.canvas.height || Infinity;
  return { cssX, cssY, x: Math.min(Math.max(cssX / sx, 0), w), y: Math.min(Math.max(cssY / sy, 0), h) };
};

// Grab band of COMPARE_DIVIDER.grabSlackPx CSS px around the split divider; shared by
// PointerController and the hover cursor.
export const nearCompareDivider = (app, clientX, clientY) => {
  if (app.compareMode !== 'vertical' && app.compareMode !== 'horizontal') return false;
  const { cssX, cssY } = canvasCoords(app, clientX, clientY);
  const scale = app.scale || 1;
  const f = Math.min(1, Math.max(0, app.compareSplit ?? 0.5));
  return app.compareMode === 'vertical'
    ? Math.abs(cssX - app.canvas.width * f * scale) <= DIVIDER_GRAB_PX
    : Math.abs(cssY - app.canvas.height * f * scale) <= DIVIDER_GRAB_PX;
};
