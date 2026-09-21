// Shapes for crop/controls.js — the two things that decide the crop's aspect
// (page format and orientation); every change re-centres the rect.
import type { CropState } from './handoff.js';

export interface CropControls {
  syncPageControls(): void;
  syncOrientationButtons(): void;
  onCustom(): void;
}

export declare function createCropControls(
  opts: { state: CropState; resetCrop: () => void; swapCrop?: () => void },
): CropControls;
