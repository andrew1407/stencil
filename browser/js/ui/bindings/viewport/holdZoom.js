import { setZoomInputValue } from '../../../utils/zoomOverlay.js';
import { zoomAroundCenter } from '../../../core/zoom/animation.js';
import { zoomStepAt, ZOOM_PRESS_STEP, ZOOM_PRESS_STEP_BIG, ZOOM_HOLD_STEP } from '../../../core/zoom/pan.js';

const DBL_WINDOW = 280;
const HOLD_DELAY = 480;
// The held rate, which a fit drag over ± (ui/drag/zoomDrag.js) runs at too.
export const HOLD_STEP = ZOOM_HOLD_STEP;
export const HOLD_REPEAT_MS = 90;

// One continuous step about the viewport centre; sign is +1 zoom-in, −1 zoom-out.
export const holdStep = (zp, sign) => {
  const t = zp.clampScale(zp.app.scale + sign * zoomStepAt(zp.app.scale, HOLD_STEP));
  setZoomInputValue(Math.round(t * 100));
  zoomAroundCenter(zp, t);
};

// Press-and-hold zoom for the +/− buttons. sign is +1 zoom-in, −1 zoom-out.
// Single press → small step; double-press → large step; hold → continuous zoom.
// The handle stops the hold and reports the view the last press started from.
export function setupHoldZoom(zp, btn, sign) {
  let holdTimer = null;
  let repeatTimer = null;
  let lastPress = 0;
  let before = null;
  const stop = () => {
    clearTimeout(holdTimer);
    holdTimer = null;
    clearInterval(repeatTimer);
    repeatTimer = null;
  };
  btn.addEventListener('mousedown', e => {
    if (e.button !== 0) return;
    e.preventDefault();
    before = zp.viewAnchor?.() ?? null;
    const now = performance.now();
    const isDouble = (now - lastPress) < DBL_WINDOW;
    lastPress = isDouble ? 0 : now;
    // The core's press steps, alike on the desktop; a double press tops the first up to the big one.
    const step = isDouble ? (ZOOM_PRESS_STEP_BIG - ZOOM_PRESS_STEP) : ZOOM_PRESS_STEP;
    const target = zp.clampScale(zp.app.scale + sign * zoomStepAt(zp.app.scale, step));
    // Update the zoom % input synchronously so users see immediate feedback
    setZoomInputValue(Math.round(target * 100));
    zoomAroundCenter(zp, target);
    holdTimer = setTimeout(() => {
      repeatTimer = setInterval(() => holdStep(zp, sign), HOLD_REPEAT_MS);
    }, HOLD_DELAY);
  });
  btn.addEventListener('mouseup', stop);
  btn.addEventListener('mouseleave', stop);
  // If the mouse is released anywhere in the window, also stop
  window.addEventListener('mouseup', stop);
  return { stop, pressAnchor: () => before };
}
