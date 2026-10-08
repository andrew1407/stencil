// The pointer's hit tests over what the canvas draws: a hidden point is never a point target,
// hidden lines never a segment target (desktop twin: model/markHits.hpp). Thresholds are image px.
import type { CodecLine } from '../line/linesCodec.js';
import type { HoldTarget } from '../draw/holdDraw.js';

type Lines = readonly Pick<CodecLine, 'points'>[];

/** Which marks the canvas paints; a flag never set counts as shown. */
export interface ShownMarks {
  points: boolean;
  lines: boolean;
}

export declare const shownMarks: (app: { showPoints?: boolean; showLines?: boolean }) => ShownMarks;
/** The line under (x, y) by what shows: findLineAt with both, the stroke or the points alone, else -1. */
export declare const lineAt: (lines: Lines, shown: ShownMarks, x: number, y: number, threshold: number) => number;
/** holdDrawTarget with a hidden kind out of reach; `radius` defaults to HIT.grabRadiusPx. */
export declare const holdTargetAt: (
  lines: Lines | null | undefined, shown: ShownMarks, x: number, y: number, radius?: number,
) => HoldTarget;
