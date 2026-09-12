// The imageWorker client and its inline fallback: downscale, thumbnail and contour renders go
// off-thread with transferable bitmaps/buffers, or run the same imageRaster.js sequence inline.

export interface DownscaleOptions {
  maxEdge: number;
  /** Encoder MIME; default 'image/png'. */
  type?: string;
  quality?: number;
  /** Step down by 2× before the final draw — the thumbnail path. */
  halve?: boolean;
}

/** Workers + OffscreenCanvas + createImageBitmap present, and the worker has not failed. */
export declare const imageWorkerUsable: () => boolean;

/** Synchronous, on a document canvas — what a caller that cannot wait uses. */
export declare const downscaleInline: (source: CanvasImageSource, sw: number, sh: number, opts: DownscaleOptions) => string;

/** Worker first (the worker gets a bitmap COPY, so `source` stays drawable), inline on any failure. */
export declare const downscaleToDataUrl: (source: CanvasImageSource, sw: number, sh: number, opts: DownscaleOptions) => Promise<string>;

/** `readPixels` yields a fresh ImageData per attempt: the worker copy is transferred (detached). */
export declare const contourToDataUrl: (readPixels: () => ImageData, type?: string) => Promise<string>;
