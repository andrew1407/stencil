export interface FrameScrub {
  /** The Frame field's value in seconds — the field itself counts frames. */
  frameSeconds: () => number;
  /** Re-cap the field and the bar at the clip's last frame. */
  syncFrameBounds: () => void;
  /** Redraw the bar's fill and span it across the picture above it. */
  syncScrub: () => void;
  /** A landed seek names the frame on screen, unless a newer seek is already queued. */
  showLandedFrame: (el: HTMLVideoElement) => void;
}

export declare function createFrameScrub(args: {
  frameEl: HTMLInputElement;
  scrubEl: HTMLInputElement;
  video: () => HTMLVideoElement;
  cropMedia: () => HTMLElement;
  isVideoSource: () => boolean;
}): FrameScrub;
