export interface PreviewDust {
  /** Whether this source has an arrival cloud left to play. */
  willFly: () => boolean;
  /** Hide the element up front, so its cloud is what the eye sees arrive. */
  preVeilIfNew: (el: HTMLElement) => void;
  /** Assemble the picture out of motes, once per source per session. */
  gather: () => void;
  /** Blow the picture on screen away, and promise an arrival for whatever lands next. */
  scatter: (el: HTMLElement, size: { width: number; height: number }) => void;
  /** Run `fn` once the departure cloud has landed. */
  queueArrival: (fn: () => void) => void;
  /** Finish any stage and unveil both media — the overlay's own sweep cannot reach it. */
  cancel: () => void;
  /** Forget which sources have flown (a fresh open). */
  reset: () => void;
}

export declare function createPreviewDust(args: {
  img: () => HTMLImageElement;
  video: () => HTMLVideoElement;
  isVideo: () => boolean;
  sourceKey: () => string;
}): PreviewDust;
