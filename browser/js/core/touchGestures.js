// ── Touch gestures: pure classification + pinch math ────────────
// DOM-free, time-injected helpers for DrawingApp.#wireTouch (the host owns the
// listeners/timers/rendering) so tap-vs-drag and pinch math stay unit-testable.
// All coordinates are client/screen px, so tolerances feel the same at any zoom.

// Thresholds: moveTol = wander allowed for a tap/long-press; tapMaxMs = longer
// press is no longer a tap; longPressMs = stationary press that opens the menu.
export const TOUCH_DEFAULTS = { moveTol: 8, tapMaxMs: 300, longPressMs: 500 };

export const dist = (ax, ay, bx, by) => Math.hypot(ax - bx, ay - by);

// Midpoint of two {clientX, clientY} touch points.
export const midpoint = (t0, t1) => ({
  x: (t0.clientX + t1.clientX) / 2,
  y: (t0.clientY + t1.clientY) / 2,
});

// Distance between two {clientX, clientY} touch points (the pinch span).
export const touchDist = (t0, t1) => dist(t0.clientX, t0.clientY, t1.clientX, t1.clientY);

// Classify a finished single-finger press given how far it moved and how long it
// lasted. A tap is short and near-stationary; anything else is a drag (or a hold
// that the host handled separately). Returns 'tap' | 'drag'.
export const classifyEnd = ({ moved, elapsed }, opts = {}) => {
  const { moveTol, tapMaxMs } = { ...TOUCH_DEFAULTS, ...opts };
  return moved <= moveTol && elapsed <= tapMaxMs ? 'tap' : 'drag';
};

// Has a stationary press qualified as a long-press yet?
export const isLongPress = ({ moved, elapsed }, opts = {}) => {
  const { moveTol, longPressMs } = { ...TOUCH_DEFAULTS, ...opts };
  return moved <= moveTol && elapsed >= longPressMs;
};
