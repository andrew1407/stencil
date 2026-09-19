import type { CropRect } from '../core/geometry.js';

/** The crop's own state, in ORIGINAL-image pixels; `scale` is the only screen-space number. */
export interface CropState {
  rect: CropRect;
  album: boolean;
  aspect: number;
  scale: number;
  iw: number;
  ih: number;
}

export declare const freshCropState: () => CropState;

/** A rect worth opening with — smaller means "no crop fitted yet", not a 0×0 crop. */
export declare const hasCropRect: (state: CropState) => boolean;

export interface CropOverlay {
  /** Settle any flight, show the box and shade, repaint the rect and the read-out. */
  render(): void;
  hide(): void;
  /** The Album/Portrait press; `fly` eases the box from its old shape to the new one. */
  recenter(fly?: boolean): void;
  /** A fresh centred rect at the page's aspect; false when no picture is measured yet. */
  fitToPage(): boolean;
  /** Re-read the media's on-screen width into `state.scale`. */
  computeScale(): void;
  settle(): void;
}

export declare const createCropOverlay: (args: {
  state: CropState;
  els: { box: HTMLElement; shade: HTMLElement; dims: HTMLElement; orient: HTMLElement };
  pageDims: () => { width: number; height: number };
  media: () => HTMLElement;
  onDrag?: () => void;
}) => CropOverlay;
