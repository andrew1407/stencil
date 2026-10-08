// The single/multi selection set and the ways to change it (⌘/Ctrl+Shift+click on
// the canvas, a click in the Lines tab), and one line's own style by index. Single-select
// keeps `selectedLines` empty and reads `selectedLineIdx`; 2+ selected hides the editor.
import type { DrawingApp } from '../drawingApp.js';

/** Valid, in-range indices only. */
export declare const selectedIndices: (app: DrawingApp) => number[];
/** Whether line i is selected, frozen for one frame: a Set lookup per line. */
export declare const selectionPredicate: (app: DrawingApp) => (i: number) => boolean;
/** Adds/removes `idx`; exactly one left drops back to single-select. */
export declare const toggleLineSelection: (app: DrawingApp, idx: number) => void;
/** Writes the "N lines selected" status note for the selection as it stands; signals nothing. */
export declare const paintMultiSelectStatus: (app: DrawingApp) => void;
/** "N lines selected" in the status line while 2+ are selected, then a `selection` change. */
export declare const updateMultiSelectStatus: (app: DrawingApp) => void;
/** After the line set was replaced whole: no selection, hover or focus survives, and the points table falls back to the in-progress stroke, else the last line. Signals nothing. */
export declare const settleReplacedLines: (app: DrawingApp) => void;
/** After the same lines moved (turn, flip, a crop that keeps them, undo, redo): the selection stays on the lines still in range, the bar and points table re-read them; with none left, settleReplacedLines. Signals nothing. */
export declare const keepLineSelection: (app: DrawingApp) => void;
/** A click on the letterbox outside the image drops the selection. */
export declare const deselectEmptyArea: (app: DrawingApp, e: MouseEvent | null | undefined) => void;
/** One field of line `idx` (thickness and pointSize clamped to LIMITS), then a history entry and a `lines` change unless `commit` is false (a live preview). False when nothing applied. */
export declare const applyLineChange: (app: DrawingApp, idx: number, prop: string, value: unknown, opts?: { commit?: boolean }) => boolean;
/** applyLineChange on the selected line. */
export declare const applySelectionChange: (app: DrawingApp, prop: string, value: unknown, opts?: { commit?: boolean }) => boolean;
/** The Lines-tab hover glow; -1 or out of range clears it. */
export declare const setListHoverLine: (app: DrawingApp, idx: number) => void;
/** Keyed by index so the list and the canvas stay in sync; `ctrlShift` toggles instead. */
export declare const selectLineFromList: (app: DrawingApp, idx: number, ctrlShift?: boolean) => unknown;
