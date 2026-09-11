import { core } from './stencilCore.js';
import { zoomAroundCenter, zoomToImagePoint } from './zoomAnimation.js';
import {
  canvasOrigin, availContentHeight, availContentWidth, viewportChromeY,
  originAt, syncViewportHeight, syncCoordPanelHeight,
} from '../utils/viewportMetrics.js';
import {
  updateZoomRectOverlay, hideZoomRectOverlay, updateRectDrawOverlay, setZoomInputValue,
} from '../utils/zoomOverlay.js';

// ── ZoomPan: zoom level, fit, hold-zoom, overlays ───────────────
// The zoom LOGIC only. Everything that queries or writes the page lives in
// utils/viewportMetrics.js (measuring + sizing) and utils/zoomOverlay.js (the marquee and
// the zoom-percent inputs); the two scroll-moving zooms are in zoomAnimation.js. The
// methods below stay because they are the app's whole zoom surface — the toolbar, the
// context menu, fullscreen and the console facade all reach zoom through app.zoomPan.

// canvasOrigin() is re-exported: it is the centring-margin term every viewport→image
// conversion needs, and callers already know it as part of the zoom surface.
export { canvasOrigin };

// How long after the LAST zoom step the session is persisted. Trailing-edge: a
// wheel/hold burst writes the final zoom once — one save, one "Saved" toast — instead
// of a full layout + thumbnail write per notch.
export const ZOOM_SAVE_DEBOUNCE_MS = 400;

// Trailing-edge debounce, timers injectable so the collapse is unit-testable. Only the
// last call in a burst runs `run`, after `delay` of quiet; `flush()` runs a pending
// save NOW, `pending()` answers whether one is armed.
export const createTrailingSave = (run, { delay = ZOOM_SAVE_DEBOUNCE_MS,
                                          setTimer = setTimeout, clearTimer = clearTimeout } = {}) => {
  let timer = null;
  const fire = () => { timer = null; run(); };
  const call = () => {
    if (timer !== null) clearTimer(timer);
    timer = setTimer(fire, delay);
  };
  call.flush = () => { if (timer === null) return; clearTimer(timer); fire(); };
  call.pending = () => timer !== null;
  return call;
};

export class ZoomPan {
  constructor(app) {
    this.app = app;
    // The single debounced persistence path for every zoom route (wheel/hold steps via
    // setZoom, the animated zoom's final snap): a burst saves once, at its end.
    this.persistZoom = createTrailingSave(() => { if (this.app.image) this.app.storage.save(); });
  }

  // Clamp a scale into the zoom limits. Delegates to the shared C++ core (wasm)
  // clampScale when loaded; the JS bound is the reference + fallback.
  clampScale = core.bind('clampScale', s => Math.max(0.05, Math.min(32, s)));

  // ── The view half (utils/zoomOverlay.js, utils/viewportMetrics.js) ──
  updateZoomRectOverlay() { updateZoomRectOverlay(this.app.zoomRectStart, this.app.zoomRectEnd); }
  hideZoomRectOverlay() { hideZoomRectOverlay(); }
  updateRectDrawOverlay() { updateRectDrawOverlay(this.app.rectDrawStart, this.app.rectDrawEnd); }
  setZoomInputValue(percent) { setZoomInputValue(percent); }
  availContentHeight() { return availContentHeight(); }
  availContentWidth() { return availContentWidth(); }
  viewportChromeY() { return viewportChromeY(); }
  originAt(scale) { return originAt(this.app.canvas, scale); }
  syncViewportHeight() { syncViewportHeight(); }
  syncCoordPanelHeight() { syncCoordPanelHeight(); }

  // ── The scroll-moving zooms (zoomAnimation.js) ──
  zoomAroundCenter(newScale) { zoomAroundCenter(this, newScale); }
  zoomToImagePoint(newScale, imgX, imgY) { zoomToImagePoint(this, newScale, imgX, imgY); }

  setZoom(newScale, persist = true) {
    // No image → there's nothing to scale; ignore zoom requests entirely.
    if (!this.app.image) return;
    newScale = this.clampScale(newScale);
    this.app.scale = newScale;
    // This IS what goes on screen, so the on-screen tracker moves with it: left stale from
    // an older animated zoom, the next focal zoom measures its start from a scale the
    // canvas no longer has and lands somewhere else entirely.
    this.app.renderedScale = newScale;
    this.app.canvas.style.width = (this.app.canvas.width * newScale) + 'px';
    this.app.canvas.style.height = (this.app.canvas.height * newScale) + 'px';
    // Not for the zoom (the frame is full-height at every scale) but for the room: a toolbar
    // that reflowed moves the viewport's top, and this is the cheapest place to catch it.
    this.syncViewportHeight();
    this.setZoomInputValue(Math.round(newScale * 100));
    // Persist zoom level — debounced (createTrailingSave): a wheel/hold burst writes
    // once, at its end, instead of a full save + "Saved" toast per step. (Scroll is
    // saved via the debounced scroll listener on the same principle.)
    if (persist && this.app.image) this.persistZoom();
  }

  fitToWindow() {
    if (!this.app.image) return;
    // Fit against the box the image actually lands in — the SAME measurements
    // syncViewportHeight() sizes the viewport to; fixed insets over-estimate the room
    // and clip the fitted image. The height budget is border-box, so take the frame off.
    const availW = this.availContentWidth();
    const availH = Math.max(1, this.availContentHeight() - this.viewportChromeY());
    const scaleW = availW / this.app.image.width;
    const scaleH = availH / this.app.image.height;
    const fit = Math.min(scaleW, scaleH, 1); // never upscale beyond 100% on fit
    // Round DOWN to the same 1% the zoom input shows: rounding up re-introduces the
    // overflow this fit exists to avoid (619px at 0.7754 → 0.78 → 3px clipped).
    this.setZoom(Math.floor(fit * 100) / 100);

    // Viewport height is the available height either way (setZoom → syncViewportHeight, or
    // the fullscreen layer's own rule) — nothing to size here, just the scroll reset.
    const viewport = document.getElementById('canvas-viewport');
    if (viewport) {
      // Reset scroll to top-left on fit
      viewport.scrollLeft = 0;
      viewport.scrollTop = 0;
    }
  }
}
