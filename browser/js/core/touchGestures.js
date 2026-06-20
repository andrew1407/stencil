// ── Touch gestures: pure classification + pinch math ────────────
// Helpers for the touchscreen input layer in DrawingApp. Kept DOM-free and
// time-injected (like ./holdDraw.js) so the decision logic is unit-testable
// without real TouchEvents. The host (DrawingApp.#wireTouch) owns the DOM
// listeners, coordinate conversion, timers and rendering; this module only
// answers "was that a tap or a drag?" and does the pinch arithmetic.
//
// All coordinates are in client/screen space (px). Tolerances stay in screen
// space so they feel the same regardless of zoom level.

// Default thresholds. moveTol: how far a finger may wander and still count as a
// tap / a stationary long-press. tapMaxMs: a press longer than this is not a tap
// (it became a hold-to-draw or a drag). longPressMs: stationary press that opens
// the context menu — matches the hold-draw delay feel.
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
