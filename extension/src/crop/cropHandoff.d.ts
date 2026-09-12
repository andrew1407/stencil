// Shapes for crop/cropHandoff.js — turns the quick-crop page's state into the editor
// hand-off payload: "Keep original" sends the image + the rect, "Cut cropped part"
// bakes the region into a fresh image.

export interface CropRect { x: number; y: number; width: number; height: number; }

/** The quick-crop page's whole mutable state, shared with cropControls.js/cropStage.js. */
export interface CropState {
  srcUrl: string;
  source: string;
  resource: string;
  dataUrl: string;
  name: string;
  imgW: number;
  imgH: number;
  page: string;
  customW: number;
  customH: number;
  album: boolean;
  crop: CropRect;
  fitScale: number;
  zoom: number;
}

export interface HandoffPayload {
  dataUrl: string;
  name: string;
  /** Canonical wire spelling (w/h, not width/height). */
  crop: { x: number; y: number; w: number; h: number };
  page: { size: string; width?: number; height?: number };
  source: string;
  resource: string;
  incognito: boolean;
}

export declare function buildHandoffPayload(
  state: CropState,
  imgEl: HTMLImageElement,
  opts: { mode: 'apply' | 'cut'; incognito: boolean },
): HandoffPayload;
