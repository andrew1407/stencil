// Shapes for lib/scan.js — the ScanEntry that flows through popup / editorMode /
// assistant / the page API, and is described in three READMEs. One entry per image or
// video the injected scanner found, deduped by `src` (or by `videoUrl` for a frameless
// video) across frames.

/** What kind of media the entry stands for. */
export type ScanKind = 'img' | 'video';

/** One scanned page resource, as the scanner emits it. */
export interface ScanEntry {
  /** Absolute URL of the still image (a video's poster or captured frame); '' when there is none. */
  src: string;
  kind: ScanKind;
  /** Natural width/height in CSS pixels; 0 when the page never laid it out. */
  w: number;
  h: number;
  /** alt text, aria-label, or the CSS property the url() came from — the row's caption. */
  alt: string;
  /** kind 'video': the media URL itself (http(s) only; '' for blob:/MSE sources). */
  videoUrl?: string;
  /** kind 'video': the poster attribute, and whether `src` is a captured frame. */
  posterUrl?: string;
  poster?: string;
  hasFrame?: boolean;
  /** Page furniture (favicons, og:image, manifest icons) — gated by "Icons & metadata". */
  meta?: boolean;
}

/** A ScanEntry once a surface has attributed it: where it came from, and its display name. */
export interface AttributedScanEntry extends ScanEntry {
  /** The tab the scan ran in. */
  tabId?: number;
  sourceTabId?: number;
  /** The scanned page's own URL — the provenance an import records. */
  resource?: string;
  /** Derived from the URL for display (lib/displayName.js). */
  name?: string;
}

export declare const MAX_IMAGES: number;
export declare const BLOCKED_SCHEMES: string[];
export declare function mergeScanFrames(
  results: Array<{ result?: ScanEntry[] } | null | undefined> | null | undefined,
  limit?: number,
): ScanEntry[];
/** Runs INJECTED in the scanned page (chrome.scripting), so it closes over nothing. */
export declare function scanPageForImages(limit: number): Promise<ScanEntry[]>;
