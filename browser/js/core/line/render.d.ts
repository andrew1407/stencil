// One line or point painted into `r.ctx`: `r` is the Renderer, or the resting stand-in
// core/draw/restingPaint.js hands the image worker.
import type { StrokeFx } from './strokeFx.js';

/** What a line or point paints through. */
export interface LinePainter {
  readonly ctx: CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D;
  suppressHighlight: boolean;
  app: {
    strokeFx: Pick<StrokeFx, 'pointsOf' | 'scaleAt' | 'paintUnder' | 'paintOver'>;
    showPoints: boolean;
    pointSize: number;
    selGlowColor?: string;
    hoverRingColor?: string;
    focusRingColor?: string;
    listHoverLineIdx?: number;
  };
  pointHighlightState(lineIdx: number, ptIdx: number): 0 | 1 | 2;
}

/** A point's ring colour for its canvas-normalised fill: '#000' from MARKER_RING.darkFromLuma up, else '#fff'. */
export declare const ringFor: (css: string) => '#000' | '#fff';
/** The colour a line's points draw in: its own pointColor when set, else its stroke colour. */
export declare const pointColorOf: (line: { color: string; pointColor?: string }) => string;
export declare function drawLine(r: LinePainter, line: object, isSelected?: boolean, lineIdx?: number): void;
export declare function drawPoint(r: LinePainter, point: { x: number; y: number }, color: string,
                                 pointSize?: number, isSelected?: boolean, highlightState?: 0 | 1 | 2): void;
