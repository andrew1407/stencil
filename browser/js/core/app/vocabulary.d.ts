// The shapes DrawingApp's fields and methods take: hits, drags, anchors, the load and open
// options, the launch payload and the modal prompts' options. Types only — no module stands
// behind this file.
import type { Point, CropRect } from '../geometry.js';
import type { CodecLine } from '../line/linesCodec.js';

export type Line = CodecLine;
export type CompareMode = 'none' | 'original' | 'vertical' | 'horizontal';
export type ImageFilter = 'none' | 'bw' | 'sepia' | 'invert' | 'contour' | 'custom';
export type DrawModeKind = 'line' | 'rect';
export type Unit = 'cm' | 'in';

/** A point hit with its owning line; lineIdx -1 = the in-progress line. */
export interface PointHit { lineIdx: number; ptIdx: number; point: Point; }
export interface SegmentHit { lineIdx: number; ptIdx1: number; ptIdx2: number; }
export interface DragPoint { lineIdx: number; ptIdx: number; point?: Point; }
export interface DragSegment {
  lineIdx: number; ptIdx1: number; ptIdx2: number;
  startX: number; startY: number; origPt1: Point; origPt2: Point;
}
export interface DragLine { lineIdx: number; startX: number; startY: number; origPoints: Point[]; }
export interface RectAnchor { imgX: number; imgY: number; cssX: number; cssY: number; }
export interface CanvasCoords { cssX: number; cssY: number; x: number; y: number; }
export interface PageDims { width: number; height: number; }

/** A server project as the projects list rows carry it (listing.js). */
export interface RemoteProjectRow {
  serverUrl: string; id: string; name?: string; source?: string; version?: number;
}

/** The `#stencil=` fragment payload both Open in… hand-offs answer with. */
export interface LaunchPayload {
  server?: { url: string; id: string; version: number };
  dataUrl?: string | null;
  name?: string;
  layout?: Record<string, unknown>;
  source?: string;
  resource?: string;
  incognito?: boolean;
}

/** Per-source options the Open Image dialog hands a load (openImageHere / openImageNewTab). */
export interface OpenImageOptions {
  crop?: CropRect;
  noCrop?: boolean;
  source?: string;
  resource?: string;
  landing?: boolean;
  from?: Point;
}

export interface LoadImageOptions extends OpenImageOptions {
  address?: string;
  remoteId?: string;
  version?: number;
  layout?: Record<string, unknown>;
  color?: string;
  name?: string;
  blankColor?: string;
  replaceInPlace?: boolean;
  rename?: boolean;
  keepAnnotations?: boolean;
  keepZoom?: boolean;
}

export interface ConfirmOptions {
  title?: string; confirmLabel?: string; cancelLabel?: string; danger?: boolean; confirmIcon?: string;
}
export interface ChooseOptions extends ConfirmOptions { options?: Array<{ value: string; label: string }>; }
export interface AskAltOptions { title?: string; confirmLabel?: string; altLabel?: string; }
export interface PromptOptions { title?: string; confirmLabel?: string; defaultValue?: string; }

/** The session gathered for projectFile.buildProjectFile (projectFileIO.projectFileState). */
export interface ProjectFileState {
  name: string; color: string; keywords: string[]; source: string; resource: string;
  blank: boolean; blankColor: string; layout: Record<string, unknown>;
  image?: { dataUrl: string; ext: string; w: number; h: number };
  theme?: { mode: 'dark' | 'light'; accent: string };
}

/** llm/persistence.js's controller, installed on the app by wireChatPersistence. */
export interface ChatPersistence {
  projectOpened(id: string): Promise<void>;
  projectRemoved(id: string): Promise<void>;
  allProjectsCleared(): Promise<void>;
  flush(): Promise<void>;
}
