// Shapes for background/tabState.js — per-tab probe state, plus the in-memory pins
// snapshot the CTX handler relabels the menu from (must be synchronous, see pins.js).

/** What the ctxTarget probe last resolved under the cursor for one tab. */
export interface TargetRecord {
  url?: string;
  imgUrl?: string;
  video?: boolean;
  poster?: string;
  videoUrl?: string;
}

/** The last right-clicked <video> for one tab, so the click handler can recapture it. */
export interface VideoRecord {
  frameId?: number | null;
  point?: { x: number; y: number } | null;
  rect?: { x: number; y: number; width: number; height: number } | null;
  dpr?: number;
  posterShown?: boolean;
}

/** One entry of the pinned-images store (lib/pins.js). */
export interface PinEntry {
  source: string;
  site: string;
  resource: string;
  name: string;
  kind: string;
  t: number;
  color?: string;
  keywords?: string[];
}

export declare const lastTargetByTab: Map<number, TargetRecord | null>;
export declare const lastVideoByTab: Map<number, VideoRecord | null>;
export declare const lastPosterByTab: Map<number, string>;
/** Kept fresh from storage; read synchronously to beat the native menu appearing. */
export declare const pinsCache: PinEntry[];
