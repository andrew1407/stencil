// The crop half of the wasm op map: the page aspect, the centred default rect, every rect
// edit — corner resize, move, scale, orientation swap and quarter rotation — and the commit snap.
import type { CoreOps } from './stencilCore.js';
import type { CoreMarshal } from './coreMarshal.js';

export type CropOpName =
  'isAlbumOrientation' | 'cropAspect' | 'centeredCrop' | 'resizeCropFromCorner' |
  'moveCropClamped' | 'scaleCropCentered' | 'swapCropOrientation' | 'cropResizeScale' |
  'cropChange' | 'rotateCropRectQuarter' | 'snapCropRect' | 'rotateEditQuarter';

export declare const buildCropOps: (core: unknown, m: CoreMarshal) => Pick<CoreOps, CropOpName>;
