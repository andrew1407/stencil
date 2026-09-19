import type { CropState, CropOverlay } from './openImageCrop.js';
import type { CropRows } from './openImageCropRows.js';
import type { PreviewDust } from './openImagePreviewDust.js';
import type { FrameScrub } from './openImageFrameScrub.js';

/** What the chosen source IS, asked per tab; the fields themselves live in openImageModal.js. */
export interface OpenImageSource {
  urlVal: () => string;
  chosenFile: () => File | null | undefined;
  hasSource: () => boolean;
  isVideoOn: (tab: string) => boolean;
  keyOn: (tab: string) => string;
}

export interface OpenImageTabs {
  tab: () => string;
  /** The active tab's media has decoded, so the box has its real size. */
  ready: () => boolean;
  previewReady: () => boolean;
  isVideoSource: () => boolean;
  sourceKey: () => string;
  isVideoTab: () => boolean;
  img: () => HTMLImageElement;
  video: () => HTMLVideoElement;
  cropMedia: () => HTMLElement;
  mediaPixels: () => { width: number; height: number };
  /** Remember the dragged rect against the decode it was dragged on. */
  persistCropRect: () => void;
  syncPreview: () => void;
  loadPreviewMedia: (replacing?: boolean) => void;
  setTab: (name: string) => void;
  reset: () => void;
  closeUp: () => void;
  rememberCropChoice: () => void;
  markUrlPreviewed: () => void;
  retireUrlPreview: () => void;
}

export declare function createOpenImageTabs(args: {
  els: Record<string, HTMLElement>;
  src: OpenImageSource;
  cropState: CropState;
  crop: CropOverlay;
  cropRows: CropRows;
  dust: PreviewDust;
  frame: FrameScrub;
  refresh: () => void;
  canReplace: () => boolean;
}): OpenImageTabs;
