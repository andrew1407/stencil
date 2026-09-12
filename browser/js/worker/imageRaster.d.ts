// Pixel work shared by the image worker and its inline fallback: pure over a canvas factory,
// so one drawImage sequence runs on an OffscreenCanvas off-thread and a document canvas inline.

/** Any canvas the factory hands back: an OffscreenCanvas in the worker, an HTMLCanvasElement inline. */
export type AnyCanvas = HTMLCanvasElement | OffscreenCanvas;
export type CanvasFactory = (width: number, height: number) => AnyCanvas;

/** ≤ maxEdge px on the long edge, never upscaled, at least 1px a side. */
export declare const fitSize: (width: number, height: number, maxEdge: number) => { width: number; height: number };

/** Draw `source` (sw×sh) onto a fresh width×height canvas; `halve` steps down by 2× first (the thumbnail path). */
export declare const paintScaled: (
  makeCanvas: CanvasFactory, source: CanvasImageSource, sw: number, sh: number,
  opts: { width: number; height: number; halve?: boolean },
) => AnyCanvas;

/** Run the core contour pass over `imageData` in place and paint it onto a fresh canvas. */
export declare const contourCanvas: (
  makeCanvas: CanvasFactory, imageData: ImageData,
  contour: (data: Uint8ClampedArray, width: number, height: number) => void,
) => AnyCanvas;
