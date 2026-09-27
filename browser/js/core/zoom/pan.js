import { core } from '../abi/stencilCore.js';
import { zoomAroundCenter, zoomToImagePoint } from './animation.js';
import {
  canvasOrigin, availContentHeight, availContentWidth, viewportChromeY,
  originAt, syncViewportHeight, syncCoordPanelHeight,
} from '../../utils/viewportMetrics.js';
import {
  updateZoomRectOverlay, hideZoomRectOverlay, updateRectDrawOverlay, setZoomInputValue,
} from '../../utils/zoomOverlay.js';


// The centring-margin term every viewport→image conversion needs.
export { canvasOrigin };

// The zoom range as scales, 5%–3200%: the fallback twin of core/state/zoomPan.hpp
// ZOOM_MIN / ZOOM_MAX, which the wasm core reports itself once loaded.
const ZOOM_MIN = 0.05;
const ZOOM_MAX = 32;
export const zoomMin = core.bind('zoomMin', () => ZOOM_MIN);
export const zoomMax = core.bind('zoomMax', () => ZOOM_MAX);

// A swept image-space rect filling the availW x availH viewport, centred, capped at ZOOM_MAX:
// the fallback twin of core/state/zoomPan.cpp rectZoom.
export const rectZoom = (x1, y1, rectW, rectH, availW, availH) => {
  const scale = Math.max(ZOOM_MIN, Math.min(ZOOM_MAX, availW / rectW, availH / rectH));
  return {
    scale,
    scrollLeft: Math.max(0, x1 * scale - (availW - rectW * scale) / 2),
    scrollTop: Math.max(0, y1 * scale - (availH - rectH * scale) / 2),
  };
};

// The same range in the whole percents the zoom input takes.
export const zoomPercentBounds = () => ({ min: Math.round(zoomMin() * 100), max: Math.round(zoomMax() * 100) });

// Trailing-edge: a wheel/hold burst writes the final zoom once instead of a full layout +
// thumbnail write per notch.
export const ZOOM_SAVE_DEBOUNCE_MS = 400;

// Only the last call in a burst runs `run`, after `delay` of quiet; `flush()` runs a pending
// save NOW, `pending()` answers whether one is armed. Timers injectable.
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
    // The single debounced persistence path for every zoom route: a burst saves once, at its end.
    this.persistZoom = createTrailingSave(() => { if (this.app.image) this.app.storage.save(); });
  }

  // The shared C++ core (wasm) clampScale when loaded; the JS bound is the reference + fallback.
  clampScale = core.bind('clampScale', s => Math.max(ZOOM_MIN, Math.min(ZOOM_MAX, s)));

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

  zoomAroundCenter(newScale) { zoomAroundCenter(this, newScale); }
  zoomToImagePoint(newScale, imgX, imgY) { zoomToImagePoint(this, newScale, imgX, imgY); }

  setZoom(newScale, persist = true) {
    if (!this.app.image) return;
    newScale = this.clampScale(newScale);
    this.app.scale = newScale;
    // The on-screen tracker moves with it, or the next focal zoom measures its start from a
    // scale the canvas no longer has.
    this.app.renderedScale = newScale;
    this.app.canvas.style.width = (this.app.canvas.width * newScale) + 'px';
    this.app.canvas.style.height = (this.app.canvas.height * newScale) + 'px';
    // Not for the zoom but for the room: a reflowed toolbar moves the viewport's top.
    this.syncViewportHeight();
    this.setZoomInputValue(Math.round(newScale * 100));
    // Debounced (createTrailingSave): a burst writes once, at its end.
    if (persist && this.app.image) this.persistZoom();
  }

  // The scale that fits the picture in the frame's content box right now, off the SAME
  // measurements syncViewportHeight() sizes the viewport to (fixed insets clip the fit).
  fitScale() {
    const availW = this.availContentWidth();
    const availH = Math.max(1, this.availContentHeight() - this.viewportChromeY());
    // A small picture is magnified to fill the frame, as the desktop's fit does.
    const fit = this.clampScale(Math.min(availW / this.app.image.width, availH / this.app.image.height));
    // Round DOWN to the 1% the zoom input shows: rounding up re-introduces the overflow
    // (619px at 0.7754 → 0.78 → 3px clipped).
    return Math.floor(fit * 100) / 100;
  }

  fitToWindow() {
    if (!this.app.image) return;
    this.setZoom(this.fitScale());

    const viewport = document.getElementById('canvas-viewport');
    if (viewport) {
      viewport.scrollLeft = 0;
      viewport.scrollTop = 0;
    }
    // A first picture reflows the toolbar AFTER its load fits it (settle.js, then updateInfo),
    // so the room is read again next frame and the fit follows it while nothing else moved the zoom.
    if (typeof requestAnimationFrame !== 'function') return;
    const applied = this.app.scale;
    requestAnimationFrame(() => {
      if (this.app.image && this.app.scale === applied && this.fitScale() !== applied) this.fitToWindow();
    });
  }
}
