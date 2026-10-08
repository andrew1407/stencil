import type { DrawingApp } from '../../core/drawingApp.js';
import type { Point } from '../../core/geometry.js';

/** The points-table DOM + per-row interactions. */
export declare class CoordTable {
  constructor(app: DrawingApp);
  /** No arguments re-renders the line the table targets; `points` alone are listed against the line owning them. */
  update(points?: readonly Point[] | null, lineIdx?: number): void;
  /** Rewrites the cells in place when the table already lists `points`; rebuilds otherwise. */
  refreshRows(points: readonly Point[], lineIdx: number): void;
  focusRowAfterRemoval(index: number): void;
  applyRowHighlight(): void;
  refreshCoordRow(ptIdx: number): void;
}
