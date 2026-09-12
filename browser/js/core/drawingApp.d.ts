// DrawingApp: the orchestrator owning editor state + DOM wiring. Every collaborator takes
// the app and reaches back through it; the plain-value fields come from editorState.js, the
// behaviour behind each delegator from the module its section names. Nothing here is a
// second implementation — the window.stencil facade and the toolbar call these same names.
import type { CodecLine } from './linesCodec.js';
import type { Storage } from './storage.js';
import type { Renderer } from './renderer.js';
import type { TabsCoordinator } from './tabsCoordinator.js';
import type { RemoteSyncController, RemoteLink } from './remoteSyncController.js';
import type { ProjectTransferController } from './projectTransferController.js';
import type { ProjectMeta } from './projectsStore.js';
import type { ConnectionManager } from '../net/connectionManager.js';
import type { HistoryStack } from './historyStack.js';
import type { FormulaEngine } from './formulaEngine.js';
import type { StrokeFx } from './strokeFx.js';
import type { ExportService } from './exportService.js';
import type { SettingsController } from './settingsController.js';
import type { ImageModel } from './imageModel.js';
import type { StencilSync } from './stencilSync.js';
import type { InputController } from './inputController.js';
import type { PointerController } from './pointerController.js';
import type { ZoomPan } from './zoomPan.js';

export type Point = { x: number; y: number };
export type Line = CodecLine;
/** A crop in rotated-original pixels — the canonical spelling ImageModel.roundRect emits. */
export interface CropRect { x: number; y: number; width: number; height: number; }
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

/** A server project as the projects list rows carry it (remoteListing.js). */
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

/** llm/chatPersistence.js's controller, installed on the app by wireChatPersistence. */
export interface ChatPersistence {
  projectOpened(id: string): Promise<void>;
  projectRemoved(id: string): Promise<void>;
  allProjectsCleared(): Promise<void>;
  flush(): Promise<void>;
}

/** The two ui/ collaborators the app holds (ui/coordTable.js, ui/accentController.js). */
export interface CoordTableLike { update(points?: Point[], lineIdx?: number): void; }
export interface AccentControllerLike {
  applyAccent(key: string): string;
  setTheme(theme: string, originEl?: Element | null): void;
  setAccent(key: string, originEl?: Element | null): void;
  setCustomAccent(hex: string, originEl?: Element | null): string | null;
  previewAccent(key: string, originEl?: Element | null): void;
  endAccentPreview(originEl?: Element | null): void;
}

export declare class DrawingApp {
  constructor();

  // ── DOM ──
  canvas: HTMLCanvasElement;
  ctx: CanvasRenderingContext2D;
  tooltip: HTMLElement & { app: DrawingApp };
  tooltipMgr: HTMLElement & { app: DrawingApp };
  coordinatesBody: HTMLElement;

  // ── Collaborators (wired in dependency order) ──
  history: HistoryStack;
  formula: FormulaEngine;
  renderer: Renderer;
  strokeFx: StrokeFx;
  storage: Storage;
  tabs: TabsCoordinator;
  coordTable: CoordTableLike;
  export: ExportService;
  settings: SettingsController;
  accents: AccentControllerLike;
  imageModel: ImageModel;
  remoteSync: RemoteSyncController;
  projectTransfer: ProjectTransferController;
  stencilSync: StencilSync;
  input: InputController;
  pointer: PointerController;
  zoomPan: ZoomPan;
  /** Created lazily by the window.stencil facade (console/stencilApi.js). */
  connections?: ConnectionManager;
  /** Installed by llm/chatPersistence.js wireChatPersistence. */
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
  hasExternalLaunch: boolean;
  openInConfig: { desktopScheme: string; telegramBotUsername: string };

  // ── Theme + accent (writes live in AccentController) ──
  readonly theme: 'dark' | 'light';
  readonly accent: string;
  readonly customAccent: string | null;
  setTheme(theme: string, originEl?: Element | null): void;
  setAccent(key: string, originEl?: Element | null): void;
  setCustomAccent(hex: string, originEl?: Element | null): string | null;
  previewAccent(key: string, originEl?: Element | null): void;
  endAccentPreview(originEl?: Element | null): void;

  // ── Boot + wiring ──
  initEventListeners(): void;
  restoreFromLocalStorage(): void;
  applyProjectDeepLink(): boolean;
  applyExternalLaunch(): void;
  importExternalImage(payload: LaunchPayload & Record<string, unknown>, opts?: { mode?: 'new' | 'replace' | 'replace-keep' }): Promise<void>;
  openInDesktopAvailable(): boolean;
  openInTelegramAvailable(): boolean;
  openInAvailable(): boolean;

  // ── Compare view + geometry ──
  compareReadOnly(): boolean;
  compareShowsPoint(x: number, y: number): boolean;
  nearCompareDivider(clientX: number, clientY: number): boolean;
  canvasCoords(clientX: number, clientY: number): CanvasCoords;
  getPageDimensions(): PageDims;
  pixelToPageCoords(x: number, y: number): Point;
  updateCoordStatus(x: number, y: number): void;
  applyUnitToUI(): void;

  // ── Loading + files ──
  loadImageFromFile(file: File, opts?: LoadImageOptions): void;
  loadJSONFromFile(file: File, opts?: { from?: Point }): void;
  openImageHere(file: File, incognito?: boolean, address?: string | null, opts?: OpenImageOptions): void;
  openImageNewTab(file: File, incognito?: boolean, opts?: OpenImageOptions): void;
  replaceProjectImage(file: File, opts?: { rename?: boolean; keepAnnotations?: boolean; crop?: CropRect | null }): void;
  createBlankImage(opts?: { color?: string; width?: number; height?: number; address?: string }): Promise<PageDims>;
  activeIsBlank(): boolean;
  setBlankColor(color: string): unknown;
  projectFileState(opts?: { includeTheme?: boolean }): ProjectFileState;
  applyProjectFile(project: Record<string, unknown>): Promise<string>;
  applyProjectFileInPlace(project: Record<string, unknown>, opts?: Record<string, unknown>): void;
  chooseFileConflict(name?: string): Promise<'theirs' | 'merge' | 'mine'>;
  updateStencilSyncUI(): void;
  currentLayoutPayload(): Record<string, unknown>;
  openInLaunchPayload(opts?: { incognito?: boolean; id?: string | null }): LaunchPayload | null;

  // ── Drawing mode + shapes (drawMode.js, shapeBuilder.js) ──
  startDrawingMode(opts?: Record<string, unknown>): void;
  stopDrawingMode(): void;
  setDrawMode(mode: DrawModeKind): void;
  syncDrawToggleUI(): void;
  syncDrawModeUI(): void;
  tryCloseShapeAt(x: number, y: number): boolean;
  insertPointOnSegment(lineIdx: number, insertIdx: number, x: number, y: number): void;
  createRect(x1: number, y1: number, x2: number, y2: number, connect?: boolean): void;

  // ── Selection (lineSelection.js, ui/selectionPanel.js, ui/linesList.js) ──
  selectedIndices(): number[];
  isLineSelected(i: number): boolean;
  updateMultiSelectStatus(): void;
  selectLineFromList(idx: number, ctrlShift?: boolean): unknown;
  applyLinesListHover(): void;
  renderLinesList(): void;
  setListHoverLine(idx: number): void;
  showSelectionPanel(line: Line): void;
  hideSelectionPanels(): void;
  applyFill(): void;
  syncFsSelectionPanel(line: Line): void;
  deselectEmptyArea(e?: MouseEvent | null): void;
  deselectLine(redraw?: boolean): void;
  applySelectionChange<K extends keyof Line>(prop: K, value: Line[K]): void;
  unchainSelectedLine(): void;

  // ── Canvas input (canvasClick.js, hoverController.js, hitTest.js) ──
  canvasClick(e: MouseEvent): void;
  canvasMouseMove(e: MouseEvent): void;
  canvasDblClick(e: MouseEvent): void;
  findLineAt(x: number, y: number, threshold?: number): number;
  findNearestPoint(x: number, y: number, threshold?: number): Point | null;
  findNearestPointWithIdx(x: number, y: number, threshold?: number): PointHit | null;
  findNearestSegmentWithIdx(x: number, y: number, threshold?: number): SegmentHit | null;
  adjustThicknessAtCursor(e: WheelEvent): boolean;

  // ── Drag gestures (dragGestures.js) ──
  beginSegmentDrag(nearSeg: SegmentHit, x: number, y: number): void;
  beginPullOutDrag(x: number, y: number): boolean;
  movePointTo(dp: DragPoint, x: number, y: number): void;
  endPointDrag(dp: DragPoint, altKey: boolean): void;
  endSegmentDrag(altKey: boolean): void;
  dragMove(clientX: number, clientY: number, shiftKey: boolean): void;
  finishDragGesture(altKey: boolean): void;

  // ── Transforms + edits (transformOps.js, lineEditOps.js) ──
  rotateSelectedLine(angle: number): void;
  flipSelectedLine(horizontal: boolean): void;
  rotateSelectedLineQuarter(dir: 1 | -1): void;
  nudgeSelected(dx: number, dy: number): unknown;
  setPointCoord(lineIdx: number, ptIdx: number, axis: 'x' | 'y', valuePx: number): unknown;
  removePoint(lineIdx: number, ptIdx: number): unknown;
  removeLine(idx: number): unknown;
  removeSelectedLines(): unknown;
  clearAllLines(): Promise<void>;

  // ── History + status ──
  saveHistory(): void;
  undo(): void;
  redo(): void;
  hasEditingSession(): boolean;
  updateButtons(): void;
  updateProjectTitle(force?: boolean): void;
  updateInfo(): void;
  showSaveStatus(msg: string, color: string, _iconName?: string | null): void;
  renderResultCanvas(): HTMLCanvasElement;

  // ── Modal prompts (ui/confirmModal.js; native / first-option fallbacks without a DOM) ──
  confirm(message: string, opts?: ConfirmOptions): Promise<boolean>;
  choose(message: string, opts?: ChooseOptions): Promise<string | null>;
  askAlt(message: string, opts?: AskAltOptions): Promise<'confirm' | 'alt' | null>;
  prompt(message: string, opts?: PromptOptions): Promise<string | null>;

  // ── Project lifecycle (ProjectTransferController delegators) ──
  newEditor(opts?: { keepChat?: boolean }): void;
  adoptIncognitoHere(): void;
  switchToProject(id: string): boolean;
  openProjectInNewTab(id: string | null, win?: Window | null): void;
  openRemoteProjectInNewTab(meta: RemoteProjectRow, win?: Window | null): void;
  openRemoteProject(meta: RemoteProjectRow): Promise<void>;
  createRemoteBlank(address: string): Promise<{ address: string }>;
  publishIncognitoToServer(address: string): Promise<RemoteLink>;
  promoteIncognitoToLocal(): string | null;
  canToggleIncognito(): boolean;
  updateIncognitoUI(): void;
  reportIncognitoSession(): void;
  renewProject(id: string): ProjectMeta | null;
  setProjectExpiration(id: string, opts?: { expiresAt?: number; refreshPeriod?: string; autoRefresh?: boolean }): ProjectMeta | null;
  closeProject(id: string | null, opts?: { fully?: boolean }): this;
  renameProject(id: string, name: string): ReturnType<ProjectTransferController['renameProject']>;
  setProjectColor(id: string, color: string): ReturnType<ProjectTransferController['setProjectColor']>;
  setProjectKeywords(id: string, keywords: string | string[]): ReturnType<ProjectTransferController['setProjectKeywords']>;
  setProjectDescription(id: string, description: string): ReturnType<ProjectTransferController['setProjectDescription']>;
  setProjectBlankColor(id: string, color: string): ReturnType<ProjectTransferController['setProjectBlankColor']>;
  removeProject(id: string): void;
  clearAllProjects(): void;
  moveProjectToServer(id: string, address: string): ReturnType<ProjectTransferController['moveProjectToServer']>;
  copyProjectToServer(id: string, address: string, opts?: { name?: string }): ReturnType<ProjectTransferController['copyProjectToServer']>;
  moveProjectToLocal(meta: RemoteProjectRow): ReturnType<ProjectTransferController['moveProjectToLocal']>;
  copyServerProjectToLocal(meta: RemoteProjectRow, opts?: { name?: string }): ReturnType<ProjectTransferController['copyServerProjectToLocal']>;
  copyServerProjectToIncognito(meta: RemoteProjectRow, opts?: { newTab?: boolean }): ReturnType<ProjectTransferController['copyServerProjectToIncognito']>;
}
