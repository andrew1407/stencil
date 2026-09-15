// DrawingApp.loadImageFromFile's body: the session bookkeeping a load does up front
// (project promotion, filename, provenance, server linkage), then the decode; what the
// decoded image settles is imageSettle.js.
import type { DrawingApp } from './drawingApp.js';
import type { CodecLine } from './linesCodec.js';

/** A crop rect in rotated-original pixels; `w`/`h` is the wire spelling, `width`/`height` the legacy one. */
export interface CropRectInput { x: number; y: number; w?: number; h?: number; width?: number; height?: number; }

/** The stored layout of a reopened server project or an adopted hand-off. */
export interface RemoteLayout {
  rotationQuarters?: number;
  cropRect?: CropRectInput;
  lines?: Partial<CodecLine>[];
  [field: string]: unknown;
}

export interface LoadImageOptions {
  /** Swap the active project's image, keeping its identity, file link and server link. */
  replaceInPlace?: boolean;
  keepAnnotations?: boolean;
  rename?: boolean;
  /** Explicit project name (extension copy-numbering) instead of the filename base. */
  name?: string;
  source?: string | null;
  resource?: string | null;
  /** With remoteId: reopen an existing server project; alone: create on that server after load. */
  address?: string | null;
  remoteId?: string | null;
  version?: number;
  layout?: RemoteLayout | null;
  adoptLayout?: boolean;
  page?: string;
  album?: boolean;
  crop?: CropRectInput | null;
  noCrop?: boolean;
  blankColor?: string | null;
  fromFile?: boolean;
  keepZoom?: boolean;
  landing?: boolean;
  from?: { x: number; y: number } | Element | null;
  color?: string | null;
  keywords?: string[];
}

/** What the first half worked out for imageSettle.js. */
export interface LoadPlan {
  replaceInPlace: boolean;
  keptLines: Partial<CodecLine>[] | null;
  oldImageSource: string | null;
  oldImageResource: string | null;
  remoteCreateAddress: string | null;
  remoteLayout: RemoteLayout | null;
}

export declare const waitForImage: (app: DrawingApp, opts?: { timeoutMs?: number; previous?: unknown }) => Promise<void>;
export declare const loadImageFromFile: (app: DrawingApp, file: File, opts?: LoadImageOptions) => void;
