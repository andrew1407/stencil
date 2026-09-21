// Shapes for crop/stage.js — the quick-crop viewport: fit/zoom, the crop-box
// overlay and its drag/resize, and the live preview canvas. `aspect` is injected —
// the page format the rect is locked to belongs to the controls (controls.js).
import type { CropState } from './handoff.js';

export interface CropStage {
  imgEl: HTMLImageElement;
  overlay: HTMLElement;
  fitToWindow(): void;
  resetCrop(): void;
  /** resetCrop, with the box easing there from its old shape (the orientation press). */
  swapCrop(): void;
  /** `shown`: a mid-flight rect drawn in place of state.crop; omitted, settles any flight. */
  layoutOverlay(shown?: { x: number; y: number; width: number; height: number }): void;
  renderPreview(): void;
}

export declare function createCropStage(
  opts: { state: CropState; aspect: () => number },
): CropStage;
