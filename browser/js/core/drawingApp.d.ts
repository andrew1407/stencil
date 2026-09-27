// DrawingApp: the orchestrator owning editor state + DOM wiring. Every collaborator takes
// the app and reaches back through it; the plain-value fields come from editorState.js. The
// wired collaborators are declared beside the module that installs them
// (remote/collaborators.d.ts) and merge into the class; a core function is called by its module,
// never through the app.
import type { Point, CropRect } from './geometry.js';
import type { RemoteLink } from './remote/syncController.js';
import type { ConnectionManager } from '../net/connectionManager.js';
import type { Snapshot } from './historyStack.js';
import type { Gesture } from './pointer/gesture.js';
import type { AppCollaborators } from './remote/collaborators.js';
import type {
  Line, CompareMode, ImageFilter, DrawModeKind, Unit, PointHit, SegmentHit, DragPoint, DragSegment,
  DragLine, RectAnchor, LaunchPayload, OpenImageOptions, ConfirmOptions, ChooseOptions, AskAltOptions,
  PromptOptions, ChatPersistence,
} from './app/vocabulary.js';

export declare class DrawingApp {
  constructor();

  // ── DOM ──
  canvas: HTMLCanvasElement;
  ctx: CanvasRenderingContext2D;
  tooltip: HTMLElement & { app: DrawingApp };
  coordinatesBody: HTMLElement;

  // ── Late collaborators + name editor ──
  /** Created lazily by the window.stencil facade (console/stencilApi.js). */
  connections?: ConnectionManager;
  /** Installed by llm/chat/persistence.js wireChatPersistence. */
  chatPersistence?: ChatPersistence;
  nameEditor: { refresh(): void } | null;
  nameEditing: boolean;

  // ── Image + provenance ──
  image: HTMLCanvasElement | HTMLImageElement | null;
  originalImage: HTMLImageElement | HTMLCanvasElement | null;
  cropRect: CropRect | null;
  rotationQuarters: 0 | 1 | 2 | 3;
  imageSource: string | null;
  imageResource: string | null;
  imageDataUrl?: string | null;
  imageBaseName?: string | null;
  imageExt?: string | null;
  blankColor: string;
  fromFile: boolean;
  filterDirty: boolean;

  // ── Lines + drawing ──
  lines: Line[];
  currentLine: Line | null;
  isDrawing: boolean;
  undonePoints?: Point[];
  drawMode: DrawModeKind;
  isRectDrawDragging: boolean;
  rectDrawStart: RectAnchor | null;
  rectDrawEnd: RectAnchor | null;
  continueLineIdx: number;
  continueInsertIdx: number;
  holdDrawDelay: number;
  holdPreview: Point | null;

  // ── Viewport, pan, drags ──
  scale: number;
  canvasRect?: DOMRect | null;
  canvasRectAt?: string;
  /** The pointer gesture in flight; each is* drag flag below is an accessor over it. */
  gesture: Gesture;
  isPanning: boolean;
  draggingPoint: DragPoint | null;
  isDraggingPoint: boolean;
  dragJustEnded: boolean;
  isDraggingSegment: boolean;
  draggingSegment: DragSegment | null;
  isDraggingLine: boolean;
  draggingLine: DragLine | null;
  isZoomRectDragging: boolean;
  zoomRectStart: RectAnchor | null;
  zoomRectEnd: RectAnchor | null;

  // ── Selection, coord table, hover ──
  selectedLineIdx: number;
  selectedLines: number[];
  coordLineIdx: number;
  hoveredPtIdx: number;
  focusedPtIdx: number;
  hoverPt: { lineIdx: number; ptIdx: number } | null;
  hoverLineIdx: number;
  listHoverLineIdx: number;
  mouseOverCanvas: boolean;
  lastMouseClientX: number;
  lastMouseClientY: number;

  // ── Style + view settings ──
  color: string;
  pointColor: string;
  thickness: number;
  pointSize: number;
  style: string;
  showPoints: boolean;
  showLines: boolean;
  imageFilter: ImageFilter;
  filterColor: string;
  compareMode: CompareMode;
  compareSplit: number;
  compareHoldOriginal: boolean;
  pageSize: string;
  customPageWidth: number;
  customPageHeight: number;
  tooltipEnabled: boolean;
  tooltipShowPage: boolean;
  tooltipShowScreen: boolean;
  tooltipShowCoords: boolean;
  allowFormulas: boolean;
  formulaX: string;
  formulaY: string;
  unit: Unit;
  selGlowColor: string;
  hoverRingColor: string;
  focusRingColor: string;
  defaultFillColor: string;

  // ── Project session ──
  activeProjectId: string | null;
  remoteLink: RemoteLink | null;
  pendingRemoteAddress: string | null;
  pendingOpenProjectId: string | null;
  pendingLaunchScript: string;
  hasExternalLaunch: boolean;
  openInConfig: { desktopScheme: string; telegramBotUsername: string };

  // ── Theme + accent (writes live in AccentController) ──
  readonly theme: 'dark' | 'light';
  readonly accent: string;
  readonly customAccent: string | null;

  // ── Boot + wiring ──
  initEventListeners(): void;
  applyProjectDeepLink(): boolean;
  importExternalImage(payload: LaunchPayload & Record<string, unknown>, opts?: { mode?: 'new' | 'replace' | 'replace-keep' }): Promise<void>;
  openInDesktopAvailable(): boolean;
  openInTelegramAvailable(): boolean;
  openInAvailable(): boolean;

  // ── Compare view + geometry ──
  compareReadOnly(): boolean;
  compareShowsPoint(x: number, y: number): boolean;
  updateCoordStatus(x: number, y: number): void;
  applyUnitToUI(): void;

  // ── Loading + files ──
  loadJSONFromFile(file: File, opts?: { from?: Point }): void;
  openImageNewTab(file: File, incognito?: boolean, opts?: OpenImageOptions): void;

  // ── Draw mode face ──
  syncDrawModeUI(): void;

  // ── Selection (ui/panel/selectionPanel.js, ui/panel/linesList.js) ──
  applyLinesListHover(): void;
  showSelectionPanel(line: Line): void;
  hideSelectionPanels(): void;
  deselectLine(redraw?: boolean): void;
  unchainSelectedLine(): void;

  // ── Hit tests (hitTest.js) ──
  findLineAt(x: number, y: number, threshold?: number): number;
  findNearestPoint(x: number, y: number, threshold?: number): Point | null;
  findNearestPointWithIdx(x: number, y: number, threshold?: number): PointHit | null;
  findNearestSegmentWithIdx(x: number, y: number, threshold?: number): SegmentHit | null;
  adjustThicknessAtCursor(e: WheelEvent): boolean;

  // ── Drag gestures ──
  beginPullOutDrag(x: number, y: number): boolean;

  // ── Edits ──
  clearAllLines(): Promise<void>;

  // ── History + status ──
  /** Pushes the current lines, crop, turn and filter as one undo step. */
  saveHistory(): void;
  undo(): void;
  redo(): void;
  /** Applies an undo step: its lines, and a memento's crop, turn and filter when they differ. */
  restoreHistoryStep(step: Snapshot): void;
  hasEditingSession(): boolean;
  /** The full control sweep (ui/control/state.js); an edit signals app.changes instead. */
  updateButtons(): void;
  updateProjectTitle(force?: boolean): void;
  updateInfo(): void;
  showSaveStatus(msg: string, color: string): void;

  // ── Modal prompts (ui/modal/confirmModal.js; native / first-option fallbacks without a DOM) ──
  confirm(message: string, opts?: ConfirmOptions): Promise<boolean>;
  choose(message: string, opts?: ChooseOptions): Promise<string | null>;
  askAlt(message: string, opts?: AskAltOptions): Promise<'confirm' | 'alt' | null>;
  prompt(message: string, opts?: PromptOptions): Promise<string | null>;

  // ── Project lifecycle ──
  updateIncognitoUI(): void;
  closeProject(id: string | null, opts?: { fully?: boolean }): this;
}

/** The collaborators wireCollaborators sets. */
export interface DrawingApp extends AppCollaborators {}
