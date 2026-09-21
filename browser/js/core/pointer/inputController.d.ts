// Hold-to-draw (a near-stationary press auto-enters drawing) plus the seam the touchscreen
// flow (input.js) drives. The mouse path stays in controller.js; all three
// share the app's drag-state fields.
import type { DrawingApp } from '../drawingApp.js';
import type { PinchSession } from '../touch/pinch.js';
import type { TouchSession } from '../touch/drag.js';

/** An image to draw on, drawing off, no gesture running, and not the rect tool. */
export declare const holdDrawEligible: (app: DrawingApp | null | undefined) => boolean;

export declare class InputController {
  app: DrawingApp;
  constructor(app: DrawingApp);
  /** True while press-and-hold is armed or drawing (mouse or touch). */
  readonly holdEngaged: boolean;
  /** The live touch gesture; input.js drives it, this class only ends a tap. */
  touchSession: TouchSession | PinchSession | null;
  wireHoldDraw(): void;
  wireTouch(): void;
  /** Arms a hold at a modifier-free press; false when the editor is not eligible. */
  armHold(clientX: number, clientY: number): boolean;
  /** A second finger, a cancel or a long-press gives up whatever hold was armed. */
  abandonHold(): void;
  /** A tap-mode finger wandering: preview along, or give the hold up. */
  moveTapGesture(clientX: number, clientY: number): void;
  /** The tap release: commit the held stroke, or let `tap` click through. */
  endTapGesture(st: TouchSession, tap: () => void): void;
  /** Where a hold-draw preview line emanates from; null when there is no stroke. */
  holdAnchorPoint(): { x: number; y: number } | null;
  /** Clamped to 100..3000 ms; persisted unless `persist` is false. */
  setHoldDrawDelay(ms: number, opts?: { persist?: boolean }): void;
}
