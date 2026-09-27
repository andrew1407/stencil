// The resting annotations an export, a thumbnail and the co-edit result carry: every line (or
// its points alone) with no ring, glow or vertex in flight — one loop for the Renderer and for
// the stand-in the image worker paints through.
import type { CodecLine } from '../line/linesCodec.js';
import type { LinePainter } from '../line/render.js';

export interface RestingState {
  showLines: boolean;
  showPoints: boolean;
  pointSize: number;
}

/** Anything that draws a line and a point the Renderer's way. */
export interface LineDrawer {
  drawLine(line: CodecLine, isSelected?: boolean): void;
  drawPoint(point: { x: number; y: number }, color: string, pointSize?: number, isSelected?: boolean): void;
}

export declare const paintRestingLines: (r: LineDrawer, lines: readonly CodecLine[], state: RestingState) => void;
/** A painter over any 2D context that needs no editor. */
export declare const restingPainter: (
  ctx: CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D,
  state: Pick<RestingState, 'showPoints' | 'pointSize'>,
) => LinePainter & LineDrawer;

/** A resting paint's inputs, snapshotted: what the image worker or this thread renders from. */
export interface RestingJob extends RestingState {
  base: CanvasImageSource;
  width: number;
  height: number;
  lines: CodecLine[];
}
export declare const restingJob: (app: {
  renderer: { restingBase(): CanvasImageSource };
  canvas: { width: number; height: number };
  lines: CodecLine[];
} & RestingState) => RestingJob;
