export interface MediaPair {
  img: HTMLImageElement;
  video: HTMLVideoElement;
}

/** What one tab's pair is holding: the key being loaded, its kind and its object URL. */
export interface TabMediaState {
  loading: string | null;
  isVideo: boolean;
  /** A decode that landed while this tab was away, delivered on the way back. */
  pending: boolean;
  objectUrl: string | null;
  src: string;
  pixelsReadable: boolean;
}

export interface MediaPairs {
  pairFor: (name: string) => MediaPair;
  /** The active tab's state record. */
  st: () => TabMediaState;
  img: () => HTMLImageElement;
  video: () => HTMLVideoElement;
  /** The element the crop rect is drawn on — the player itself for a video. */
  cropMedia: () => HTMLElement;
  mediaPixels: () => { width: number; height: number };
  revoke: (st: TabMediaState) => void;
  /** Hide the outgoing pair and make `name`'s the active one. */
  swapTo: (name: string) => void;
  resetAll: () => void;
  pauseAll: () => void;
}

export declare function createMediaPairs(args: {
  previewImg: HTMLImageElement;
  previewVideo: HTMLVideoElement;
  statusEl: HTMLElement;
  tab: () => string;
  onDecode: (el: HTMLElement, px: { width: number; height: number }) => void;
  onFrameBounds: () => void;
  onSeeked: (video: HTMLVideoElement) => void;
}): MediaPairs;
