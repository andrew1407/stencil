// The touchscreen flow's gesture machine: which of tap / point / segment / pinch a press
// became, and how each follows and ends. The hold-draw half stays in inputController.js.
import type { InputController } from './inputController.js';

/** The four listeners inputController.js registers, in one closure over the long-press timer. */
export interface TouchHandlers {
  onStart: (e: TouchEvent) => void;
  onMove: (e: TouchEvent) => void;
  onEnd: (e: TouchEvent) => void;
  onCancel: () => void;
}

export declare const touchHandlers: (ctrl: InputController, viewport: HTMLElement) => TouchHandlers;
