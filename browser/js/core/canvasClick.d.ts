// What a click on the canvas means: while drawing it extends/closes the stroke, a Ctrl/Cmd
// click inserts or adds a point, a plain click selects the point or line under the cursor.
import type { DrawingApp } from './drawingApp.js';

/** Route one canvas click (reached as app.canvasClick(e)); a no-op without an image. */
export declare const canvasClick: (app: DrawingApp, e: MouseEvent) => void;
