// Pure hit-testing over the editor's line model, allocation-free with a bbox early reject
// (twin: core/geometry/hitTest.cpp). Results are topmost-first; thresholds are in image
// pixels, so callers divide a screen radius by the zoom.
import type { Point } from './geometry.js';
import type { CodecLine } from './line/linesCodec.js';

type Lines = readonly Pick<CodecLine, 'points'>[];

/** Index of the topmost line within `threshold` (points padded +4), or -1. */
export declare function findLineAt(lines: Lines, x: number, y: number, threshold: number): number;
/** First point within `threshold` across committed lines then the in-progress line. */
export declare function findNearestPoint(
  lines: Lines, currentLine: Pick<CodecLine, 'points'> | null, x: number, y: number, threshold: number,
): Point | null;
/** lineIdx -1 = the in-progress line (checked first); committed lines scan topmost-first. */
export declare function findNearestPointWithIdx(
  lines: Lines, currentLine: Pick<CodecLine, 'points'> | null, x: number, y: number, threshold: number,
): { lineIdx: number; ptIdx: number; point: Point } | null;
/** Nearest segment among committed lines. */
export declare function findNearestSegmentWithIdx(
  lines: Lines, x: number, y: number, threshold: number,
): { lineIdx: number; ptIdx1: number; ptIdx2: number } | null;
