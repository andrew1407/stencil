// Crop-window geometry, a port of core/cropGeometry.{hpp,cpp}. A crop is an axis-aligned
// rect in ORIGINAL-image pixels; the public names route to the wasm core when loaded and
// to the *JS references otherwise (the parity tests drive both).
import type { CodecLine } from './linesCodec.js';

export interface CropRect { x: number; y: number; width: number; height: number; }
/** Corners: 0 top-left, 1 top-right, 2 bottom-right, 3 bottom-left. */
export type CropCorner = 0 | 1 | 2 | 3;
export interface CropChange { orientationChanged: boolean; scale: number; }
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
export declare const cropChangeJS: (oldRect: CropRect, newRect: CropRect) => CropChange;
/** Multiply every point of every line by `scale` in place. */
export declare const scaleLinePoints: (lines: Lines, scale: number) => void;
/** Rotate a crop rect one quarter turn inside an imageW × imageH space. */
export declare const rotateCropRectQuarterJS: (r: CropRect, imageW: number, imageH: number, clockwise: boolean) => CropRect;
/** Rotate every crop-local point one quarter turn inside a boxW × boxH box, in place. */
export declare const rotateLinePointsQuarter: (lines: Lines, boxW: number, boxH: number, clockwise: boolean) => void;

export declare const isAlbumOrientation: typeof isAlbumOrientationJS;
export declare const cropAspect: typeof cropAspectJS;
export declare const centeredCrop: typeof centeredCropJS;
export declare const resizeCropFromCorner: typeof resizeCropFromCornerJS;
export declare const moveCropClamped: typeof moveCropClampedJS;
export declare const scaleCropCentered: typeof scaleCropCenteredJS;
export declare const cropResizeScale: typeof cropResizeScaleJS;
export declare const cropChange: typeof cropChangeJS;
export declare const rotateCropRectQuarter: typeof rotateCropRectQuarterJS;
