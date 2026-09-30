// One-finger pan: a press on empty canvas that wanders past the tap tolerance scrolls the
// viewport under the finger 1:1, the touch twin of the mouse's Alt+drag (pointer/controller.js).
// The pan is the viewport's own scroll offset; `isPanning` only stands hover and hold down.

// From the press point, so the tolerance the finger wandered first is scrolled too.
export const beginTouchPan = (app, st) => {
  st.mode = 'pan';
  st.lastX = st.startX;
  st.lastY = st.startY;
  app.isPanning = true;
};

export const touchPanTo = (viewport, st, t) => {
  viewport.scrollLeft -= t.clientX - st.lastX;
  viewport.scrollTop -= t.clientY - st.lastY;
  st.lastX = t.clientX;
  st.lastY = t.clientY;
};

// The mouse release (pointer/release.js) never runs for a finger, so the flag drops here.
export const endTouchPan = (app) => { app.isPanning = false; };
