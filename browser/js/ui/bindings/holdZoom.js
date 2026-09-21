import { setZoomInputValue } from '../../utils/zoomOverlay.js';
import { zoomAroundCenter } from '../../core/zoom/zoomAnimation.js';

// Press-and-hold zoom for the +/− buttons. sign is +1 zoom-in, −1 zoom-out.
// Single press → small step; double-press → large step; hold → continuous zoom.
export function setupHoldZoom(zp, btn, sign) {
  // Smaller, gentler steps so a single click feels like one notch, not a leap.
  const SMALL = 0.10;
  const LARGE = 0.40;
  const CONT = 0.05;
  const DBL_WINDOW = 280;
  const HOLD_DELAY = 480;
  const REPEAT_MS = 90;
  let holdTimer = null;
  let repeatTimer = null;
  let lastPress = 0;
  const stop = () => {
    clearTimeout(holdTimer);
    holdTimer = null;
    clearInterval(repeatTimer);
    repeatTimer = null;
  };
  btn.addEventListener('mousedown', e => {
    if (e.button !== 0) return;
    e.preventDefault();
    const now = performance.now();
    const isDouble = (now - lastPress) < DBL_WINDOW;
    lastPress = isDouble ? 0 : now;
    // On double-click, top up the prior SMALL step to reach LARGE in total
    const step = isDouble ? (LARGE - SMALL) : SMALL;
    const target = zp.clampScale(zp.app.scale + sign * step);
    // Update the zoom % input synchronously so users see immediate feedback
    setZoomInputValue(Math.round(target * 100));
    zoomAroundCenter(zp, target);
    holdTimer = setTimeout(() => {
      repeatTimer = setInterval(() => {
        const t = zp.clampScale(zp.app.scale + sign * CONT);
        setZoomInputValue(Math.round(t * 100));
        zoomAroundCenter(zp, t);
      }, REPEAT_MS);
    }, HOLD_DELAY);
  });
  btn.addEventListener('mouseup', stop);
  btn.addEventListener('mouseleave', stop);
  // If the mouse is released anywhere in the window, also stop
  window.addEventListener('mouseup', stop);
}
