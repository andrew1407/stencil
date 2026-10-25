// Shape of marksCache.js — the overlay's lines and points as a reusable bitmap.

export class MarksCache {
  /** The values the marks are painted from, beside the per-line fields and points the cache walks itself. */
  static scalars(app: object, readOnly: boolean, w: number, h: number): unknown[];
  /** Paints the marks onto an overlay context already cleared and scaled to w×h. */
  draw(app: object, target: CanvasRenderingContext2D, readOnly: boolean, w: number, h: number,
    paint: (ctx: CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D) => void): void;
}
