// Point / line mutation shared by the coord table, the Lines tab and the console facade.
// Each keeps the selection, the coord-table target and every cached hover consistent with
// the indices it just shifted, then saves one history entry. lineIdx -1 = currentLine.
import type { DrawingApp } from './drawingApp.js';

export declare const setPointCoord: (app: DrawingApp, lineIdx: number, ptIdx: number, axis: 'x' | 'y', valuePx: number | string) => unknown;
/** Removing the last point of a committed line drops the line too. */
export declare const removePoint: (app: DrawingApp, lineIdx: number, ptIdx: number) => unknown;
export declare const removeLine: (app: DrawingApp, idx: number) => unknown;
/** Every selected line at once, as one history entry. */
export declare const removeSelectedLines: (app: DrawingApp) => unknown;
