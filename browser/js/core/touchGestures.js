// Pure classification + pinch math for DrawingApp.#wireTouch. All coordinates are
// client/screen px, so tolerances feel the same at any zoom.

// moveTol: wander allowed for a tap/long-press; tapMaxMs: a longer press is no longer a tap;
// longPressMs: a stationary press that opens the menu.
export const TOUCH_DEFAULTS = Object.freeze({ moveTol: 8, tapMaxMs: 300, longPressMs: 500 });

export const dist = (ax, ay, bx, by) => Math.hypot(ax - bx, ay - by);

export const midpoint = (t0, t1) => ({
  x: (t0.clientX + t1.clientX) / 2,
  y: (t0.clientY + t1.clientY) / 2,
});

// The pinch span.
export const touchDist = (t0, t1) => dist(t0.clientX, t0.clientY, t1.clientX, t1.clientY);

// 'tap' (short and near-stationary) | 'drag' (anything else, incl. a hold the host handled).
export const classifyEnd = ({ moved, elapsed }, opts = {}) => {
  const { moveTol, tapMaxMs } = { ...TOUCH_DEFAULTS, ...opts };
  return moved <= moveTol && elapsed <= tapMaxMs ? 'tap' : 'drag';
};

export const isLongPress = ({ moved, elapsed }, opts = {}) => {
  const { moveTol, longPressMs } = { ...TOUCH_DEFAULTS, ...opts };
  return moved <= moveTol && elapsed >= longPressMs;
};
