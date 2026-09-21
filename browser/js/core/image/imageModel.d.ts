// Non-destructive crop + quarter-turn rotation over the app's `originalImage` (never
// modified) → the working cropped `image`, tracked by `cropRect` (rotated-original pixels)
// and `rotationQuarters`. Pure geometry lives in cropGeometry.js; this orchestrates it.
import type { CropRect } from '../geometry.js';
import type { DrawingApp } from '../drawingApp.js';
import type { CropRectInput } from './imageLoadFlow.js';

export declare class ImageModel {
  app: DrawingApp;
  constructor(app: DrawingApp);
  /** Centered page-aspect crop; orientation auto-matches the image unless `albumOverride` is given. */
  defaultCropRect(albumOverride?: boolean | null): CropRect;
  /** Original dimensions after the current rotation (odd quarter-turns swap width and height). */
  rotatedOriginalDims(): { width: number; height: number };
  effectiveOriginalDims(): { width: number; height: number };
  /** The stored data URL untouched when unrotated, else a freshly rotated canvas encoded. */
  effectiveOriginalDataUrl(): string;
  /** Snaps to integer pixels, clamped inside the rotated original. */
  roundRect(r: CropRectInput, iw?: number, ih?: number): CropRect;
  rebuildCroppedImage(): void;
  /** dir < 0 rotates left (CCW), dir > 0 right (CW); crop window and lines follow the picture. */
  rotateImage(dir: number): void;
  /** With `recalc`, lines are cleared on an orientation flip or rescaled to the new size. */
  applyCrop(rect: CropRectInput, opts?: { recalc?: boolean }): void;
}
