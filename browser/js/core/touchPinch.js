// The two-finger pinch: the anchor taken at the start, the scale each move stashes, the one
// coalesced frame that writes it, and the end that hands the level back to ZoomPan.
import { midpoint, touchDist } from './touchGestures.js';
import { canvasOrigin } from './zoomPan.js';

export const beginPinch = (app, viewport, a, b) => {
  const mid = midpoint(a, b);
  const vpRect = viewport.getBoundingClientRect();
  // Minus the centring margins: a picture smaller than the frame does not start at the scroll origin.
  const org = canvasOrigin();
  const contentX = mid.x - vpRect.left + viewport.scrollLeft - org.x;
  const contentY = mid.y - vpRect.top + viewport.scrollTop - org.y;
  app.canvas.classList.add('zoom-no-transition');
  return {
    mode: 'pinch',
    startDist: touchDist(a, b) || 1,
    startScale: app.scale,
    imgX: contentX / app.scale,
    imgY: contentY / app.scale,
    vpLeft: vpRect.left, vpTop: vpRect.top,
    pending: null, raf: null,
  };
};

export const pinchTo = (app, st, a, b) => {
  const mid = midpoint(a, b);
  const factor = touchDist(a, b) / st.startDist;
  const newScale = app.zoomPan.clampScale(st.startScale * factor);
  st.pending = { scale: newScale, midX: mid.x, midY: mid.y };
};

export const applyPinchFrame = (app, viewport, st) => {
  const { scale, midX, midY } = st.pending;
  st.pending = null;
  st.raf = null;
  app.scale = scale;
  app.canvas.style.width = (app.canvas.width * scale) + 'px';
  app.canvas.style.height = (app.canvas.height * scale) + 'px';
  app.zoomPan.setZoomInputValue(Math.round(scale * 100));
  // Keep the pinched image point under the moving midpoint; vpLeft/vpTop are cached at pinch start.
  viewport.scrollLeft = st.imgX * scale - (midX - st.vpLeft);
  viewport.scrollTop = st.imgY * scale - (midY - st.vpTop);
};

// The pending frame lands first, so the last span the fingers held is the level persisted.
export const endPinch = (app, st, applyNow) => {
  if (st.raf) { cancelAnimationFrame(st.raf); st.raf = null; }
  if (st.pending) applyNow();
  app.canvas.classList.remove('zoom-no-transition');
  app.zoomPan.setZoom(app.scale, true);
};
