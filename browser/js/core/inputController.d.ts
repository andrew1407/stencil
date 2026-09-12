// The two alternative input flows: hold-to-draw (a near-stationary press auto-enters
// drawing) and touchscreen (direct-manipulation drag + two-finger pan/pinch). The mouse
// path stays in pointerController.js; both share the app's drag-state fields.
import type { DrawingApp } from './drawingApp.js';

/** An image to draw on, drawing off, no gesture running, and not the rect tool. */
export declare const holdDrawEligible: (app: DrawingApp | null | undefined) => boolean;

export declare class InputController {
  app: DrawingApp;
  constructor(app: DrawingApp);
  /** True while press-and-hold is armed or drawing (mouse or touch). */
  readonly holdEngaged: boolean;
  wireHoldDraw(): void;
  wireTouch(): void;
  /** Where a hold-draw preview line emanates from; null when there is no stroke. */
  holdAnchorPoint(): { x: number; y: number } | null;
  /** Clamped to 100..3000 ms; persisted unless `persist` is false. */
  setHoldDrawDelay(ms: number, opts?: { persist?: boolean }): void;
}
