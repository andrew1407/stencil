import type { CropRect } from '../../core/geometry.js';
import type { CropState, CropOverlay } from './crop.js';
import type { CropRows } from './cropRows.js';
import type { PreviewDust } from './previewDust.js';
import type { FrameScrub } from './frameScrub.js';
import type { MediaPairs } from './mediaPairs.js';
import type { OpenImageSource } from './tabs.js';

/** What each source tab remembers of its own decode and crop choice. */
export interface TabMemo {
  seen: Record<string, string | null>;
  ready: Record<string, boolean>;
  crop: Record<string, boolean>;
  cropRect: Record<string, { rect: CropRect; iw: number; ih: number } | null>;
}

export interface PreviewFlow {
  /** An arriving decode: fit the rect, show the box, tell the dialog to re-gate. */
  mediaDecoded: (el: HTMLElement, px: { width: number; height: number }) => void;
  /** What the preview box shows right now, crop rows and scrub bar included. */
  syncPreview: () => void;
  /** Fetch/decode this tab's source; `replacing` blows the old picture away first. */
  loadPreviewMedia: (replacing?: boolean) => void;
  /** Bring a re-shown tab back to its own picture, decoding only if it must. */
  showTabPreview: () => void;
}

export declare function createPreviewFlow(ctx: {
  els: Record<string, HTMLElement>;
  src: OpenImageSource;
  cropState: CropState;
  crop: CropOverlay;
  cropRows: CropRows;
  dust: PreviewDust;
  frame: FrameScrub;
  pairs: MediaPairs;
  memo: TabMemo;
  refresh: () => void;
  tab: () => string;
  ready: () => boolean;
  setReady: (v: boolean) => void;
  previewReady: () => boolean;
  isVideoSource: () => boolean;
  sourceKey: () => string;
  urlPreviewShown: () => boolean;
  restoreCropFor: (width: number, height: number) => void;
}): PreviewFlow;
