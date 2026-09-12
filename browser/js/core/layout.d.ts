// Pure layout helpers: serialisation through the LAYOUT_FIELDS table, cropRect wire
// spellings, the co-edit union merge, hardening of untrusted `lines`, and the small
// geometry-edit decisions. Never touches DOM or app state.
import type { CodecLine } from './linesCodec.js';

/** One row of config/layoutFields.json; array order IS the session payload's byte order. */
export interface LayoutField {
  key: string;
  /** 0-based position in the export/server subset; absent = session-only. */
  export?: number;
  /** Emit even when undefined; otherwise emitted only when `!= null`. */
  exportForce?: boolean;
}

/** The canonical wire spelling; readers also accept the legacy {width,height}. */
export interface WireCropRect { x: number; y: number; w?: number; h?: number; width?: number; height?: number; }
/** The app's internal spelling. */
export interface CropRect { x: number; y: number; width: number; height: number; }

/** A layout as it crosses a wire: the export subset of LAYOUT_FIELDS plus `lines`. */
export interface LayoutPayload {
  lines?: CodecLine[];
  imageWidth?: number;
  imageHeight?: number;
  cropRect?: WireCropRect | null;
  [field: string]: unknown;
}

export interface LayoutValidation {
  ok: boolean;
  reason?: 'no-image';
  needsReplaceConfirm: boolean;
  needsDimMismatchConfirm: boolean;
  lines: CodecLine[];
}

export declare const LAYOUT_FIELDS: readonly LayoutField[];
/** Every table field in table order, from a plain state object. */
export declare const serializeSession: (state: Record<string, unknown>) => Record<string, unknown>;
/** → the app's internal {x,y,width,height}; null for non-objects. */
export declare const normalizeCropRect: (r: WireCropRect | null | undefined) => CropRect | null;
/** The export subset in its own key order; cropRect leaves as {x,y,w,h}. */
export declare const buildLayoutPayload: (src: Record<string, unknown>) => LayoutPayload;
/** Server lines first, then any local line not already present (order-independent key). */
export declare const mergeLines: (serverLines: readonly CodecLine[] | null | undefined, localLines: readonly CodecLine[] | null | undefined) => CodecLine[];
/** Rebuilds each line from a field whitelist onto a fresh object; caps lines and points. */
export declare const sanitizeLines: (rawLines: unknown) => CodecLine[];
export declare const validateLayout: (
  data: unknown,
  ctx: { hasImage: boolean; imgW: number; imgH: number; hasExistingLines: boolean },
) => LayoutValidation;
/** Just after the focused point when it belongs to the shown line, else append. */
export declare const resolveInsertIdx: (
  line: { points: readonly unknown[] },
  sel: { coordLineIdx: number; selectedLineIdx: number; focusedPtIdx: number },
) => number;
/** The page (cm) at `dpi`, at least 1px per side — mirrors core::defaultBlankSizePx. */
export declare const defaultBlankSizePx: (page: { width: number; height: number }, dpi?: number) => { width: number; height: number };
export declare const fillState: (line: { fillColor?: string }, defaultFillColor: string | null | undefined) => { enabled: boolean; value: string };
