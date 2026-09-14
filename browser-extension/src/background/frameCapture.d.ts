// Shapes for background/frameCapture.js — the three routes to a still of the
// right-clicked <video>, tried in order by ctxActions.js.

/** A same-origin/CORS in-page draw succeeded, or it fell back to `{src,t}` for a
 * caller-side re-fetch when the canvas came back tainted. */
export type VideoFrameResult = { frame: string } | { src: string; t: number } | null;

export declare const captureFrameFromScreenshot: (
  windowId: number,
  rect: { x: number; y: number; width: number; height: number },
  dpr?: number,
) => Promise<string>;
export declare const captureVideoFrameInTab: (
  tabId: number | null,
  frameId: number | null | undefined,
  point?: { x: number; y: number } | null,
) => Promise<VideoFrameResult>;
export declare const captureVideoFrameViaFetch: (
  tabId: number | null,
  frameId: number | null | undefined,
  src: string,
  t: number,
  pageUrl?: string,
) => Promise<string | null>;
