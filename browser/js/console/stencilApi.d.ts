// window.stencil — the console control API (README "Console API"). A frozen, hard-guarded
// facade over the live DrawingApp: every mutation routes through the same core methods
// the toolbar uses, most calls return the facade (or a Project / Line / Point) to chain.
import type { DrawingApp } from '../core/drawingApp.js';
import type { CodecLine } from '../core/line/linesCodec.js';
import type { LayoutPayload, WireCropRect } from '../core/layout.js';
import type { RefreshPeriod } from '../core/project/store/projectsStore.js';
import type { ConnectSpec } from '../net/connectionManager.js';
import type { TaggedRemoteProject } from '../net/serverConnection.js';
import type { RemoteProjectMeta } from '../core/project/transferController.js';
import type { LlmSettings } from '../llm/settings.js';
import type { VariantResult } from '../llm/plan/opPlan.js';

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
  fillColor: ColorInput;
  selectionGlow: ColorInput;
  hoverRing: ColorInput;
  focusRing: ColorInput;
  /** 'default' (English) or a BCP-47 tag. */
  voiceInputLanguage: string;
  /** ms, clamped 500–10000. */
  voiceSilenceMs: number;
}

export interface TooltipSections { enabled: boolean; page: boolean; screen: boolean; coords: boolean; }

/** One {x,y} of a line; lineIdx -1 is the in-progress line. */
export interface Point {
  readonly lineIdx: number;
  readonly ptIdx: number;
  x: number | undefined; y: number | undefined;
  apply(opts?: { x?: number; y?: number; size?: number }): Point;
  move(delta?: { x?: number; y?: number }): Point;
  /** Empties the line → the line is dropped; returns the owning Line, else the facade. */
  remove(): Line | Stencil;
}

export interface Line {
  readonly idx: number;
  readonly points: Point[];
  color: string | undefined;
  /** Reads the colour the points actually draw in; null/'' clears back to the stroke. */
  pointColor: string | undefined;
  thickness: number | undefined;
  pointSize: number | undefined;
  style: string | undefined;
  fillColor: string | undefined;
  apply(opts?: { color?: ColorInput; pointColor?: ColorInput; thickness?: number; pointSize?: number; style?: LineStyle | string; fillColor?: ColorInput | 'transparent' }): Line;
  move(delta?: { x?: number; y?: number }): Line;
  /** Degrees clockwise around `pivot`, else the bounding-box centre. */
  rotate(deg: number, pivot?: Partial<XY>): Line;
  add(point: XY, at?: { neighbour?: number | XY; after?: boolean }): Line;
  remove(indexOrPoint: number | XY | Point): Line;
  /** Append another line's points and drop it. */
  join(other: Line): Line;
}

/** A Date, epoch ms, a parseable date string, or 0/null = keep forever. */
export type ExpiryInput = Date | number | string | null;

/** One registry row, or this tab's incognito editor (id null). */
export interface Project {
  readonly id: string | null;
  readonly incognito: boolean;
  readonly isOpened: boolean;
  expiresAt: number | null | ExpiryInput;
  expirationDate: Date | null | ExpiryInput;
  readonly isExpired: boolean;
  refreshPeriod: RefreshPeriod;
  autoRefresh: boolean;
  readonly size: { image: Size | null };
  name: string | null;
  color: string;
  /** The project's description, as the Description window edits it; '' clears it. */
  description: string;
  /** One array entry per keyword (which may be several words); a string splits on commas and newlines. */
  keywords: string[] | string;
  readonly blank: boolean;
  readonly fromFile: boolean;
  /** null for a non-blank project; assigning recolours a blank in place. */
  blankColor: string | null;
  addKeywords(...kw: (string | string[])[]): Project;
  removeKeywords(...kw: (string | string[])[]): Project;
  /** Settable on the active project only. */
  imageName: string | null;
  readonly layout: LayoutPayload | undefined;
  source: string | null;
  resource: string | null;
  renew(): Project;
  /** A free-form duration ('days 23', 'fortnight', 'off'); no argument returns the help text. */
  expire(spec?: string): Project | string;
  keepForever(): Project;
  close(opts?: { fully?: boolean }): Project;
  open(): Project;
  remove(): null;
  moveToServer(address: string): Promise<string>;
  copyToServer(address: string, opts?: { name?: string }): Promise<string>;
}

export interface LlmFacade {
  provider: LlmSettings['provider'];
  baseUrl: string;
  model: string;
  apiKey: string;
  serverUrl: string;
  /** Partial update; unknown providers / non-http(s) URLs throw. */
  setup(opts?: Partial<LlmSettings>): Stencil;
}

export interface ChatTurnResult { reply: string; warnings: string[]; results: VariantResult[]; chatOnly?: boolean; }

export interface ChatFacade {
  open(): Stencil;
  close(): Stencil;
  dock(mode: ChatDock): Stencil;
  readonly isOpen: boolean;
  /** Settled transcript copies: no raw model JSON, no error cards, no in-flight row. */
  readonly history: { role: 'user' | 'assistant'; text: string }[];
  /** True when a turn was actually running. */
  abort(): boolean;
  /** Throws mid-turn. */
  clear(): Stencil;
  readonly isSending: boolean;
  swapSides: boolean;
  voiceInput: boolean;
}

export interface CropSpec {
  x1?: number | string; y1?: number | string; x2?: number | string; y2?: number | string;
  album?: boolean;
  /** 'W:H' — shrinks one dimension about its centre to fit. */
  aspect?: string;
  /** Grow/shrink about the centre; exclusive with the edge tokens. */
  scale?: number;
}

export interface LoadOptions {
  source?: string;
  resource?: string;
  name?: string;
  /** Seconds into a video. */
  frame?: number;
  usePoster?: boolean;
  crop?: CropSpec | Record<string, unknown>;
  /** Also create+link the project on that connected server. */
  address?: string;
  incognito?: boolean;
}

export interface ApplyOptions extends Partial<StencilSettings> {
  /** Alias for pageSize. */
  page?: string;
  showTooltip?: boolean;
  tooltip?: Partial<TooltipSections>;
  fullscreen?: boolean;
  incognito?: boolean;
  zoom?: number;
  layout?: LayoutPayload | string;
  crop?: CropSpec;
  move?: { x?: number; y?: number };
}

export interface LayoutInstallOptions { mode?: 'replace' | 'combine'; history?: boolean; }

/** The extension's editor-page API; the extension owns the shape (browser-extension/README.md). */
export interface ExtensionEditorApi {
  editors(): Promise<unknown[]>;
  focus(tabId: number): Promise<unknown>;
  tabs(): Promise<unknown[]>;
  images(tabId: number): Promise<unknown[]>;
  open(index: number): Promise<unknown>;
  readonly current: Promise<unknown>;
  [member: string]: unknown;
}

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
