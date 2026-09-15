// The Stencil browser app's console API, as types.
//
// Generated from browser/js/console/stencilApi.d.ts by vscode-extension/tools/genTypings.mjs.
// Drop it beside your JavaScript and the editor types `stencil` itself: hovering a member
// gives its signature rather than `any`. The Stencil extension writes it here for you with
// "Stencil: Add facade typings to this workspace".
// window.stencil — the console control API (README "Console API"). A frozen, hard-guarded
// facade over the live DrawingApp: every mutation routes through the same core methods
// the toolbar uses, most calls return the facade (or a Project / Line / Point) to chain.
type DrawingApp = unknown;
type CodecLine = unknown;
type LayoutPayload = unknown;
type WireCropRect = unknown;
type RefreshPeriod = unknown;
type ConnectSpec = unknown;
type TaggedRemoteProject = unknown;
type RemoteProjectMeta = unknown;
type LlmSettings = unknown;
type VariantResult = unknown;

interface XY { x: number; y: number; }
interface Size { width: number; height: number; }

/** Any CSS colour on write (normalised to hex); '' where the comment says so. */
type ColorInput = string;

type LineStyle = 'solid' | 'dashed' | 'dotted';
type ImageFilter = 'none' | 'bw' | 'sepia' | 'invert' | 'contour' | 'custom';
type CompareMode = 'none' | 'original' | 'vertical' | 'horizontal';
type Unit = 'cm' | 'mm' | 'in';
type DrawMode = 'line' | 'rect';
type MotionMode = 'particles' | 'water' | 'fire' | 'slide' | 'none';
type ExportVariant = 'current' | 'original' | 'tint' | 'split';
type ChatDock = 'left' | 'right' | 'top' | 'bottom' | 'float';

/** Every key works both on the facade and under .settings, mirroring a toolbar control. */
interface StencilSettings {
  /**
   * The colour new strokes are drawn in.
   *
   * ```js
   * stencil.lineColor = '#ff3b30';
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  lineColor: ColorInput;
  /**
   * The colour of the points on a line.
   *
   * '' = points follow lineColor.
   *
   * ```js
   * stencil.pointColor = 'yellow';   // '' follows lineColor
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  pointColor: ColorInput;
  /**
   * Stroke width, in pixels.
   *
   * ```js
   * stencil.thickness = 4;
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  thickness: number;
  /**
   * Point radius, in pixels.
   *
   * ```js
   * stencil.pointSize = 6;
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  pointSize: number;
  /**
   * Solid, dashed or dotted strokes.
   *
   * ```js
   * stencil.lineStyle = 'dashed';   // solid | dashed | dotted
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  lineStyle: LineStyle;
  /**
   * Whether points are drawn — the alias of showPoints.
   *
   * Alias of showPoints.
   *
   * ```js
   * stencil.pointStyle = false;   // the alias of showPoints
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  pointStyle: boolean;
  /**
   * Whether points are drawn on top of the lines.
   *
   * ```js
   * stencil.showPoints = true;
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  showPoints: boolean;
  /**
   * Whether the strokes themselves are drawn.
   *
   * ```js
   * stencil.showLines = false;   // points alone
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  showLines: boolean;
  /**
   * The tint over the picture.
   *
   * ```js
   * stencil.filter = 'sepia';   // none | bw | sepia | invert | contour | custom
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  filter: ImageFilter;
  /**
   * How the original is shown beside the edit.
   *
   * ```js
   * stencil.compareMode = 'vertical';   // none | original | vertical | horizontal
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  compareMode: CompareMode;
  /**
   * Where the comparison divider sits.
   *
   * Divider position 0..1 for the split modes.
   *
   * ```js
   * stencil.compareSplit = 0.35;   // 0..1
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  compareSplit: number;
  /**
   * The tint colour a custom filter uses.
   *
   * ```js
   * stencil.apply({ filter: 'custom', filterColor: '#1e63c8' });
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  filterColor: ColorInput;
  /**
   * The unit lengths are typed and read in.
   *
   * ```js
   * stencil.unit = 'cm';   // cm | mm | in
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  unit: Unit;
  /**
   * The page format the picture is measured against.
   *
   * Case-insensitive ISO name ('a3', 'B5', 'c10') or 'custom'.
   *
   * ```js
   * stencil.pageSize = 'A4';   // or 'custom', with pageWidth/pageHeight
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  pageSize: string;
  /**
   * Custom page width, in centimetres.
   *
   * cm; applies when pageSize === 'custom'.
   *
   * ```js
   * stencil.pageSize = 'custom';
   * stencil.pageWidth = 21;     // cm
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  pageWidth: number;
  /**
   * Custom page height, in centimetres.
   *
   * ```js
   * stencil.pageHeight = 29.7;   // cm
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  pageHeight: number;
  /**
   * Whether the editor wears its dark theme.
   *
   * ```js
   * stencil.darkTheme = true;
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  darkTheme: boolean;
  /**
   * The accent the whole editor takes.
   *
   * A preset key persists + syncs; a hex is a custom accent for THIS page only.
   *
   * ```js
   * stencil.mainTheme = '#7c3aed';   // a preset key, or any hex
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  mainTheme: string;
  /**
   * Every accent preset there is to choose from.
   *
   * ```js
   * stencil.mainTheme = stencil.mainThemes[0];
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  readonly mainThemes: string[];
  /**
   * The open project's own accent colour.
   *
   * Active project's accent colour; '' = neutral. Throws without an active project.
   *
   * ```js
   * stencil.projectColor = '#4ec9b0';   // '' is neutral
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  projectColor: string;
  /**
   * The open project’s description.
   *
   * ```js
   * stencil.description = 'Roof survey, north face.';
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  description: string;
  /**
   * The open project’s keywords, which the project search reads.
   *
   * Read: string[]. Write: an array or a comma/space-separated string.
   *
   * ```js
   * stencil.keywords = 'roof, survey';   // read back as an array
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  keywords: string[] | string;
  /**
   * Whether a drag draws a line or a rectangle.
   *
   * ```js
   * stencil.drawMode = 'rect';   // line | rect
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  drawMode: DrawMode;
  /**
   * How long a press must be held before it starts drawing.
   *
   * ms, clamped 100–3000.
   *
   * ```js
   * stencil.holdDrawDelay = 400;   // ms, 100..3000
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  holdDrawDelay: number;
  /**
   * Whether a coordinate may be typed as a formula.
   *
   * ```js
   * stencil.allowFormulas = true;
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  allowFormulas: boolean;
  /**
   * The formula the x coordinate is computed from.
   *
   * ```js
   * stencil.formulaX = 'x * 2';
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  formulaX: string;
  /**
   * The formula the y coordinate is computed from.
   *
   * ```js
   * stencil.formulaY = 'y + 10';
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  formulaY: string;
  /**
   * Whether a stroke animates as it is drawn.
   *
   * ```js
   * stencil.drawingAnimations = false;
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  drawingAnimations: boolean;
  /**
   * Which motion the interface uses.
   *
   * ```js
   * stencil.motionMode = 'water';   // particles | water | fire | slide | none
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  motionMode: MotionMode;
  /**
   * Every motion mode there is to choose from.
   *
   * ```js
   * stencil.motionMode = stencil.motionModes.at(-1);
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  readonly motionModes: MotionMode[];
  /**
   * The fill inside a closed shape.
   *
   * ```js
   * stencil.fillColor = 'aqua';
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  fillColor: ColorInput;
  /**
   * The glow around a selected line.
   *
   * ```js
   * stencil.selectionGlow = '#ffd54a';
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  selectionGlow: ColorInput;
  /**
   * The ring drawn around a hovered point.
   *
   * ```js
   * stencil.hoverRing = '#7c3aed';
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  hoverRing: ColorInput;
  /**
   * The ring drawn around the focused control.
   *
   * ```js
   * stencil.focusRing = '#1e63c8';
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  focusRing: ColorInput;
  /**
   * The language voice input is recognised in.
   *
   * 'default' (English) or a BCP-47 tag.
   *
   * ```js
   * stencil.voiceInputLanguage = 'uk-UA';   // or 'default'
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  voiceInputLanguage: string;
  /**
   * How much silence ends a spoken turn.
   *
   * ms, clamped 500–10000.
   *
   * ```js
   * stencil.voiceSilenceMs = 1500;   // ms, 500..10000
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  voiceSilenceMs: number;
}

interface TooltipSections { enabled: boolean; page: boolean; screen: boolean; coords: boolean; }

/** One {x,y} of a line; lineIdx -1 is the in-progress line. */
interface Point {
  readonly lineIdx: number;
  readonly ptIdx: number;
  x: number | undefined;
  y: number | undefined;
  apply(opts?: { x?: number; y?: number; size?: number }): Point;
  move(delta?: { x?: number; y?: number }): Point;
  /** Empties the line → the line is dropped; returns the owning Line, else the facade. */
  remove(): Line | Stencil;
}

interface Line {
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
type ExpiryInput = Date | number | string | null;

/** One registry row, or this tab's incognito editor (id null). */
interface Project {
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

interface LlmFacade {
  provider: LlmSettings['provider'];
  baseUrl: string;
  model: string;
  apiKey: string;
  serverUrl: string;
  /** Partial update; unknown providers / non-http(s) URLs throw. */
  setup(opts?: Partial<LlmSettings>): Stencil;
}

interface ChatTurnResult { reply: string; warnings: string[]; results: VariantResult[]; chatOnly?: boolean; }

interface ChatFacade {
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

interface CropSpec {
  x1?: number | string; y1?: number | string; x2?: number | string; y2?: number | string;
  album?: boolean;
  /** 'W:H' — shrinks one dimension about its centre to fit. */
  aspect?: string;
  /** Grow/shrink about the centre; exclusive with the edge tokens. */
  scale?: number;
}

interface LoadOptions {
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

interface ApplyOptions extends Partial<StencilSettings> {
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

interface LayoutInstallOptions { mode?: 'replace' | 'combine'; history?: boolean; }

/** The extension's editor-page API; the extension owns the shape (browser-extension/README.md). */
interface ExtensionEditorApi {
  editors(): Promise<unknown[]>;
  focus(tabId: number): Promise<unknown>;
  tabs(): Promise<unknown[]>;
  images(tabId: number): Promise<unknown[]>;
  open(index: number): Promise<unknown>;
  readonly current: Promise<unknown>;
  [member: string]: unknown;
}

interface Stencil extends StencilSettings {
  /**
   * The browser extension’s editor hooks, or null when none is there.
   *
   * ```js
   * if (stencil.extension) await stencil.extension.crop();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  readonly extension: ExtensionEditorApi | null;
  /**
   * Every toolbar control at once, as one object.
   *
   * ```js
   * const { filter, thickness } = stencil.settings;
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  readonly settings: StencilSettings;
  /**
   * Whether the editor fills the screen.
   *
   * ```js
   * stencil.fullscreen = true;
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  fullscreen: boolean;
  /**
   * Whether this session is kept out of the store.
   *
   * Turning on needs a blank editor; turning off with work on screen promotes it to a project.
   *
   * ```js
   * stencil.incognito = true;   // needs a blank editor
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  incognito: boolean;
  /**
   * The loaded picture’s size, or undefined before one is.
   *
   * ```js
   * const { width, height } = stencil.imageSize ?? {};
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  readonly imageSize: Size | undefined;
  /**
   * The crop rectangle in the rotated original’s pixels.
   *
   * Rotated-original px, {x,y,w,h} plus width/height aliases; null before an image loads.
   *
   * ```js
   * const { x, y, w, h } = stencil.cropRect ?? {};
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  readonly cropRect: (Required<WireCropRect>) | null;
  /**
   * Turn the incognito session into a saved project.
   *
   * ```js
   * const id = stencil.promoteIncognito();   // null if there was nothing to keep
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  promoteIncognito(): string | null;
  /**
   * Which sections the coordinate tooltip shows.
   *
   * ```js
   * if (stencil.tooltip.coords) console.log("coordinates are shown");
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  readonly tooltip: TooltipSections;
  /**
   * Every line on the picture.
   *
   * ```js
   * console.log(stencil.lines.length, "lines");
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  readonly lines: Line[];
  /**
   * The points of the line the editor is working on.
   *
   * The current line's points: in-progress, else coord-table line, else last line.
   *
   * ```js
   * const [first] = stencil.points;
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  readonly points: Point[];
  /**
   * Every action and the key combination that fires it.
   *
   * ```js
   * console.log(stencil.shortcuts.undo);
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  readonly shortcuts: Record<string, string>;
  /**
   * Give an action a different key combination.
   *
   * `oldRef` is an action id or its current combo. Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.changeShortcut('undo', 'Alt+Z');
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  changeShortcut(oldRef: string, newCombo: string): Stencil;

  // Projects
  /**
   * The project on screen, or null in a blank editor.
   *
   * ```js
   * console.log(stencil.current?.name);
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  readonly current: Project | null;
  /**
   * The projects open in this browser.
   *
   * ```js
   * for (const p of stencil.openedProjects) console.log(p.name);
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  readonly openedProjects: Project[];
  /**
   * The projects put away but not deleted.
   *
   * ```js
   * console.log(stencil.archivedProjects.length);
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  readonly archivedProjects: Project[];
  /**
   * The projects this incognito session holds.
   *
   * ```js
   * console.log(stencil.incognitoProjects.map((p) => p.name));
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  readonly incognitoProjects: Project[];
  /**
   * The projects, narrowed to the kind you ask for.
   *
   * ```js
   * const archived = stencil.getProjects({ archived: true });
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  getProjects(opts?: { archived?: boolean; incognito?: boolean }): Project[];
  /**
   * One project by its name, or null.
   *
   * ```js
   * const p = stencil.getProjectByName('roof-survey');
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  getProjectByName(name: string): Project | null;
  /**
   * Every project tagged with any of these keywords.
   *
   * ```js
   * stencil.getProjectsByKeyword('roof', 'survey');
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  getProjectsByKeyword(...keywords: (string | string[])[]): Project[];
  /**
   * Read or set how long a project lives before it is cleared.
   *
   * ```js
   * stencil.expire('days 30');   // no argument returns the help text
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  expire(spec?: string): Project | string;

  // Servers
  /**
   * Connect to a collaboration server.
   *
   * Resolves to the facade, so calls chain with `await`.
   *
   * ```js
   * await stencil.connect('https://stencil.example');
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  connect(urlOrUrls: ConnectSpec | ConnectSpec[]): Promise<Stencil>;
  /**
   * Drop one connection, or all of them.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.disconnect('https://stencil.example');   // no argument drops them all
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  disconnect(url?: string): Stencil;
  /**
   * Open every saved connection again.
   *
   * Resolves to the facade, so calls chain with `await`.
   *
   * ```js
   * await stencil.reconnect();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  reconnect(): Promise<Stencil>;
  /**
   * The servers this editor is talking to.
   *
   * ```js
   * console.log(stencil.connections);
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  readonly connections: string[];
  /**
   * Every project the connected servers hold.
   *
   * ```js
   * const remote = await stencil.serverProjects();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  serverProjects(): Promise<TaggedRemoteProject[]>;
  /**
   * Take a server project and keep it here instead.
   *
   * ```js
   * const id = await stencil.moveServerProjectToLocal(remote[0]);
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  moveServerProjectToLocal(meta: RemoteProjectMeta): Promise<string>;
  /**
   * Copy a server project into this browser.
   *
   * ```js
   * await stencil.copyServerProjectToLocal(remote[0], { name: 'a copy' });
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  copyServerProjectToLocal(meta: RemoteProjectMeta, opts?: { name?: string }): Promise<string>;
  /**
   * Copy a server project into an unlinked incognito editor.
   *
   * ```js
   * await stencil.copyServerProjectToIncognito(remote[0], { newTab: true });
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  copyServerProjectToIncognito(meta: RemoteProjectMeta, opts?: { newTab?: boolean }): Promise<unknown>;
  /**
   * Put the incognito project on a server.
   *
   * ```js
   * await stencil.publishIncognito('https://stencil.example');
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  publishIncognito(address: string): Promise<unknown>;

  // Assistant
  /**
   * The assistant’s settings — provider, model, key.
   *
   * ```js
   * stencil.llm.model = 'claude-sonnet-5';
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  readonly llm: LlmFacade;
  /**
   * Ask the assistant for an edit, and let it act.
   *
   * ```js
   * const { reply } = await stencil.prompt('make it sepia and crop 10%');
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  prompt(text: string, opts?: { images?: string | string[] }): Promise<ChatTurnResult>;
  /**
   * The chat panel: its history, its dock, its turns.
   *
   * ```js
   * stencil.chat.dock('right');
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  readonly chat: ChatFacade;

  // Windows
  /**
   * Every window that can be opened by title.
   *
   * ```js
   * console.log(stencil.windows);
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  readonly windows: string[];
  /**
   * Open a window by its title.
   *
   * By title, case/punctuation-free; a hotkey id works too. Already open ⇒ no-op. Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.openWindow('crop');   // by title, case and punctuation free
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  openWindow(title: string): Stencil;
  /**
   * Close whichever window is open.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.closeWindow();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  closeWindow(): Stencil;
  /**
   * The open window’s title, or null.
   *
   * ```js
   * if (stencil.openedWindow === 'Crop') stencil.closeWindow();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  readonly openedWindow: string | null;
  /**
   * Open the projects list.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.openProjectsWindow();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  openProjectsWindow(): Stencil;
  /**
   * Open the server projects window.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.openServersWindow();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  openServersWindow(): Stencil;
  /**
   * Open the connections window.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.openConnectionsWindow();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  openConnectionsWindow(): Stencil;
  /**
   * Open the links window.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.openLinksWindow();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  openLinksWindow(): Stencil;
  /**
   * Open the project description window.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.openDescriptionWindow();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  openDescriptionWindow(): Stencil;
  /**
   * Open the project keywords window.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.openKeywordsWindow();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  openKeywordsWindow(): Stencil;
  /**
   * Open the assistant’s settings.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.openAssistantSettingsWindow();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  openAssistantSettingsWindow(): Stencil;
  /**
   * Open the keyboard shortcuts window.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.openShortcutsWindow();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  openShortcutsWindow(): Stencil;
  /**
   * Open the visuals window.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.openVisualsWindow();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  openVisualsWindow(): Stencil;
  /**
   * Open the help window.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.openHelpWindow();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  openHelpWindow(): Stencil;
  /**
   * Open the image chooser.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.openImageWindow();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  openImageWindow(): Stencil;
  /**
   * Open the crop window.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.openCropWindow();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  openCropWindow(): Stencil;

  // Editor actions
  /**
   * Turn the picture a quarter-turn anticlockwise.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.rotateLeft();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  rotateLeft(): Stencil;
  /**
   * Turn the picture a quarter-turn clockwise.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.rotateRight().apply({ filter: 'sepia' });
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  rotateRight(): Stencil;
  /**
   * Mirror the picture left to right.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.flipH();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  flipH(): Stencil;
  /**
   * Mirror the picture top to bottom.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.flipV();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  flipV(): Stencil;
  /**
   * Take back the last edit.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.undo();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  undo(): Stencil;
  /**
   * Put back an edit that was taken away.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.redo();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  redo(): Stencil;
  /**
   * Begin a line.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.startDrawing();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  startDrawing(): Stencil;
  /**
   * Finish the line being drawn.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.stopDrawing();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  stopDrawing(): Stencil;
  /**
   * Whether a line is being drawn right now.
   *
   * ```js
   * stencil.drawing = true;
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  drawing: boolean;
  /**
   * Whether the assistant is listening.
   *
   * Hands-free voice chat; throws where the browser has no speech recognition.
   *
   * ```js
   * stencil.voiceChat = true;   // throws where the browser cannot listen
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  voiceChat: boolean;
  /**
   * Take every line off the picture.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.clearLines();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  clearLines(): Stencil;
  /**
   * Pan the picture under the viewport.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.move({ x: -40, y: 0 });
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  move(delta?: { x?: number; y?: number }): Stencil;
  /**
   * Zoom by a step, about a point that stays put.
   *
   * A relative step; `point` (image px) stays fixed on screen. Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.zoom(0.25, { x: 200, y: 150 });   // the point stays put
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  zoom(amount: number, point?: Partial<XY>): Stencil;
  /**
   * The zoom factor, read or set outright.
   *
   * ```js
   * stencil.zoomLevel = 1;   // 100%
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  zoomLevel: number;
  /**
   * Fit the whole picture in the viewport.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.zoomFit();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  zoomFit(): Stencil;
  /**
   * Apply a batch of settings in one go.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.apply({ filter: 'bw', lineColor: '#ff3b30', thickness: 4 });
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  apply(opts?: ApplyOptions): Stencil;

  // Export / layout
  /**
   * Save the picture as a file.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.downloadImage('split');   // current | original | tint | split
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  downloadImage(variant?: ExportVariant): Stencil;
  /**
   * Put the layout on the clipboard.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.copyLayout();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  copyLayout(): Stencil;
  /**
   * Copy the picture.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.copyImage();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  copyImage(variant?: ExportVariant): Stencil;
  /**
   * Put the picture on the clipboard.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.copyImageToClipboard('original');
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  copyImageToClipboard(variant?: ExportVariant): Stencil;
  /**
   * Share the picture the way the browser offers.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.shareImage();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  shareImage(): Stencil;
  /**
   * Open this project in another Stencil front-end.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.openIn();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  openIn(): Stencil;
  /**
   * Save the layout as a file.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.downloadLayout();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  downloadLayout(): Stencil;
  /**
   * Read the layout on the picture, or paste one over it.
   *
   * Read: the current layout. Write: the paste path (prompts over an existing layout).
   *
   * ```js
   * const layout = stencil.layout;
   * stencil.layout = layout;   // paste it back
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  layout: LayoutPayload | string | undefined;
  /**
   * Install a layout, combining with or replacing what is there.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.applyLayout(layout, { mode: 'replace' });
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  applyLayout(data: LayoutPayload | string, opts?: LayoutInstallOptions): Stencil;
  /**
   * Put lines on the picture directly.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.setLines([{ points: [{ x: 10, y: 10 }, { x: 90, y: 90 }] }], { mode: 'combine' });
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  setLines(lines: readonly Partial<CodecLine>[], opts?: LayoutInstallOptions): Stencil;
  /**
   * Write the project out as a .stencil file.
   *
   * Resolves to the facade, so calls chain with `await`.
   *
   * ```js
   * await stencil.saveProjectFile({ includeTheme: true });
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  saveProjectFile(opts?: { includeTheme?: boolean }): Promise<Stencil>;
  /**
   * Open a .stencil project.
   *
   * A File, the raw JSON text, or nothing for a picker. Resolves to the facade, so calls chain with `await`.
   *
   * ```js
   * await stencil.openProjectFile();   // no argument opens a picker
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  openProjectFile(fileOrText?: File | string): Promise<Stencil>;
  /**
   * Whether edits are written back to the linked file as they happen.
   *
   * ```js
   * stencil.liveSync = true;
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  liveSync: boolean;
  /**
   * The file this project is synced with, or null.
   *
   * ```js
   * console.log(stencil.linkedFile);
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  readonly linkedFile: string | null;
  /**
   * Write the project to its linked file now.
   *
   * Resolves to the facade, so calls chain with `await`.
   *
   * ```js
   * await stencil.syncNow();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  syncNow(): Promise<Stencil>;
  /**
   * Delete the linked file and unlink the project.
   *
   * Resolves to the facade, so calls chain with `await`.
   *
   * ```js
   * await stencil.deleteProjectFile();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  deleteProjectFile(): Promise<Stencil>;

  // Session
  /**
   * Start a blank editor.
   *
   * ```js
   * stencil.newEditor();   // { address } also links it to a server
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  newEditor(opts?: { address?: string }): Stencil | Promise<Stencil>;
  /**
   * Make a blank page of a colour and a size.
   *
   * Resolves to the facade, so calls chain with `await`.
   *
   * ```js
   * await stencil.blank('#ffffff', { size: { width: 1200, height: 800 } });
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  blank(color?: ColorInput, opts?: { size?: Partial<Size>; address?: string }): Promise<Stencil>;
  /**
   * Save the project as it stands.
   *
   * ```js
   * await stencil.save();
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  save(): Stencil | Promise<Stencil>;
  /**
   * Load a picture, or a video frame, from a URL.
   *
   * Resolves to the facade, so calls chain with `await`.
   *
   * ```js
   * await stencil.load('https://example.com/roof.png');
   * await stencil.load(videoUrl, { frame: 3 });
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  load(url: string, opts?: LoadOptions): Promise<Stencil>;

  // Crop / coordinates
  /**
   * Cut the picture down.
   *
   * Hands the facade back, so calls chain.
   *
   * ```js
   * stencil.crop({ x1: '10%', x2: '-10%', aspect: '3:2' });
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  crop(spec?: CropSpec): Stencil;

  /**
   * Run a .stc script against the open project.
   *
   * Run a .stc script against the open project (contracts/stc/stc-contract.md). Resolves to the facade, so calls chain with `await`.
   *
   * ```js
   * await stencil.execScript('@crop 10%\n@filter sepia\n');
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  execScript(text: string): Promise<Stencil>;
  /**
   * Check a .stc without running it, and hand back its diagnostics.
   *
   * Parse only: the formatted diagnostics an editor would underline.
   *
   * ```js
   * const problems = stencil.checkScript('@filtre bw\n');
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  checkScript(text: string, file?: string): string[];
  /**
   * Turn image pixels into page coordinates.
   *
   * ```js
   * const onPage = stencil.px2Page({ x: 120, y: 240 });
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  px2Page(p: XY): XY;
  /**
   * Turn page coordinates into image pixels.
   *
   * ```js
   * const inPixels = stencil.page2Px({ x: 5, y: 7 });
   * ```
   *
   * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api)
   */
  page2Px(p: XY): XY;
}

/**
 * The browser app's console control API, installed by the page as `window.stencil`.
 *
 * A frozen, guarded facade over the live editor: every mutation goes through the same core
 * methods the toolbar uses, and most calls hand the facade back, so they chain.
 *
 * ```js
 * stencil.rotateRight().apply({ filter: 'sepia' });
 * ```
 *
 * [Stencil console API](https://github.com/andrew1407/stencil/blob/main/browser/README.md#console-api) · [the .stc language](https://github.com/andrew1407/stencil/blob/main/contracts/stc/stc-contract.md)
 */
declare var stencil: Stencil;
