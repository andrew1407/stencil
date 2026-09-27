// The imageWorker client and its inline fallback: downscale, thumbnail, contour and filter renders
// go off-thread with transferable bitmaps/buffers, or run the same imageRaster.js sequence inline.
import type { FilterInput } from '../core/image/filterCanvas.js';

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

/** The filterPixels.js pass off-thread (the pixels' buffer is transferred); rejects when the worker cannot. */
export declare const filterInWorker: (input: FilterInput) => Promise<ImageBitmap>;

/** The co-edit result of a snapshotted resting paint as PNG bytes: in the worker, else inline; null if not encoded. */
export declare const resultPngBytes: (job: import('../core/draw/restingPaint.js').RestingJob) => Promise<Uint8Array | null>;
