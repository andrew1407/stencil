// The pointer gesture in flight, one of GESTURES: the seven exclusive drag and pan flags as one
// state, so two can never both be set and "is the pointer busy" is a single question.
export const GESTURES = Object.freeze(['none', 'pan', 'point', 'segment', 'line', 'zoomRect', 'rectDraw', 'compareSplit']);

// The flag each reader still says, and the gesture it names.
export const GESTURE_FLAGS = Object.freeze({
  isPanning: 'pan', isDraggingPoint: 'point', isDraggingSegment: 'segment', isDraggingLine: 'line',
  isZoomRectDragging: 'zoomRect', isRectDrawDragging: 'rectDraw', isDraggingCompareSplit: 'compareSplit',
});

// The flags as accessors over `gesture`: raising one ends any other, lowering it ends only itself.
export const installGestureFlags = (proto) => {
  for (const [flag, g] of Object.entries(GESTURE_FLAGS)) {
    Object.defineProperty(proto, flag, {
      get() { return this.gesture === g; },
      set(on) { if (on) this.gesture = g; else if (this.gesture === g) this.gesture = 'none'; },
      configurable: true,
    });
  }
};

// Read through the flags, so a host that keeps plain booleans (a test rig) answers the same.
export const activeGesture = (app) => {
  for (const [flag, g] of Object.entries(GESTURE_FLAGS)) if (app[flag]) return g;
  return 'none';
};

// A drag or pan of the canvas content — any gesture but the compare divider. Hover, the
// tooltip and hold-to-draw stand down while one runs.
export const canvasGestureActive = (app) => {
  const g = activeGesture(app);
  return g !== 'none' && g !== 'compareSplit';
};
