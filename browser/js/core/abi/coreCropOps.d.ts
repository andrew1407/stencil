// The crop half of the wasm op map: the page aspect, the centred default rect, and every
// rect edit — corner resize, move, scale, orientation swap and quarter rotation.
import type { CoreOps } from './stencilCore.js';
import type { CoreMarshal } from './coreMarshal.js';

export type CropOpName =
  'isAlbumOrientation' | 'cropAspect' | 'centeredCrop' | 'resizeCropFromCorner' |
  'moveCropClamped' | 'scaleCropCentered' | 'swapCropOrientation' | 'cropResizeScale' |
  'cropChange' | 'rotateCropRectQuarter';

export declare const buildCropOps: (core: unknown, m: CoreMarshal) => Pick<CoreOps, CropOpName>;
