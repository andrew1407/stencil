import type { DrawingApp } from '../core/drawingApp.js';
import type { Line, Point, Stencil } from './stencilApi.js';

export interface LineWrappersDeps {
  app: DrawingApp;
  guard: <T extends object>(obj: T) => T;
}

export interface LineWrappers {
  makePoint(lineIdx: number, ptIdx: number): Point;
  makeLine(startIdx: number): Line;
  /** The facade this factory falls back to for chaining; set once createStencil finishes. */
  setFacade(f: Stencil): void;
}

/** window.stencil's Line and Point wrappers; mutually recursive, so they share one factory. */
export declare const createLineWrappers: (deps: LineWrappersDeps) => LineWrappers;
