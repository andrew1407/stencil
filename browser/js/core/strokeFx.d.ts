// The vertices currently in flight: one record per point just added, flying from where it
// came from to where it was put (maths in ui/motion.js). A record holds the point OBJECT,
// not its index — a later insert shifts every index after it.
import type { DrawingApp } from './drawingApp.js';
import type { CodecLine } from './linesCodec.js';

export interface Point { x: number; y: number; }
export type FxLine = Pick<CodecLine, 'points'> & Partial<CodecLine>;
export interface FlightRecord {
  line: FxLine; pt: Point; from: Point; to: Point;
  bow: number; flyMs: number; start: number;
}

export declare class StrokeFx {
  constructor(app: DrawingApp | null, opts?: { now?: () => number; schedule?: ((fn: FrameRequestCallback) => number) | null });
  readonly active: boolean;
  /** Nothing is drawn in flight while suspended (an export is the RESTING picture). */
  suspend(): void;
  resume(): void;
  /** null when motion is off or the point does not exist. */
  flyIn(line: FxLine | null | undefined, idx: number, from?: Point | null): FlightRecord | null;
  flyInRange(line: FxLine, startIdx: number, count: number, from?: Point | null): void;
  cancel(): void;
  has(line: FxLine): boolean;
  /** The line's points as DRAWN this frame — the array itself when nothing moves. */
  pointsOf(line: FxLine): Point[];
  scaleAt(pt: Point): number;
  paintUnder(ctx: CanvasRenderingContext2D, line: FxLine, pts: readonly Point[]): void;
  paintOver(ctx: CanvasRenderingContext2D, line: FxLine, pts: readonly Point[]): void;
}
