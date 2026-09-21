// Transforms over the selection: rotate / flip / quarter-turn / nudge. The point maths is
// transforms.js; this is the selection plumbing — which pivot, which lines, and the one
// debounced history save a burst of them collapses into.
import type { DrawingApp } from '../drawingApp.js';

/** Radians; one selected line pivots on its focused point when there is one. */
export declare const rotateSelectedLine: (app: DrawingApp, angle: number) => void;
export declare const flipSelectedLine: (app: DrawingApp, horizontal: boolean) => void;
/** dir > 0 → +90° clockwise, dir < 0 → −90°. */
export declare const rotateSelectedLineQuarter: (app: DrawingApp, dir: number) => void;
/** Image-space px; the arrow-key nudge. */
export declare const nudgeSelected: (app: DrawingApp, dx: number, dy: number) => unknown;
