// The app-side view of a hold-draw stroke: the ghost vertex under the finger, where the
// preview line emanates from, and the clock both input paths time holds against.
import type { DrawingApp } from './drawingApp.js';
import type { XY } from './abi/stencilCore.js';

/** performance.now() where there is one, else Date.now(). */
export declare const nowMs: () => number;
export declare const setHoldPreview: (app: DrawingApp, x: number, y: number) => void;
export declare const clearHoldPreview: (app: DrawingApp) => void;
/** The stroke's growing tip: the head when prepending, else the point before the tail. */
export declare const holdAnchor: (app: DrawingApp, prepend: boolean) => XY | null;
