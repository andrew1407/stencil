// Crop-window geometry, a port of core/geometry/cropGeometry + cropSnap. A crop is an axis-aligned
// rect in ORIGINAL-image pixels; the public names route to the wasm core when loaded and
// to the *JS references otherwise (the parity tests drive both).
import type { CropRect } from '../geometry.js';
import type { CodecLine } from '../line/linesCodec.js';

/** Corners: 0 top-left, 1 top-right, 2 bottom-right, 3 bottom-left. */
export type CropCorner = 0 | 1 | 2 | 3;
export interface CropChange { orientationChanged: boolean; scale: number; }
/** One quarter-turn of an edit: the snapped window in the turned space, and the 0..3 count. */
export interface EditTurn { crop: CropRect; quarters: number; }
type Lines = readonly Pick<CodecLine, 'points'>[];

export declare const isAlbumOrientationJS: (width: number, height: number) => boolean;
export declare const cropAspectJS: (pageWidth: number, pageHeight: number, album: boolean) => number;
export declare const centeredCropJS: (imageW: number, imageH: number, aspectWoverH: number) => CropRect;
export declare const resizeCropFromCornerJS: (cur: CropRect, corner: CropCorner, cursorX: number, cursorY: number,
  aspectWoverH: number, imageW: number, imageH: number, minSize?: number) => CropRect;
export declare const moveCropClampedJS: (cur: CropRect, dx: number, dy: number, imageW: number, imageH: number) => CropRect;
export declare const cropResizeScaleJS: (oldWidth: number, newWidth: number) => number;
/** Scale the crop about its centre by `factor`: aspect kept, capped, floored at minSize. */
export declare const scaleCropCenteredJS: (cur: CropRect, factor: number, aspectWoverH: number,
  imageW: number, imageH: number, minSize?: number) => CropRect;
/** Album/Portrait press: swap width/height about the same centre, clamped to the image. */
export declare const swapCropOrientationJS: (cur: CropRect, aspectWoverH: number,
  imageW: number, imageH: number) => CropRect;
export declare const cropChangeJS: (oldRect: CropRect, newRect: CropRect) => CropChange;
/** Multiply every point of every line by `scale` in place. */
export declare const scaleLinePoints: (lines: Lines, scale: number) => void;
/** Rotate a crop rect one quarter turn inside an imageW × imageH space. */
export declare const rotateCropRectQuarterJS: (r: CropRect, imageW: number, imageH: number, clockwise: boolean) => CropRect;
/** Rotate every crop-local point one quarter turn inside a boxW × boxH box, in place. */
export declare const rotateLinePointsQuarter: (lines: Lines, boxW: number, boxH: number, clockwise: boolean) => void;
/** Mirror every crop-local point left-right inside a box boxW wide, in place. */
export declare const mirrorLinePoints: (lines: Lines, boxW: number) => void;
/** Integer pixels inside imageW × imageH: sides Math.round-ed into [1, side], origin moved inside. */
export declare const snapCropRectJS: (r: CropRect, imageW: number, imageH: number) => CropRect;
/** The window across one quarter-turn of an unturned originalW × originalH picture at `quarters`. */
export declare const rotateEditQuarterJS: (crop: CropRect, quarters: number, originalW: number,
  originalH: number, clockwise: boolean) => EditTurn;
/** The window reflected across the turned width and the negated count, for a left-right flip of the shown picture. */
export declare const mirrorEditJS: (crop: CropRect, quarters: number, originalW: number, originalH: number) => EditTurn;

export declare const isAlbumOrientation: typeof isAlbumOrientationJS;
export declare const cropAspect: typeof cropAspectJS;
export declare const centeredCrop: typeof centeredCropJS;
export declare const resizeCropFromCorner: typeof resizeCropFromCornerJS;
export declare const moveCropClamped: typeof moveCropClampedJS;
export declare const scaleCropCentered: typeof scaleCropCenteredJS;
export declare const swapCropOrientation: typeof swapCropOrientationJS;
export declare const cropResizeScale: typeof cropResizeScaleJS;
export declare const cropChange: typeof cropChangeJS;
export declare const rotateCropRectQuarter: typeof rotateCropRectQuarterJS;
export declare const snapCropRect: typeof snapCropRectJS;
/** Turns the crop-local lines inside the old window in place, then the window itself. */
export declare const rotateEditQuarter: (lines: Lines, crop: CropRect, quarters: number,
  originalW: number, originalH: number, clockwise: boolean) => EditTurn;
/** Mirrors the crop-local lines inside the window in place, then reflects the window itself. */
export declare const mirrorEdit: (lines: Lines, crop: CropRect, quarters: number,
  originalW: number, originalH: number) => EditTurn;
