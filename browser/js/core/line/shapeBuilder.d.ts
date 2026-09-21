// Shape building: closing a stroke into a locked area, inserting + connecting points, and
// rectangles. Each takes the app and mutates its shared drawing state (lines / currentLine
// / continueLineIdx / selection); the click path and hold-to-draw both come through here.
import type { DrawingApp } from '../drawingApp.js';
import type { CodecLine } from './linesCodec.js';

/** The close-grab radius, kept screen-constant when zoomed out (never harder to hit than 1:1). */
export declare const closeGrabSize: (app: DrawingApp, line: Partial<CodecLine>) => number;
/** true when (x, y) closed the stroke being drawn into a locked area. */
export declare const tryCloseShapeAt: (app: DrawingApp, x: number, y: number) => boolean;
export declare const closeCurrentShape: (app: DrawingApp) => void;
export declare const closeContinuedShape: (app: DrawingApp) => void;
export declare const closeShape: (app: DrawingApp,
  opts: { line: Partial<CodecLine> | null; idx?: number; isContinuation: boolean }) => void;
export declare const insertPointOnSegment: (app: DrawingApp, lineIdx: number, insertIdx: number, x: number, y: number) => void;
/** A standalone point, or one connected to the selected line (inheriting its style). */
export declare const addConnectedPoint: (app: DrawingApp, x: number, y: number) => void;
/** Four corners as a locked area; `connect` appends them to the selected line instead. */
export declare const createRect: (app: DrawingApp, x1: number, y1: number, x2: number, y2: number, connect?: boolean) => void;
