// Breaking the chain while drawing: the stroke so far is kept and an unconnected stroke opens
// at the press, drawing mode stays on. Desktop twin: canvas/draw/CanvasChainBreak.cpp.
import type { DrawingApp } from '../drawingApp.js';
import type { Point } from '../geometry.js';

/** Record the point a drawing click just placed (held by object, not index). */
export declare const noteDrop: (app: DrawingApp, line: { points: Point[] }, pt: Point) => void;
/** How long a click's new segment waits before it flies: the double-click (or double-tap) window. */
export declare const dropHoldMs: (e?: { pointerType?: string } | null) => number;
/** Commit the current chain (a lone point included) and start a new one at (x, y). */
export declare const breakChainAt: (app: DrawingApp, x: number, y: number) => void;
/** A double-click's second click: move the point the first click dropped onto a new chain. */
export declare const breakChainOnRepeat: (app: DrawingApp, x: number, y: number) => boolean;
