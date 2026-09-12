export declare const DEFAULT_MAX_EDGE: number;
export declare const DEFAULT_RASTER_EDGE: number;
export declare const DECODE_ERROR: string;

export declare function isSvgType(type: string): boolean;
export declare function isSvgUrl(url: string): boolean;
export declare function mediaTypeOf(url: string): string;
export declare function fitSize(w: number, h: number, maxEdge?: number): { width: number; height: number };

export interface RasterSizeInput {
  width?: number; height?: number; naturalWidth?: number; naturalHeight?: number;
  maxEdge?: number; fallbackEdge?: number; minEdge?: number;
}
export declare function rasterSize(input?: RasterSizeInput): { width: number; height: number };

export interface RasterDeps {
  createBitmap?: ((blob: Blob) => Promise<ImageBitmap>) | null;
  makeImage?: () => HTMLImageElement;
  makeCanvas?: () => HTMLCanvasElement;
  toBlob?: (url: string) => Promise<Blob>;
  objectUrl?: (blob: Blob) => string;
  revokeUrl?: (url: string) => void;
  timer?: (fn: () => void, ms: number) => unknown;
  clearTimer?: (id: unknown) => void;
}

export interface RasterSource { dataUrl?: string; blob?: Blob | null; width?: number; height?: number; }
export interface RasterizeOptions { maxEdge?: number; fallbackEdge?: number; timeoutMs?: number; deps?: RasterDeps; }

export declare function rasterizeToPngDataUrl(source?: RasterSource, opts?: RasterizeOptions): Promise<string>;
export declare function decodeSize(
  source?: RasterSource, opts?: { timeoutMs?: number; deps?: RasterDeps },
): Promise<{ width: number; height: number }>;
