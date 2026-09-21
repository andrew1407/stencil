// Per-frame composition: the filtered image, the lines and points, the compare split and
// the hold-to-draw preview. The filter chain lives in filterCanvas.js, one line or
// point in render.js — this owns the order they paint in.
import type { DrawingApp } from '../drawingApp.js';
import type { CodecLine } from '../line/linesCodec.js';

export { pointColorOf } from '../line/render.js';

export type CompareMode = 'none' | 'original' | 'vertical' | 'horizontal';

export declare class Renderer {
  constructor(app: DrawingApp);
  app: DrawingApp;
  /** Set per frame: true suppresses selection glow + hover/focus rings (read-only compare views). */
  suppressHighlight: boolean;
  drawImageWithFilter(ctx: CanvasRenderingContext2D): void;
  /** An Alt+Shift+O hold forces 'original' regardless of the selected mode. */
  effectiveCompareMode(): CompareMode;
  redraw(): void;
  drawCompareSplit(mode: 'vertical' | 'horizontal', opts?: { withDivider?: boolean }): void;
  drawHoldPreview(): void;
  drawLine(line: CodecLine, isSelected?: boolean, lineIdx?: number): void;
  drawPoint(point: { x: number; y: number }, color: string, pointSize?: number, isSelected?: boolean, highlightState?: 0 | 1 | 2): void;
  /** 0 none, 1 hover, 2 focused — regardless of which line the coord table shows. */
  pointHighlightState(lineIdx: number, ptIdx: number): 0 | 1 | 2;
}
