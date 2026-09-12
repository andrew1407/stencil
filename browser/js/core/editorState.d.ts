// The editor's plain-data defaults: every DrawingApp field that is a value, not a
// collaborator or a DOM node. A fresh object per call — nothing is shared between editors.
import type { CodecLine } from './linesCodec.js';

export interface Point { x: number; y: number; }
export interface CropRect { x: number; y: number; width: number; height: number; }
/** A drag anchor in both image and CSS pixels. */
export interface DualPoint { imgX: number; imgY: number; cssX: number; cssY: number; }
export type ImageFilter = 'none' | 'bw' | 'sepia' | 'invert' | 'contour' | 'custom';
export type CompareMode = 'none' | 'original' | 'vertical' | 'horizontal';
export type Unit = 'cm' | 'in';
/** The stroke being drawn: a CodecLine minus the fields it gains on commit. */
export type CurrentLine = Pick<CodecLine, 'points' | 'color' | 'pointColor' | 'thickness' | 'pointSize' | 'style'>
  & Partial<Pick<CodecLine, 'locked' | 'fillColor'>>;
export interface RemoteLink { address: string; remoteId: string; version: number; }

export interface EditorState {
  image: HTMLImageElement | HTMLCanvasElement | null;
  originalImage: HTMLImageElement | HTMLCanvasElement | null;
  /** In original-image pixels; line/point coords are crop-local. */
  cropRect: CropRect | null;
  /** Clockwise quarter turns (0..3) applied to originalImage before cropping. */
  rotationQuarters: number;
  imageSource: string | null;
  imageResource: string | null;
  /** '#rrggbb' for a blank project (recolourable), '' for an ordinary image. */
  blankColor: string;
  fromFile: boolean;
  lines: CodecLine[];
  currentLine: CurrentLine | null;
  isDrawing: boolean;
  scale: number;
  isPanning: boolean;
  draggingPoint: { lineIdx: number; ptIdx: number; point?: Point } | null;
  isDraggingPoint: boolean;
  dragJustEnded: boolean;
  isDraggingSegment: boolean;
  draggingSegment: { lineIdx: number; ptIdx1: number; ptIdx2: number; startX: number; startY: number;
    origPt1: Point; origPt2: Point; origPoints: Point[] } | null;
  isDraggingLine: boolean;
  draggingLine: { lineIdx: number; startX: number; startY: number; origPoints: Point[];
    multiOrig?: { li: number; pts: Point[] }[] | null } | null;
  isZoomRectDragging: boolean;
  zoomRectStart: DualPoint | null;
  zoomRectEnd: DualPoint | null;
  coordLineIdx: number;
  hoveredPtIdx: number;
  focusedPtIdx: number;
  color: string;
  /** '' = follow `color` (core's Line::pointColor fallback). */
  pointColor: string;
  thickness: number;
  pointSize: number;
  style: string;
  showPoints: boolean;
  showLines: boolean;
  imageFilter: ImageFilter;
  filterColor: string;
  compareMode: CompareMode;
  /** Divider position 0..1 for the split compare modes. */
  compareSplit: number;
  compareHoldOriginal: boolean;
  pageSize: string;
  customPageWidth: number;
  customPageHeight: number;
  selectedLineIdx: number;
  /** Multi-select set; empty in single-select mode. */
  selectedLines: number[];
  tooltipEnabled: boolean;
  tooltipShowPage: boolean;
  tooltipShowScreen: boolean;
  tooltipShowCoords: boolean;
  allowFormulas: boolean;
  formulaX: string;
  formulaY: string;
  unit: Unit;
  holdDrawDelay: number;
  holdPreview: Point | null;
  drawMode: 'line' | 'rect';
  isRectDrawDragging: boolean;
  rectDrawStart: DualPoint | null;
  rectDrawEnd: DualPoint | null;
  continueLineIdx: number;
  continueInsertIdx: number;
  hoverPt: { lineIdx: number; ptIdx: number } | null;
  hoverLineIdx: number;
  listHoverLineIdx: number;
  mouseOverCanvas: boolean;
  lastMouseClientX: number;
  lastMouseClientY: number;
  selGlowColor: string;
  hoverRingColor: string;
  focusRingColor: string;
  defaultFillColor: string;
  /** Mirrors storage.activeId; null = temporary editor. */
  activeProjectId: string | null;
  remoteLink: RemoteLink | null;
  pendingRemoteAddress: string | null;
}

export declare const createEditorState: () => EditorState;
