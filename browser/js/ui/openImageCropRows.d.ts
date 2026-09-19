import type { DrawingApp } from '../core/drawingApp.js';

export interface CropRows {
  /** The page or plain ratio the crop's aspect is locked to. */
  pageDims: () => { width: number; height: number };
  /** Show or hide the orientation button and the size row (Custom's fields collapse first). */
  syncCropUi: (shown: boolean) => void;
  /** Show or hide the dimensions read-out, consuming the pending user-change flag. */
  syncDimsRow: (cropping: boolean) => void;
  hideDims: () => void;
  /** Take every crop-only row down at once, silently. */
  hideAll: () => void;
  /** Back to "Page" and the project's own custom size (a fresh open). */
  reset: () => void;
  /** The next sync is the user's own change, so it plays its cloud. */
  markUserChange: () => void;
}

export declare function createCropRows(args: {
  app: DrawingApp;
  els: {
    cropDims: HTMLElement;
    orientBtn: HTMLElement;
    cropSizeRow: HTMLElement;
    cropSizeCustom: HTMLElement;
    cropSizeSel: HTMLSelectElement;
    cropSizeW: HTMLInputElement;
    cropSizeH: HTMLInputElement;
  };
  fitToPage: () => boolean;
  persist: () => void;
}): CropRows;
