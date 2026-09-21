import { canvasOrigin } from '../../utils/viewportMetrics.js';
import { setZoomInputValue } from '../../utils/zoomOverlay.js';

// The two zooms that move the scroll offset with the scale; `zp` is the ZoomPan.

// app.renderedScale is the scale actually on screen; app.scale is kept in sync each frame
// so rapid clicks start from the visual position, not the stale logical target.
export const zoomAroundCenter = (zp, newScale) => {
  const app = zp.app;
  if (!app.image) return;
  const vp = document.getElementById('canvas-viewport');
  if (!vp) {
    zp.setZoom(newScale);
    return;
  }
  newScale = zp.clampScale(newScale);
  // Cancel any in-flight animation; start from whatever is on screen NOW.
  if (app.zoomAnimRaf) {
    cancelAnimationFrame(app.zoomAnimRaf);
    app.zoomAnimRaf = null;
  }

  const scaleStart = (app.renderedScale != null) ? app.renderedScale : app.scale;
  const scrollX0 = vp.scrollLeft;
  const scrollY0 = vp.scrollTop;

  // Image-space point at the current viewport centre (scaleStart, not app.scale).
  const o0 = canvasOrigin();
  const imgCx = (scrollX0 + vp.clientWidth / 2 - o0.x) / scaleStart;
  const imgCy = (scrollY0 + vp.clientHeight / 2 - o0.y) / scaleStart;

  // Predicted centring margin at the target zoom (0 once the image overflows).
  const o1 = zp.originAt(newScale);
  const scrollX1 = imgCx * newScale - vp.clientWidth / 2 + o1.x;
  const scrollY1 = imgCy * newScale - vp.clientHeight / 2 + o1.y;

  app.canvas.classList.add('zoom-no-transition');

  const DURATION = 200;
  const ease = t => 1 - (1 - t) * (1 - t); // ease-out-quad

  const t0 = performance.now();
  const tick = now => {
    const p = Math.min((now - t0) / DURATION, 1);
    const e = ease(p);

    // app.scale in sync so hit-tests are correct.
    const s = scaleStart + (newScale - scaleStart) * e;
    app.renderedScale = s;
    app.scale = s;

    // Canvas CSS size first — scrollWidth grows with it, so the scroll write is never clamped.
    app.canvas.style.width = (app.canvas.width * s) + 'px';
    app.canvas.style.height = (app.canvas.height * s) + 'px';

    vp.scrollLeft = scrollX0 + (scrollX1 - scrollX0) * e;
    vp.scrollTop = scrollY0 + (scrollY1 - scrollY0) * e;

    setZoomInputValue(Math.round(s * 100));

    if (p < 1) {
      app.zoomAnimRaf = requestAnimationFrame(tick);
    } else {
      app.scale = newScale;
      app.renderedScale = newScale;
      app.canvas.style.width = (app.canvas.width * newScale) + 'px';
      app.canvas.style.height = (app.canvas.height * newScale) + 'px';
      vp.scrollLeft = scrollX1;
      vp.scrollTop = scrollY1;
      setZoomInputValue(Math.round(newScale * 100));
      app.canvas.classList.remove('zoom-no-transition');
      app.zoomAnimRaf = null;
      if (app.image) zp.persistZoom();
    }
  };
  app.zoomAnimRaf = requestAnimationFrame(tick);
};

// Focal zoom: the image-space point (imgX, imgY) stays under its on-screen position.
// Non-animated (the console API's stencil.zoom(amount, { x, y })).
export const zoomToImagePoint = (zp, newScale, imgX, imgY) => {
  const app = zp.app;
  if (!app.image) return;
  newScale = zp.clampScale(newScale);
  const vp = document.getElementById('canvas-viewport');
  if (!vp) { zp.setZoom(newScale); return; }
  if (app.zoomAnimRaf) { cancelAnimationFrame(app.zoomAnimRaf); app.zoomAnimRaf = null; }
  const scaleStart = (app.renderedScale != null) ? app.renderedScale : app.scale;
  // On-screen offset of the focal point, through the centring margins.
  const o0 = canvasOrigin();
  const offX = imgX * scaleStart + o0.x - vp.scrollLeft;
  const offY = imgY * scaleStart + o0.y - vp.scrollTop;
  // The canvas CSS size TRANSITIONS (layout/canvasCursor.css): mid-flight the browser would
  // clamp the scroll write to the old range and the focal point slides. Suppressed for this step.
  app.canvas.classList.add('zoom-no-transition');
  zp.setZoom(newScale);
  app.renderedScale = newScale;
  const o1 = canvasOrigin();
  vp.scrollLeft = imgX * newScale + o1.x - offX;
  vp.scrollTop = imgY * newScale + o1.y - offY;
  requestAnimationFrame(() => app.canvas.classList.remove('zoom-no-transition'));
};
