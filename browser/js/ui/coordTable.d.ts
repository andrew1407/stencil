import type { DrawingApp } from '../core/drawingApp.js';
import type { Point } from '../core/geometry.js';

/** The points-table DOM + per-row interactions. */
export declare class CoordTable {
  constructor(app: DrawingApp);
  update(points?: readonly Point[] | null, lineIdx?: number): void;
  focusRowAfterRemoval(index: number): void;
  applyRowHighlight(): void;
  refreshCoordRow(ptIdx: number): void;
}
