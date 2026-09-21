// The single/multi selection set and the two ways to change it (⌘/Ctrl+Shift+click on
// the canvas, a click in the Lines tab). Single-select keeps `selectedLines` empty and
// reads `selectedLineIdx`; 2+ selected hides the single-line editor.
import type { DrawingApp } from '../drawingApp.js';

/** Valid, in-range indices only. */
export declare const selectedIndices: (app: DrawingApp) => number[];
export declare const isLineSelected: (app: DrawingApp, i: number) => boolean;
/** Adds/removes `idx`; exactly one left drops back to single-select. */
export declare const toggleLineSelection: (app: DrawingApp, idx: number) => void;
/** "N lines selected" in the status line while 2+ are selected. */
export declare const updateMultiSelectStatus: (app: DrawingApp) => void;
/** A click on the letterbox outside the image drops the selection. */
export declare const deselectEmptyArea: (app: DrawingApp, e: MouseEvent | null | undefined) => void;
/** One field of the selected line, then a history entry. */
export declare const applySelectionChange: (app: DrawingApp, prop: string, value: unknown) => void;
/** The Lines-tab hover glow; -1 or out of range clears it. */
export declare const setListHoverLine: (app: DrawingApp, idx: number) => void;
/** Keyed by index so the list and the canvas stay in sync; `ctrlShift` toggles instead. */
export declare const selectLineFromList: (app: DrawingApp, idx: number, ctrlShift?: boolean) => unknown;
