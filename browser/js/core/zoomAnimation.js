import { canvasOrigin } from '../utils/viewportMetrics.js';
import { setZoomInputValue } from '../utils/zoomOverlay.js';

// ── The two zooms that move the scroll offset with the scale ─────
// Split out of ZoomPan (zoomPan.js), which keeps the plain setZoom/fit. Both take the
// ZoomPan as `zp` so they keep reading app state and the shared clamp through it.

// this.app.renderedScale tracks the scale that is actually on screen right now;
// this.app.scale is kept in sync each frame so rapid clicks start from the
// correct visual position rather than the stale logical target.
export const zoomAroundCenter = (zp, newScale) => {
  const app = zp.app;
  if (!app.image) return;
  const vp = document.getElementById('canvas-viewport');
  if (!vp) {
    zp.setZoom(newScale);
    return;
  }
  newScale = zp.clampScale(newScale);
  // No viewport resize here any more: the frame is full-height at every zoom, so the
  // clientHeight the centring math reads below is already the one the zoom lands in.

  // Cancel any in-flight animation; start from whatever is on screen NOW
  if (app.zoomAnimRaf) {
    cancelAnimationFrame(app.zoomAnimRaf);
    app.zoomAnimRaf = null;
  }

  // scaleStart = what the canvas actually looks like right now
  const scaleStart = (app.renderedScale != null) ? app.renderedScale : app.scale;
  const scrollX0 = vp.scrollLeft;
  const scrollY0 = vp.scrollTop;

  // Image-space point at current viewport center (use scaleStart, not app.scale).
  const o0 = canvasOrigin();
  const imgCx = (scrollX0 + vp.clientWidth / 2 - o0.x) / scaleStart;
  const imgCy = (scrollY0 + vp.clientHeight / 2 - o0.y) / scaleStart;

  // Predicted centring margin at the target zoom (0 once the image overflows), so a zoom
  // that ends up smaller than the frame targets 0 exactly instead of a clamped guess.
  const o1 = zp.originAt(newScale);
  const scrollX1 = imgCx * newScale - vp.clientWidth / 2 + o1.x;
  const scrollY1 = imgCy * newScale - vp.clientHeight / 2 + o1.y;

  // Suppress CSS transition — we control every frame ourselves
  app.canvas.classList.add('zoom-no-transition');

  const DURATION = 200; // ms
  const ease = t => 1 - (1 - t) * (1 - t); // ease-out-quad

  const t0 = performance.now();
  const tick = now => {
    const p = Math.min((now - t0) / DURATION, 1);
    const e = ease(p);

    // Interpolated scale — keep app.scale in sync so hit-tests are correct
    const s = scaleStart + (newScale - scaleStart) * e;
    app.renderedScale = s;
    app.scale = s;

    // Write canvas CSS size first — scrollWidth grows with it
    app.canvas.style.width = (app.canvas.width * s) + 'px';
    app.canvas.style.height = (app.canvas.height * s) + 'px';

    // Write scroll — never clamped because scrollWidth just grew
    vp.scrollLeft = scrollX0 + (scrollX1 - scrollX0) * e;
    vp.scrollTop = scrollY0 + (scrollY1 - scrollY0) * e;

    setZoomInputValue(Math.round(s * 100));

    if (p < 1) {
      app.zoomAnimRaf = requestAnimationFrame(tick);
    } else {
      // Snap to exact final values
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

// Zoom to `newScale` keeping the image-space point (imgX, imgY) pinned under its
// current on-screen position (focal zoom). Non-animated; used by the console API's
// stencil.zoom(amount, { x, y }). Falls back to plain setZoom with no viewport.
export const zoomToImagePoint = (zp, newScale, imgX, imgY) => {
  const app = zp.app;
  if (!app.image) return;
  newScale = zp.clampScale(newScale);
  const vp = document.getElementById('canvas-viewport');
  if (!vp) { zp.setZoom(newScale); return; }
  if (app.zoomAnimRaf) { cancelAnimationFrame(app.zoomAnimRaf); app.zoomAnimRaf = null; }
  const scaleStart = (app.renderedScale != null) ? app.renderedScale : app.scale;
  // On-screen offset (within the viewport) of the focal point right now — through the
  // centring margins, which move the image origin off the scroll origin.
  const o0 = canvasOrigin();
  const offX = imgX * scaleStart + o0.x - vp.scrollLeft;
  const offY = imgY * scaleStart + o0.y - vp.scrollTop;
  // The canvas CSS size TRANSITIONS (layout.css), so mid-flight the scroll range is
  // still the old one and the browser clamps the write below — the focal point slid.
  // Suppress it for this step (the animated zoom does the same), and let it back on a
  // frame later, once the new size is settled and nothing is left to animate.
  app.canvas.classList.add('zoom-no-transition');
  zp.setZoom(newScale);          // updates scale + canvas CSS size + zoom input + persist
  app.renderedScale = newScale;
  const o1 = canvasOrigin();     // re-measured (and flushes the new size into layout)
  vp.scrollLeft = imgX * newScale + o1.x - offX;
  vp.scrollTop = imgY * newScale + o1.y - offY;
  requestAnimationFrame(() => app.canvas.classList.remove('zoom-no-transition'));
};
