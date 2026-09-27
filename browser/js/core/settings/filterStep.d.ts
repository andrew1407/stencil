// The filter as an undo step: a committed mode or tint pushes one editor memento when it moved
// the filter off the step on screen; the commits of one batch share a step.
import type { DrawingApp } from '../drawingApp.js';

export declare class FilterSteps {
  app: DrawingApp;
  constructor(app: DrawingApp);
  /** One filter commit: pushed now, or at the end of the open batch. */
  committed(): void;
  /** Runs `fn`; its filter commits land as one undo step, or none with `history: false`. */
  batch<T>(fn: () => T, opts?: { history?: boolean }): T;
}
