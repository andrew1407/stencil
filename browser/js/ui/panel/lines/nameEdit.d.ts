// A Lines-row name edited in place: Enter or blur commits one history step, Escape keeps it.
import type { DrawingApp } from '../../../core/drawingApp.js';

/** The row's label for line `idx`: its name, else "Line N". */
export declare const shownName: (line: { name?: string } | null | undefined, idx: number) => string;
/** What `typed` stores for line `idx`: an unnamed line's own "Line N", left as it was, stores ''. */
export declare const typedName: (line: { name?: string } | null | undefined, idx: number, typed: string) => string;
/** Writes that label into `cell`, muted (`lines-name-unset`) while the line is unnamed. */
export declare const paintNameCell: (cell: HTMLElement, line: { name?: string } | null | undefined, idx: number) => void;
/** Turns the `.lines-name` `cell` into a text field for line `idx`; inert in a read-only compare view. */
export declare const editLineName: (app: DrawingApp, cell: HTMLElement, idx: number) => void;
