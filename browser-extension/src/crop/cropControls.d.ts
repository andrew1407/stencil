// Shapes for crop/cropControls.js — the two things that decide the crop's aspect
// (page format and orientation); every change re-centres the rect.
import type { CropState } from './cropHandoff.js';

export interface CropControls {
  syncPageControls(): void;
  syncOrientationButtons(): void;
  onCustom(): void;
}

export declare function createCropControls(
  opts: { state: CropState; resetCrop: () => void },
): CropControls;
