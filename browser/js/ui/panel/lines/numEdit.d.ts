// A Lines-row number edited in place: Enter or blur commits one history step, Escape keeps it.
import type { DrawingApp } from '../../../core/drawingApp.js';

/** Turns `cell` (a `.lines-num` naming its `data-prop`, thickness or pointSize) into a field for line `idx`; inert in a read-only compare view. */
export declare const editLineNumber: (app: DrawingApp, cell: HTMLElement, idx: number) => void;
