// The values window.stencil hands out and takes: the Point / Line / Project handles, the llm
// and chat halves, and each call's options. Types only — stencilApi.d.ts re-exports them where
// they stood, and vscode-extension/tools/genTypings.mjs inlines them at that re-export.
import type {
  XY, Size, ColorInput, LineStyle, ChatDock, StencilSettings, Stencil,
} from './stencilApi.js';
import type { LayoutPayload } from '../core/layout.js';
import type { RefreshPeriod } from '../core/project/store/projectsStore.js';
import type { LlmSettings } from '../llm/settings.js';
import type { VariantResult } from '../llm/plan/opPlan.js';

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
  /** openai-compat: the stored key. anthropic: write-only — held for this tab's session, read back as '[redacted]' ('' when none). */
  apiKey: string;
  serverUrl: string;
  /** When the anthropic session key lapses, epoch ms; 0 when none is held. */
  readonly keyExpiresAt: number;
  /** Drops the anthropic session key now. */
  forgetKey(): Stencil;
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
