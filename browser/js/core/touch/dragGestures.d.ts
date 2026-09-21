// The Alt-drag gesture engine (point / segment / whole line) over the app's shared drag
// state, plus the ring helpers behind chaining: a click-closed shape repeats its first
// point at the end, and "the ring" is always the points minus that duplicate.
import type { Point } from '../geometry.js';
import type { DrawingApp } from '../drawingApp.js';
import type { CodecLine } from '../line/linesCodec.js';

type Line = Pick<CodecLine, 'points' | 'locked' | 'fillColor'>;

/** A grabbed vertex, or a grabbed segment body (its two endpoint indices). */
export type PullOutTarget =
  | { kind: 'point'; ptIdx: number; lineIdx?: number }
  | { kind: 'segment'; ptIdx1: number; ptIdx2: number; lineIdx?: number };

export interface NearestSegment { lineIdx: number; ptIdx1: number; ptIdx2: number; }
/** The point being dragged; lineIdx −1 addresses the stroke still being drawn. */
export interface DraggingPoint { lineIdx: number; ptIdx: number; point?: Point; }

export declare const ringPoints: (points: readonly Point[]) => Point[];
/** Open a ring at vertex k, re-rooted to start there and end on a copy of it. */
export declare const openRingAt: (points: readonly Point[], k: number) => Point[];
export declare const clearFill: (line: Pick<CodecLine, 'fillColor'>) => void;
/** Unchain an area back into an open polyline; false when it was not locked. */
export declare const unchainLine: (line: Line | null | undefined) => boolean;
/** Add the pulled-out point (opening a locked line first); returns its index, or −1. */
export declare const pullOutPoint: (line: Line | null | undefined, target: PullOutTarget | null | undefined, x: number, y: number) => number;
export declare function beginSegmentDrag(app: DrawingApp, nearSeg: NearestSegment, x: number, y: number): void;
export declare function movePointTo(app: DrawingApp, dp: DraggingPoint, x: number, y: number): void;
export declare function endPointDrag(app: DrawingApp, dp: DraggingPoint | null, altKey: boolean): void;
export declare function endSegmentDrag(app: DrawingApp, altKey: boolean): void;
/** Apply the active segment / whole-line drag at the cursor; Shift promotes a segment drag to a translate. */
export declare function dragMove(app: DrawingApp, clientX: number, clientY: number, shiftKey: boolean): void;
export declare function finishDragGesture(app: DrawingApp, altKey: boolean): void;
