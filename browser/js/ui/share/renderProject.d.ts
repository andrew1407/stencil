// A project that is not open, drawn off-screen as the editor draws it.

/** A canvas (on-screen or Offscreen) encoded as a PNG; rejects when the encode yields nothing. */
export declare const pngOf: (canvas: HTMLCanvasElement | OffscreenCanvas) => Promise<Blob>;
/** The original turned, mirrored and cropped by the layout; no crop is the whole turned picture. */
export declare const orientedCrop: (img: CanvasImageSource & { width: number; height: number }, layout?: Record<string, unknown>) => HTMLCanvasElement;
/** 'current' (filter and lines), 'tint' (filter only) or 'original' (neither), as a PNG. */
export declare const renderProject: (blob: Blob, layout?: Record<string, unknown>, variant?: 'current' | 'tint' | 'original',
  look?: { showLines?: boolean; showPoints?: boolean; pointSize?: number }) => Promise<Blob>;
