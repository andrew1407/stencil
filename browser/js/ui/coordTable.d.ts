import type { DrawingApp, Point } from '../core/drawingApp.js';

/** The points-table DOM + per-row interactions. */
export declare class CoordTable {
  constructor(app: DrawingApp);
  update(points?: readonly Point[] | null, lineIdx?: number): void;
  focusRowAfterRemoval(index: number): void;
  applyRowHighlight(): void;
  refreshCoordRow(ptIdx: number): void;
}
