// The app's whole zoom surface: zoom level, fit, hold-zoom and the overlays. Measuring
// and sizing live in utils/viewportMetrics.js, the marquee and zoom inputs in
// utils/zoomOverlay.js, the two scroll-moving zooms in animation.js.
import type { DrawingApp } from '../drawingApp.js';

/** The centring-margin term every viewport→image conversion needs (utils/viewportMetrics.js). */
export declare const canvasOrigin: () => { x: number; y: number };

/** The smallest zoom scale (0.05); core-bound (wasm) with the JS fallback as the reference. */
export declare const zoomMin: () => number;
/** The largest zoom scale (32); core-bound (wasm) with the JS fallback as the reference. */
export declare const zoomMax: () => number;
/** The scale and scroll that fit a swept image-space rect to the viewport, centred, capped at zoomMax(). */
export declare const rectZoom: (x1: number, y1: number, rectW: number, rectH: number, availW: number, availH: number) =>
  { scale: number; scrollLeft: number; scrollTop: number };
/** The zoom range in whole percents, as the zoom input takes it (5–3200). */
export declare const zoomPercentBounds: () => { min: number; max: number };

/** Quiet time after the LAST zoom step before the session is persisted. */
export declare const ZOOM_SAVE_DEBOUNCE_MS: number;

/** Trailing-edge debounce: only the last call in a burst runs `run`, after `delay` of quiet. */
export interface TrailingSave {
  (): void;
  flush(): void;
  pending(): boolean;
}
export declare const createTrailingSave: (run: () => void, opts?: {
  delay?: number;
  setTimer?: (fn: () => void, ms: number) => unknown;
  clearTimer?: (id: unknown) => void;
}) => TrailingSave;

export declare class ZoomPan {
  constructor(app: DrawingApp);
  app: DrawingApp;
  /** The one debounced persistence path for every zoom route. */
  persistZoom: TrailingSave;
  /** Into [zoomMin(), zoomMax()]; core-bound (wasm) with the JS fallback as the reference. */
  clampScale: (s: number) => number;
  updateZoomRectOverlay(): void;
  hideZoomRectOverlay(): void;
  updateRectDrawOverlay(): void;
  setZoomInputValue(percent: number): void;
  availContentHeight(): number;
  availContentWidth(): number;
  viewportChromeY(): number;
  originAt(scale: number): { x: number; y: number };
  syncViewportHeight(): void;
  syncCoordPanelHeight(): void;
  zoomAroundCenter(newScale: number): void;
  zoomToImagePoint(newScale: number, imgX: number, imgY: number): void;
  /** No image → ignored. `persist` false skips the debounced save. */
  setZoom(newScale: number, persist?: boolean): void;
  /** The zoom and the image point under the viewport's centre; null without an image. */
  viewAnchor(): ViewAnchor | null;
  /** Back to the anchor's zoom with its point centred; a fitted (or missing) anchor refits. */
  restoreAnchor(anchor: ViewAnchor | null): void;
  /** Never upscales past 100%; rounds DOWN to the 1% the zoom input shows. */
  fitToWindow(): void;
}

export interface ViewAnchor {
  scale: number;
  /** The picture sat at its fit scale. */
  fit: boolean;
  /** Image-space px at the viewport's centre. */
  x: number;
  y: number;
}
