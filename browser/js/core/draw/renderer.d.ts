// Per-frame composition over two layers: the picture on #canvas, repainted only when it
// changes, and the lines, points, compare divider and hold-to-draw preview on the overlay above
// it. The base lives in baseLayer.js, one line or point in render.js — this owns the order.
import type { DrawingApp } from '../drawingApp.js';
import type { CodecLine } from '../line/linesCodec.js';

export { pointColorOf } from '../line/render.js';

export type CompareMode = 'none' | 'original' | 'vertical' | 'horizontal';

export declare class Renderer {
  constructor(app: DrawingApp);
  app: DrawingApp;
  /** Set per frame: true suppresses selection glow + hover/focus rings (read-only compare views). */
  suppressHighlight: boolean;
  /** Where a line or point paints: the overlay mid-frame, else `app.ctx` (an export swaps it). */
  readonly ctx: CanvasRenderingContext2D;
  /** Stack the view's lines canvas over #canvas; the picture then repaints only when it changes. */
  useOverlay(canvas: HTMLCanvasElement | null): void;
  /** A blank recolour on trial: the picture paints as a solid `color` (filtered as the base is) until the image changes; null ends it. */
  previewFill(color: string | null): void;
  /** The stage's canvases, bottom first — what a reader of its pixels composites. */
  layers(): HTMLCanvasElement[];
  /** The filtered base as export needs it: built now, on this thread; the image itself when unfiltered. */
  restingBase(): CanvasImageSource;
  /** The filtered base as export needs it: built now, on this thread. */
  drawImageWithFilter(ctx: CanvasRenderingContext2D): void;
  /** An Alt+Shift+O hold forces 'original' regardless of the selected mode. */
  effectiveCompareMode(): CompareMode;
  redraw(): void;
  /** redraw() coalesced to one call per animation frame (straight through without rAF). */
  requestRedraw(): void;
  /** The original side, then (unless `withDivider: false`) the divider, into `ctx`. */
  drawCompareSplit(mode: 'vertical' | 'horizontal', opts?: { withDivider?: boolean }): void;
  drawHoldPreview(): void;
  drawLine(line: CodecLine, isSelected?: boolean, lineIdx?: number): void;
  drawPoint(point: { x: number; y: number }, color: string, pointSize?: number, isSelected?: boolean, highlightState?: 0 | 1 | 2): void;
  /** 0 none, 1 hover, 2 focused — regardless of which line the coord table shows. */
  pointHighlightState(lineIdx: number, ptIdx: number): 0 | 1 | 2;
}
