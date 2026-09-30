// window.stencil — the console control API (README "Console API"). A frozen, hard-guarded
// facade over the live DrawingApp: every mutation routes through the same core methods
// the toolbar uses, most calls return the facade (or a Project / Line / Point) to chain.
import type { DrawingApp } from '../core/drawingApp.js';
import type { CodecLine } from '../core/line/linesCodec.js';
import type { CopyProjectRequest } from '../core/project/copy/options.js';
import type { LayoutPayload, WireCropRect } from '../core/layout.js';
import type { ConnectSpec } from '../net/connectionManager.js';
import type { TaggedRemoteProject } from '../net/serverConnection.js';
import type { RemoteProjectMeta } from '../core/project/transferController.js';
import type {
  TooltipSections, Point, Line, Project, LlmFacade, ChatTurnResult, ChatFacade, CropSpec,
  LoadOptions, ApplyOptions, LayoutInstallOptions, ExtensionEditorApi,
} from './apiShapes.js';
export { WINDOWS } from './api/windowsApi.js';
export interface XY { x: number; y: number; }
export interface Size { width: number; height: number; }

/** Any CSS colour on write, normalised to hex on read. */
export type ColorInput = string;
export type LineStyle = 'solid' | 'dashed' | 'dotted';
export type ImageFilter = 'none' | 'bw' | 'sepia' | 'invert' | 'contour' | 'custom';
export type CompareMode = 'none' | 'original' | 'vertical' | 'horizontal';
export type Unit = 'cm' | 'mm' | 'in';
export type DrawMode = 'line' | 'rect';
export type MotionMode = 'particles' | 'water' | 'fire' | 'slide' | 'none';
export type NotifyChannel = 'toast' | 'system';
export type ExportVariant = 'current' | 'original' | 'tint' | 'split';
export type ChatDock = 'left' | 'right' | 'top' | 'bottom' | 'float';
/** A logo show (logoStage.json) as a call, `<show>Mode` its on/off switch, `close`, `what` the words. */
export type EasterEggsFacade = { readonly [show: string]: (() => Stencil) | boolean } & { [mode: `${string}Mode`]: boolean }
  & { readonly what: () => readonly string[]; readonly of: (word: string) => Stencil; readonly close: () => Stencil };

export interface StencilSettings {
  lineColor: ColorInput;
  /** '' = points follow lineColor. */
  pointColor: ColorInput;
  thickness: number;
  pointSize: number;
  lineStyle: LineStyle;
  /** Alias of showPoints. */
  pointStyle: boolean;
  showPoints: boolean;
  showLines: boolean;
  filter: ImageFilter;
  compareMode: CompareMode;
  /** Divider position 0..1 for the split modes. */
  compareSplit: number;
  filterColor: ColorInput;
  unit: Unit;
  /** Case-insensitive ISO name ('a3', 'B5', 'c10') or 'custom'. */
  pageSize: string;
  /** cm; applies when pageSize === 'custom'. */
  pageWidth: number;
  pageHeight: number;
  darkTheme: boolean;
  /** A preset key persists + syncs; a hex is a custom accent for THIS page only. */
  mainTheme: string;
  readonly mainThemes: string[];
  /** Active project's accent colour; '' = neutral. Throws without an active project. */
  projectColor: string;
  description: string;
  /** Read: string[]. Write an array entry per keyword, or one comma/newline separated string. */
  keywords: string[] | string;
  drawMode: DrawMode;
  /** ms, clamped 100–3000. */
  holdDrawDelay: number;
  allowFormulas: boolean;
  formulaX: string;
  formulaY: string;
  drawingAnimations: boolean;
  motionMode: MotionMode;
  readonly motionModes: MotionMode[];
  /** Where a notice shows: in the app, or the browser's own once its permission is granted. */
  notifyChannel: NotifyChannel;
  fillColor: ColorInput;
  selectionGlow: ColorInput;
  hoverRing: ColorInput;
  focusRing: ColorInput;
  /** 'default' (English) or a BCP-47 tag. */
  voiceInputLanguage: string;
  /** ms, clamped 500–10000. */
  voiceSilenceMs: number;
}

export type {
  TooltipSections, Point, Line, ExpiryInput, Project, LlmFacade, ChatTurnResult, ChatFacade,
  CropSpec, LoadOptions, ApplyOptions, LayoutInstallOptions, ExtensionEditorApi,
} from './apiShapes.js';

export interface Stencil extends StencilSettings {
  readonly extension: ExtensionEditorApi | null;
  readonly settings: StencilSettings;
  fullscreen: boolean;
  /** Turning on needs a blank editor; turning off with work on screen promotes it to a project. */
  incognito: boolean;
  readonly imageSize: Size | undefined;
  /** Rotated-original px, {x,y,w,h} plus width/height aliases; null before an image loads. */
  readonly cropRect: (Required<WireCropRect>) | null;
  promoteIncognito(): string | null;
  readonly tooltip: TooltipSections;
  readonly lines: Line[];
  /** The current line's points: in-progress, else coord-table line, else last line. */
  readonly points: Point[];
  readonly shortcuts: Record<string, string>;
  /** `oldRef` is an action id or its current combo. */
  changeShortcut(oldRef: string, newCombo: string): Stencil;

  // Projects
  readonly current: Project | null;
  readonly openedProjects: Project[];
  readonly archivedProjects: Project[];
  readonly incognitoProjects: Project[];
  getProjects(opts?: { archived?: boolean; incognito?: boolean }): Project[];
  getProjectByName(name: string): Project | null;
  getProjectsByKeyword(...keywords: (string | string[])[]): Project[];
  expire(spec?: string): Project | string;
  /** "Make a copy" of the live editor: the new local project, or null for an incognito or server copy. */
  copyProject(opts: CopyProjectRequest): Promise<Project | null>;

  // Servers
  connect(urlOrUrls: ConnectSpec | ConnectSpec[]): Promise<Stencil>;
  disconnect(url?: string): Stencil;
  reconnect(): Promise<Stencil>;
  readonly connections: string[];
  serverProjects(): Promise<TaggedRemoteProject[]>;
  moveServerProjectToLocal(meta: RemoteProjectMeta): Promise<string>;
  copyServerProjectToLocal(meta: RemoteProjectMeta, opts?: { name?: string }): Promise<string>;
  copyServerProjectToIncognito(meta: RemoteProjectMeta, opts?: { newTab?: boolean }): Promise<unknown>;
  publishIncognito(address: string): Promise<unknown>;

  // Assistant
  readonly llm: LlmFacade;
  prompt(text: string, opts?: { images?: string | string[] }): Promise<ChatTurnResult>;
  readonly chat: ChatFacade;

  // Windows
  readonly windows: string[];
  /** By title, case/punctuation-free; a hotkey id works too. Already open ⇒ no-op. */
  openWindow(title: string): Stencil;
  closeWindow(): Stencil;
  readonly openedWindow: string | null;
  openProjectsWindow(): Stencil;
  openServersWindow(): Stencil;
  openConnectionsWindow(): Stencil;
  openLinksWindow(): Stencil;
  openDescriptionWindow(): Stencil;
  openKeywordsWindow(): Stencil;
  openAssistantSettingsWindow(): Stencil;
  openShortcutsWindow(): Stencil;
  openVisualsWindow(): Stencil;
  openHelpWindow(): Stencil;
  openImageWindow(): Stencil;
  openCropWindow(): Stencil;
  readonly EasterEggs: EasterEggsFacade;

  // Editor actions
  rotateLeft(): Stencil;
  rotateRight(): Stencil;
  flipH(): Stencil;
  flipV(): Stencil;
  undo(): Stencil;
  redo(): Stencil;
  startDrawing(): Stencil;
  stopDrawing(): Stencil;
  drawing: boolean;
  /** Hands-free voice chat; throws where the browser has no speech recognition. */
  voiceChat: boolean;
  clearLines(): Stencil;
  move(delta?: { x?: number; y?: number }): Stencil;
  /** A relative step; `point` (image px) stays fixed on screen. */
  zoom(amount: number, point?: Partial<XY>): Stencil;
  zoomLevel: number;
  zoomFit(): Stencil;
  apply(opts?: ApplyOptions): Stencil;

  // Export / layout
  downloadImage(variant?: ExportVariant): Stencil;
  copyLayout(): Stencil;
  copyImage(variant?: ExportVariant): Stencil;
  copyImageToClipboard(variant?: ExportVariant): Stencil;
  shareImage(): Stencil;
  openIn(): Stencil;
  downloadLayout(): Stencil;
  /** Read: the current layout. Write: the paste path (prompts over an existing layout). */
  layout: LayoutPayload | string | undefined;
  applyLayout(data: LayoutPayload | string, opts?: LayoutInstallOptions): Stencil;
  setLines(lines: readonly Partial<CodecLine>[], opts?: LayoutInstallOptions): Stencil;
  saveProjectFile(opts?: { includeTheme?: boolean }): Promise<Stencil>;
  /** A File, the raw JSON text, or nothing for a picker. */
  openProjectFile(fileOrText?: File | string): Promise<Stencil>;
  liveSync: boolean;
  readonly linkedFile: string | null;
  syncNow(): Promise<Stencil>;
  deleteProjectFile(): Promise<Stencil>;

  // Session
  newEditor(opts?: { address?: string }): Stencil | Promise<Stencil>;
  blank(color?: ColorInput, opts?: { size?: Partial<Size>; address?: string }): Promise<Stencil>;
  save(): Stencil | Promise<Stencil>;
  load(url: string, opts?: LoadOptions): Promise<Stencil>;

  // Crop / coordinates
  crop(spec?: CropSpec): Stencil;

  /** Run a .stc script against the open project (contracts/stc/stc-contract.md). */
  execScript(text: string): Promise<Stencil>;
  /** Parse only: the formatted diagnostics an editor would underline. */
  checkScript(text: string, file?: string): string[];
  px2Page(p: XY): XY;
  page2Px(p: XY): XY;
}

/** Builds the facade once the DrawingApp exists; index.js installs it as window.stencil. */
export declare const createStencil: (app: DrawingApp) => Stencil;
