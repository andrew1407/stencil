// The points table's rows: built from a point, rewritten in place on a re-list, repainted on a move.
import type { DrawingApp } from '../../core/drawingApp.js';
import type { Point } from '../../core/geometry.js';

export declare const buildCoordRow: (app: DrawingApp, point: Point, index: number) => HTMLTableRowElement;
/** Rewrites the rows `body` holds for `points`, adding or dropping at the end; false when a row is mid-exit (rebuild). */
export declare const reuseCoordRows: (app: DrawingApp, body: HTMLElement, points: Point[]) => boolean;
/** A moved point's cells, leaving an open cell editor alone. */
export declare const paintCoordRow: (app: DrawingApp, row: HTMLElement, point: Point | null | undefined) => void;
/** A windowed table's unseen sizing row: each column's longest text over every point. */
export declare const coordSizerRow: (app: DrawingApp, points: readonly Point[]) => HTMLTableRowElement;
