import { wireCanvasScrollbars } from '../ui/canvasScrollbars.js';
import { setVal, notify, cmToUnit, unitLabel, defaultUnitFromLocale, composeControlTitle, shortName, compareEditedShows, wireScrollbarHover } from '../utils.js';
import * as hitTest from './hitTest.js';
import * as dragGestures from './dragGestures.js';
import * as selectionPanel from '../ui/selectionPanel.js';
import { updateButtons as updateControlState } from '../ui/controlState.js';
import constants from '../config/constants.json' with { type: 'json' };
const { PAGE_SIZES } = constants;
import { HistoryStack } from './historyStack.js';
import { FormulaEngine } from './formulaEngine.js';
import { Renderer } from './renderer.js';
import { StrokeFx } from './strokeFx.js';
import { Storage } from './storage.js';
import { getProjectsBackend } from './projectsBackend.js';
import { TabsCoordinator } from './tabsCoordinator.js';
import { PROJECT_ACTION } from '../worker/messages.js';
import { defaultBlankSizePx } from './layout.js';
import { CoordTable } from './coordTable.js';
import { ZoomPan } from './zoomPan.js';
import { ExportService } from './exportService.js';
import { SettingsController } from './settingsController.js';
import { AccentController } from './accentController.js';
import { ImageModel } from './imageModel.js';
import { RemoteSyncController } from './remoteSyncController.js';
import { ProjectTransferController } from './projectTransferController.js';
import { InputController } from './inputController.js';
import { PointerController } from './pointerController.js';
import { ControlsBinder } from './controlsBinder.js';
import { core } from './stencilCore.js';
import { hotkeys } from './hotkeys.js';
import { DEFAULT_ACCENT, isAccent, applyAccentFavicon, normalizeHex, accentHex } from './accents.js';
import { buildLayoutPayload, validateLayout, resolveInsertIdx, fillState, mergeLines } from './layout.js';
import { readOpenProjectId, buildExternalLaunchUrl, normalizeLaunchPayload, LAUNCH_DATA_URL_MAX } from './deepLink.js';
import { StencilSync } from './stencilSync.js';
import { wireExtensionBridge } from './extensionBridge.js';
import { normalizePageSize, pageFormatLabel } from './units.js';
import { icon } from '../ui/icons.js';
import { enhanceSelect, enhanceAllSelects } from '../ui/customSelect.js';
import { playCanvasArrival, leaveThenRemove, swapContent, pinWidestFace, revealControls, strokeFoot } from '../ui/motion.js';
import { requireConnection, createRemoteProject, saveRemoteProject } from '../net/remoteSync.js';
import { getSyncToServer, loadSavedServers } from '../net/connectionStore.js';
import { normalizeUrl } from '../net/connectionManager.js';
import { OPEN_IN_DEFAULTS, loadOpenInConfig } from '../config/openInConfig.js';

// Inline SVG glyphs for the draw-mode toggle. `currentColor` makes them inherit
// the button's text color (theme + label match).
// A matched PAIR, because they are one toggle: both anchor the same two handles —
// (3,13) and (13,3), the corners a drag actually starts and ends on — in the same
// 1.5 stroke. Only what joins them changes, a segment or the box it spans, so
// Line↔Rect swaps between two siblings instead of two different families.
// The hooks are the canon's own (icons.json `line`/`rect`), so both faces play the
// canonical draw-on: the shape draws itself between the handles, which then pop.
export const DRAW_MODE_ICON = {
  line: '<svg class="draw-mode-icon" viewBox="0 0 16 16" width="13" height="13" aria-hidden="true">' +
    '<line class="ic-stroke" x1="3" y1="13" x2="13" y2="3" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>' +
    '<circle class="ic-handle" cx="3" cy="13" r="2" fill="currentColor"/><circle class="ic-handle" cx="13" cy="3" r="2" fill="currentColor"/></svg>',
  rect: '<svg class="draw-mode-icon" viewBox="0 0 16 16" width="13" height="13" aria-hidden="true">' +
    '<rect class="ic-box" x="3" y="3" width="10" height="10" rx="1" fill="none" stroke="currentColor" stroke-width="1.5"/>' +
    '<circle class="ic-handle" cx="3" cy="13" r="2" fill="currentColor"/><circle class="ic-handle" cx="13" cy="3" r="2" fill="currentColor"/></svg>',
};

// Base name without its file extension (for project naming / source matching).
const stripExt = (name) => {
  const s = String(name || '');
  const dot = s.lastIndexOf('.');
  return dot > 0 ? s.slice(0, dot) : s;
};

// ── The external-import tail, shared by every surface handing an image to THIS tab ──
// `mode`: 'new' = its own project (the only mode the naming options apply to); 'replace' /
// 'replace-keep' = swap the active project's image, dropping or keeping its lines. Rejects on
// a failed fetch. A free function (not a method) so tests can drive plain stand-in apps.
const importInlineImage = (app, launch, { mode = 'new' } = {}) => {
  const name = launch.name || 'image.png';
  // Auto-number the name against existing same-source projects ("name (1)", …);
  // skipped for incognito, which never persists.
  const opts = launch.crop ? { crop: launch.crop } : {};
  if (!launch.crop && launch.noCrop) opts.noCrop = true;   // Open-Image dialog "Crop off" → full frame
  opts.source = launch.source;
  opts.resource = launch.resource;
  if (!app.storage.incognito && launch.source) opts.name = app.storage.store.copyName(stripExt(name), launch.source);
  // An inline layout (desktop/bot hand-off) restores annotations + filter + crop + page.
  if (launch.layout) {
    opts.layout = launch.layout;
    opts.adoptLayout = true;
  }

  // `src` launches carry an http(s) image URL instead of inline bytes (kept short for
  // links sent through chat). The fetch is best-effort: the host must allow CORS.
  const imageUrl = launch.kind === 'src' ? launch.src : launch.dataUrl;
  if (launch.kind === 'src' && !opts.source) opts.source = launch.src;
  return fetch(imageUrl, launch.kind === 'src' ? { mode: 'cors' } : undefined)
    .then(r => { if (!r.ok) throw new Error(`HTTP ${r.status}`); return r.blob(); })
    .then(blob => {
      const file = new File([blob], name, { type: blob.type || 'image/png' });
      if (mode === 'new') app.loadImageFromFile(file, opts);
      else app.replaceProjectImage(file, { keepAnnotations: mode === 'replace-keep', crop: opts.crop });
    });
};

// ── DrawingApp: orchestrator owning state + DOM wiring ──────────
// DOM event wiring is split into cohesive #wire* methods invoked in source order
// by initEventListeners(). Pure decision helpers live in ./layout.js so they can
// be unit-tested in Node without a DOM.
export class DrawingApp {
  // Point/segment/line drag state
  draggingPoint = null;
  dragJustEnded = false;
  draggingSegment = null;
  draggingLine = null;
  // Continuation drawing
  continueLineIdx = -1;
  continueInsertIdx = -1;
  #rectConnectOnce = false;
  // Touch + hold-to-draw gesture state lives in InputController (inputController.js), as
  // this.input. The two drawing-continuation fields (continueLineIdx/InsertIdx above) and the
  // drag helpers are public because that controller + the mouse pan/drag path both use them.
  // Topbar project-name editor controller ({ refresh }), wired in #wireToolbarButtons.
  nameEditor = null;
  // True while the topbar name is in inline-edit mode (input unlocked, ✓/✗ shown).
  nameEditing = false;
  // Debounce timers
  #thicknessSaveTimer = null;
  #rotateSaveTimer = null;
  // Live co-edit push/pull (debounce timers, echo-suppression timestamp, reload guards) lives
  // in RemoteSyncController (remoteSyncController.js), constructed as this.remoteSync.
  // True when THIS user changed the filter since the last sync — so a save imposes our
  // filter (our intent wins), but a save that's only line edits preserves the shared
  // server filter instead of clobbering a peer's filter change.
  filterDirty = false;

  constructor() {
    this.canvas = document.getElementById('canvas');
    this.ctx = this.canvas.getContext('2d');
    this.tooltip = document.getElementById('tooltip');
    this.coordinatesBody = document.getElementById('coordinates-body');

    this.image = null;
    // Crop support: `originalImage` = untouched full-res bitmap; working `image` =
    // canvas holding only the cropped (page-shaped) region; `cropRect` records it in
    // original-image pixels. Line and point coords are crop-local. See applyCrop / #buildCroppedImage.
    this.originalImage = null;
    this.cropRect = null;
    // Non-destructive 90° rotation: quarter-turn count (0..3, clockwise) applied to
    // `originalImage` before cropping. Original is never modified; `cropRect` lives in
    // the rotated pixel space and line points ride along each turn. See rotateImage / #rotatedOriginalCanvas.
    this.rotationQuarters = 0;
    // Provenance: `imageSource` = image/video's own URL, `imageResource` = web page it
    // came from. Both null for plain local uploads; set by add-by-URL + extension hand-off.
    // Persisted in the layout and mirrored into project meta.
    this.imageSource = null;
    this.imageResource = null;
    // Active session's blank-fill colour ("#rrggbb"), or "" for an ordinary image project. Set by
    // createBlankImage, restored on open, persisted into project meta (storage.js). Non-empty ⇔
    // this is a blank project (whose solid background can be recoloured after creation).
    this.blankColor = '';
    // True when opened from a portable .stencil file (provenance → bronze projects-list outline).
    this.fromFile = false;
    this.lines = [];
    this.currentLine = null;
    this.isDrawing = false;
    this.scale = 1;

    // Pan state (Alt+drag) — delta-based, with optional Shift speed-up. The pan cursor delta
    // lives in PointerController; only the isPanning flag is shared editor state.
    this.isPanning = false;

    // Point drag state (Alt+hover+drag on point)
    this.draggingPoint = null; // { lineIdx, ptIdx, point }
    this.isDraggingPoint = false;
    this.dragJustEnded = false;

    // Segment drag state (Alt+drag on a line segment between two points)
    this.isDraggingSegment = false;
    this.draggingSegment = null; // { lineIdx, ptIdx1, ptIdx2, startX, startY, origPt1, origPt2 }

    // Whole-line drag state (Alt+Shift+drag on any part of a line)
    this.isDraggingLine = false;
    this.draggingLine = null; // { lineIdx, startX, startY, origPoints }

    // Zoom rect state (Shift+left-drag)
    this.isZoomRectDragging = false;
    this.zoomRectStart = null; // { imgX, imgY, cssX, cssY }
    this.zoomRectEnd = null; // { imgX, imgY, cssX, cssY }

    // Coord table state
    this.coordLineIdx = -1;   // which line is shown in table
    this.hoveredPtIdx = -1;   // hovered row in table
    this.focusedPtIdx = -1;   // clicked/focused row in table

    this.color = '#FFFF00';
    // Default point colour for new lines. '' = follow this.color, matching
    // core's Line::pointColor / pointColorOr fallback.
    this.pointColor = '';
    this.thickness = 2;
    this.pointSize = 4;
    this.style = 'solid';
    this.showPoints = true;
    this.showLines = true;
    this.imageFilter = 'none'; // 'none' | 'bw' | 'sepia' | 'custom'
    this.filterColor = '#7c3aed'; // custom tint color
    // Compare view: the edited result against the untouched original (crop + rotation only).
    // 'original' shows the original alone; 'vertical'/'horizontal' split with a movable
    // divider. Transient view state — never persisted or synced.
    this.compareMode = 'none'; // 'none' | 'original' | 'vertical' | 'horizontal'
    this.compareSplit = 0.5;   // divider position (0..1) for the split compare modes
    this.compareHoldOriginal = false; // Alt+Shift+O momentary "show original" override
    this.pageSize = 'A3';
    this.customPageWidth = 21;
    this.customPageHeight = 29.7;
    this.selectedLineIdx = -1;
    // Multi-line selection set (Ctrl/⌘+Shift+click). Empty in single-select mode —
    // selectedIndices() then falls back to [selectedLineIdx]. With 2+ lines held,
    // selectedLineIdx is -1 (the single-line editor hides) and move/rotate act on all.
    this.selectedLines = [];
    // Tooltip visibility (persisted)
    this.tooltipEnabled = true;
    this.tooltipShowPage = true;
    this.tooltipShowScreen = true;
    this.tooltipShowCoords = true;

    // Coordinate formula transforms
    this.allowFormulas = false;
    this.formulaX = ''; // empty = identity
    this.formulaY = '';

    // Display unit for page/length readouts: 'cm' or 'in'. Lengths are always stored in
    // cm; this only affects display/entry. Default seeded from locale (US/imperial → in,
    // else cm); a restored layout's saved unit overrides it.
    this.unit = defaultUnitFromLocale();

    // Hold-to-draw (see ./holdDraw.js): delay (ms) is configurable; holdPreview is the
    // ghost-line cursor target (image space) while a hold stroke is active.
    this.holdDrawDelay = 500;
    this.holdPreview = null;

    // ── Drawing mode: 'line' (click points) or 'rect' (drag rectangle) ──
    this.drawMode = 'line';
    this.isRectDrawDragging = false;
    this.rectDrawStart = null; // { imgX, imgY, cssX, cssY }
    this.rectDrawEnd = null;
    this.#rectConnectOnce = false; // one-shot: connect next rect to selection
    // Continuation drawing: Start with a line selected → new points/rects extend that
    // line (connecting to its last/focused point) and inherit its style. -1 = fresh line.
    this.continueLineIdx = -1;
    this.continueInsertIdx = -1;

    // ── Hover tracking (for hover ring on any point, Ctrl/Shift tooltip refresh) ──
    this.hoverPt = null;          // { lineIdx, ptIdx } currently hovered on canvas
    this.hoverLineIdx = -1;       // line under the canvas cursor → tints its Lines-list row
    this.listHoverLineIdx = -1;   // hovered Lines-list row → hover glow on the canvas
    this.mouseOverCanvas = false;
    this.lastMouseClientX = 0;
    this.lastMouseClientY = 0;

    // ── Configurable visuals (persisted) ──
    this.selGlowColor = '#ffc800'; // selection highlight glow (lines + points)
    this.hoverRingColor = '#7c3aed'; // hover ring around points
    this.focusRingColor = '#7c3aed'; // focused/clicked point ring
    // White, not blue: a fill is paint you put ON the picture, and the neutral one is the
    // least surprising thing for the swatch to start at (a saturated blue reads as a choice
    // already made). Shared with the desktop via config/constants.json.
    this.defaultFillColor = '#ffffff';

    // ── Multi-project state ──
    // The active project id mirrors storage.activeId; null = temporary editor.
    this.activeProjectId = null;

    // Link to a server-stored project for the current editing session, or null for
    // a purely-local one. { address, remoteId, version }; set when a remote project
    // is opened or a local create targets a server, consumed by saveToServer().
    this.remoteLink = null;

    // One-shot server address armed by newEditor({ address }): the NEXT image load
    // creates the project on it (with real bytes — the server forbids image-less
    // projects), then links the session. Consumed/cleared by the next loadImageFromFile.
    this.pendingRemoteAddress = null;

    // ── Components ──
    this.history = new HistoryStack();
    this.formula = new FormulaEngine();
    this.renderer = new Renderer(this);
    // Vertices in flight: every route that adds a point hands it here and the
    // renderer draws it travelling to where it was put (strokeFx.js).
    this.strokeFx = new StrokeFx(this);
    this.storage = new Storage(this);
    this.tabs = new TabsCoordinator();
    // Another tab changed the project set: sync the editor if it's our project.
    this.tabs.onProjectsChanged(detail => this.#onRemoteProjectsChange(detail || {}));
    // Another tab changed the accent: repaint our UI live (no re-broadcast) and
    // let any open Visuals modal resync its swatch. A local custom (temp) accent wins —
    // a peer's preset change must not clobber this page's one-off colour.
    this.tabs.onAccent(key => {
      if (this.customAccent) return;
      const next = this.accents.applyAccent(key);
      try { window.dispatchEvent(new CustomEvent('stencil:accent-changed', { detail: next })); } catch { /* no DOM — best-effort UI nudge */ }
    });
    this.coordTable = new CoordTable(this);
    // Image/layout export, clipboard, and file IO (see exportService.js).
    this.export = new ExportService(this);
    // Shared editor setters (style/page/formula/tooltip/visual — see settingsController.js).
    this.settings = new SettingsController(this);
    // UI theme + accent writes (see accentController.js). The theme/accent getters stay on app.
    this.accents = new AccentController(this);
    // Non-destructive crop + quarter-turn rotation geometry (see imageModel.js).
    this.imageModel = new ImageModel(this);
    // Live co-edit push/pull + server writes (see remoteSyncController.js).
    this.remoteSync = new RemoteSyncController(this);
    // Project lifecycle + local↔server move/copy (see projectTransferController.js). Takes
    // explicit deps — the `host` facade is the narrow slice of app state/callbacks it needs
    // (`getConnections` is a getter because stencilApi creates the manager lazily).
    const app = this;
    this.projectTransfer = new ProjectTransferController({
      storage: this.storage,
      tabs: this.tabs,
      remoteSync: this.remoteSync,
      getConnections: () => app.connections,
      host: {
        get activeProjectId() { return app.activeProjectId; },
        set activeProjectId(id) { app.activeProjectId = id; },
        get remoteLink() { return app.remoteLink; },
        set remoteLink(link) { app.remoteLink = link; },
        set blankColor(color) { app.blankColor = color; },
        set imageBaseName(name) { app.imageBaseName = name; },
        get chatPersistence() { return app.chatPersistence; },
        updateProjectTitle: (force) => app.updateProjectTitle(force),
        updateIncognitoUI: () => app.updateIncognitoUI(),
        newEditor: (opts) => app.newEditor(opts),
        loadImageFromFile: (file, opts) => app.loadImageFromFile(file, opts),
        setBlankColor: (color) => app.setBlankColor(color),
      },
    });
    // Live two-way sync between a file-linked project and its .stencil on disk (opt-in;
    // File System Access / Chromium only — no-ops elsewhere). See stencilSync.js.
    this.stencilSync = new StencilSync(this);
    // Touch + hold-to-draw alternative input (see inputController.js). Wired in initEventListeners.
    this.input = new InputController(this);
    // Mouse pan / drag / rect / zoom-rect wiring (see pointerController.js).
    this.pointer = new PointerController(this);
    // The tooltip is a custom element (<stencil-tooltip>) that owns its render
    // logic; give it the app ref and alias it as tooltipMgr for existing callers.
    this.tooltip.app = this;
    this.tooltipMgr = this.tooltip;
    this.zoomPan = new ZoomPan(this);
    // DOM event wiring for toolbars/keyboard/canvas (see controlsBinder.js). Constructed last so
    // its wire* methods can reference every collaborator; invoked by initEventListeners() below.
    this.controls = new ControlsBinder(this);

    this.initEventListeners();
    // Keep the canvas viewport (and the coordinates panel, the other column that can
    // outgrow the window) sized to the available height on every geometry change.
    const syncViewport = () => {
      const vp = document.getElementById('canvas-viewport');
      if (!vp || document.body.classList.contains('fullscreen-mode')) return;
      this.zoomPan.syncViewportHeight(); // the frame always fills the available height
      this.zoomPan.syncCoordPanelHeight();
    };
    syncViewport();
    // …and again after the first paint: the first call runs before the shell's layout is
    // real, which leaves the empty editor slightly too tall (permanent scrollbar).
    if (typeof requestAnimationFrame === 'function') requestAnimationFrame(syncViewport);
    // …and once the shell's reveal ends. The measure already discounts the appReveal
    // translate (zoomPan.js layoutTop), so this only catches anything else it settled.
    const shell = document.querySelector('.container');
    shell?.addEventListener('animationend', (e) => {
      if (e.target === shell && e.animationName === 'appReveal') syncViewport();
    });
    window.addEventListener('resize', syncViewport);
    // Docking/undocking the chat moves body's padding (editor height), so re-measure too;
    // deferred a frame so the emitter's class change is in the layout.
    const syncViewportSoon = () => {
      if (typeof requestAnimationFrame === 'function') requestAnimationFrame(syncViewport);
      else syncViewport();
    };
    window.addEventListener('stencil:chat-layout-changed', syncViewportSoon);
    // …and once body's padding finishes ANIMATING (components.css slides it over ~340ms):
    // the events above fire at the start of the slide and measure the old geometry.
    document.body.addEventListener('transitionend', (e) => {
      if (e.target === document.body && e.propertyName.startsWith('padding')) syncViewport();
    });
    // …and once the toolbar fold lands: collapsing it moves the viewport's top by the whole
    // height of the tool rows, so the cap it was given no longer reaches the window bottom.
    document.getElementById('controls-body')?.addEventListener('transitionend', (e) => {
      if (e.propertyName === 'grid-template-rows') syncViewport();
    });
    // Boot synchronously into a blank temporary editor (migrate + sweep only); the
    // projects component decides whether to offer a chooser after readiness.
    this.restoreFromLocalStorage();
    this.storage.newTemporary();
    // "?open=<id>" deep link = tab launched to view one project (projects modal's "open
    // in new tab"). Read now, before any component wires, so the chooser stays closed;
    // applyProjectDeepLink() loads it once everything is wired.
    this.pendingOpenProjectId = readOpenProjectId(location.search);
    // Extension hand-off (`#stencil=…`) = tab launched to open one image; like the deep
    // link above, the chooser must stay closed so it doesn't pop over the imported image.
    // Read before the fragment is consumed/stripped in applyExternalLaunch().
    this.hasExternalLaunch = (location.hash || '').startsWith('#stencil=');
    // "Open in…" targets (desktop scheme + optional Telegram bot username). Seeded with
    // defaults so the toolbar button gates correctly before the async config load; the
    // fetch then refreshes the gating (e.g. reveals Telegram availability).
    this.openInConfig = { ...OPEN_IN_DEFAULTS };
    loadOpenInConfig().then(cfg => { this.openInConfig = cfg; this.updateButtons(); });
    // Reflect the initial (imageless) state: undo/redo + fullscreen start disabled.
    this.updateButtons();
    this.applyUnitToUI();
  }

  // Whether the current session can be handed to another app. Desktop needs a configured
  // URL scheme (opens any project — server ref or inline). Telegram needs a bot username
  // AND a server project (a 64-char t.me start payload can't carry image bytes). Drives
  // both the toolbar #open-in-btn visibility and the modal's per-button hiding.
  openInDesktopAvailable() { return !!this.openInConfig?.desktopScheme; }
  openInTelegramAvailable() { return !!this.openInConfig?.telegramBotUsername && !!this.remoteLink; }
  openInAvailable() { return this.openInDesktopAvailable() || this.openInTelegramAvailable(); }

  // Slim orchestrator: wire each cohesive control group in source order so
  // document-level listener dispatch order stays identical to before the split.
  initEventListeners() {
    this.controls.wireStyleControls();
    this.controls.wireSelectionPanelControls();
    this.controls.wirePageAndDisplayControls();
    this.controls.wireFormulaControls();
    this.controls.wireToolbarButtons();
    this.controls.wireZoomControls();
    this.controls.wireScrollPersist();
    this.controls.wireTheme();
    this.controls.wireKeyboard();
    this.controls.wireArrowPan();
    this.controls.wireDropPaste();
    // The canvas gets its own overlay bars (js/ui/canvasScrollbars.js); every other
    // scrollable's native thumb takes the accent only under the pointer (utils.js).
    wireCanvasScrollbars(document.getElementById('canvas-viewport'));
    wireScrollbarHover();
    this.controls.wireCanvasPointer();
    this.controls.wireSmoothZoom();
    // Last, so every select the layout rendered (toolbar, panel and each modal, which are
    // all in the DOM from boot) wears the app's own dropdown rather than the OS one — the
    // toolbar pair enhanced in wirePageAndDisplayControls above included, since a second
    // pass over an enhanced select is a no-op.
    // The image-filter and compare selects preview on hover: resting on a row live-applies
    // it to the canvas (repaint only); leaving the list puts the current value back.
    enhanceAllSelects(document, {
      preview: (sel) => {
        if (sel.id === 'image-filter') return (v) => this.settings.preview('imageFilter', v);
        if (sel.id === 'compare-mode') return (v) => this.settings.preview('compareMode', v);
        return null;
      },
    });
    this.pointer.wirePanDrag();
    this.input.wireHoldDraw();
    this.input.wireTouch();
    this.#wireExternalResume();
    // The extension's "editor mode" asks this tab about itself (project/image/preview) and
    // imports into it without opening a new tab — see js/core/extensionBridge.js.
    wireExtensionBridge(this);
  }

  // The extension's editorBridge dispatches `stencil:switch-to-source` when the user picks
  // "resume in the open editor tab": switch to the matching project here (no reload) instead
  // of the extension spawning a new tab. Ignored while incognito (those images never persist).
  #wireExternalResume() {
    window.addEventListener('stencil:switch-to-source', (e) => {
      if (this.storage.incognito) return;
      const { source = '', name = '' } = e?.detail || {};
      if (!source && !name) return;
      this.#resumeBySource(source, name);
    });
  }


  // ── Formula controls (top bar) ──────────────────────────────
  // The formula UI helpers (settings.syncFormulaUI / showFormulaError / refreshFormulaCoords)
  // and setAllowFormulas live in SettingsController with the other setters. This validates
  // BOTH inputs together (no half-typed pair) and shows the inline error; the console's
  // setFormula() throws instead.




  // Active UI theme ('dark' | 'light').
  get theme() { return document.documentElement.getAttribute('data-theme') === 'dark' ? 'dark' : 'light'; }
  // Theme + accent writes live in AccentController (accentController.js); these thin
  // delegators keep the public method names the toolbar + window.stencil facade call. The
  // theme/accent/customAccent GETTERS stay here because they just read the document element.
  setTheme(theme, originEl = null) { this.accents.setTheme(theme, originEl); }
  setAccent(key, originEl = null) { this.accents.setAccent(key, originEl); }
  setCustomAccent(hex, originEl = null) { return this.accents.setCustomAccent(hex, originEl); }
  previewAccent(key, originEl = null) { this.accents.previewAccent(key, originEl); }
  endAccentPreview(originEl = null) { this.accents.endAccentPreview(originEl); }

  // Active accent preset key (see js/core/accents.js); falls back to violet.
  get accent() {
    const a = document.documentElement.getAttribute('data-accent');
    return isAccent(a) ? a : DEFAULT_ACCENT;
  }

  // A custom (non-preset) accent applied to THIS page only — the inline --accent override
  // string, or null when a named preset is active. Set via setCustomAccent.
  get customAccent() {
    return document.documentElement.style.getPropertyValue('--accent').trim() || null;
  }







  // Mouse pan/drag/rect/zoom-rect wiring lives in PointerController (pointerController.js),
  // invoked from initEventListeners as this.pointer.wirePanDrag(). The drag helpers it uses
  // stay public on DrawingApp (shared with the touch path).

  // Touch + hold-to-draw input lives in InputController (inputController.js), invoked from
  // initEventListeners as this.input.wireHoldDraw()/wireTouch(). Callers reach it directly:
  // the renderer via app.input.holdAnchorPoint(), the visuals modal / storage / window.stencil
  // facade via app.input.setHoldDrawDelay().

  // Convert a viewport client point to canvas CSS offset and image-space coords.
  // A comparison view (original / split, or the Alt+Shift+O peek) is read-only: hover
  // tooltips, selection/hover highlights, and every editing gesture are suppressed while
  // it's active. Only navigation (pan/zoom) and the divider drag stay live.
  compareReadOnly() {
    return this.renderer.effectiveCompareMode() !== 'none';
  }

  // …but the coordinate tooltip is DISPLAY, not editing, so it stays live in a
  // comparison — for the points you can actually see. A point behind the original half
  // gets nothing, because labelling something the user is not looking at is a lie
  // (desktop parity). Gate on the POINT's own coordinates, never the cursor's: a point
  // just across the divider from the pointer must not be labelled from the visible side.
  compareShowsPoint(x, y) {
    return compareEditedShows(this.renderer.effectiveCompareMode(), this.compareSplit,
      x, y, this.canvas.width, this.canvas.height);
  }

  // Is a viewport client point within the grab band (8 CSS px) of the split-compare
  // divider? Shared by PointerController (drag start) and updateHover (resize cursor) so
  // the geometry lives once. False unless a split mode ('vertical'/'horizontal') is active.
  nearCompareDivider(clientX, clientY) {
    if (this.compareMode !== 'vertical' && this.compareMode !== 'horizontal') return false;
    const { cssX, cssY } = this.canvasCoords(clientX, clientY);
    const scale = this.scale || 1;
    const f = Math.min(1, Math.max(0, this.compareSplit ?? 0.5));
    return this.compareMode === 'vertical'
      ? Math.abs(cssX - this.canvas.width * f * scale) <= 8
      : Math.abs(cssY - this.canvas.height * f * scale) <= 8;
  }

  canvasCoords(clientX, clientY) {
    const rect = this.canvas.getBoundingClientRect();
    const cssX = clientX - rect.left;
    const cssY = clientY - rect.top;
    // Map through the LIVE on-screen size, not this.scale: mid zoom-transition (the canvas
    // animates width/height) the two disagree and hit-tests land on the wrong point.
    const sx = (rect.width > 0 && this.canvas.width > 0) ? rect.width / this.canvas.width : this.scale;
    const sy = (rect.height > 0 && this.canvas.height > 0) ? rect.height / this.canvas.height : this.scale;
    return { cssX, cssY, x: cssX / sx, y: cssY / sy };
  }

  // opts.crop — explicit crop rect {x,y,width,height} in original-image pixels, overriding
  // the default centered page-aspect crop (external-launch path). opts.source/opts.resource —
  // provenance URLs for add-by-URL + extension hand-off; omitted for local uploads (clears prior).
  loadImageFromFile(file, opts = {}) {
    const replaceInPlace = !!opts.replaceInPlace;
    // A fresh (non-in-place) load starts a DIFFERENT project — drop any .stencil file link the
    // previous project held, or live-sync auto-save would keep writing this new project into the
    // old project's file and overwrite it. A .stencil open re-links right after (see
    // ExportService.pickAndOpenProjectFile); an in-place replace keeps the same project + link.
    if (!replaceInPlace) this.stencilSync?.unlink();
    // Replacing an existing project's image in place: capture what must survive the swap —
    // the annotations to keep, and the OLD image's pin identity to clear — before they're overwritten.
    const keptLines = (replaceInPlace && opts.keepAnnotations) ? this.lines : null;
    const oldImageSource = this.imageSource;
    const oldImageResource = this.imageResource;
    // A different server project becomes its own local project: flush the active one and
    // reset to blank so the promote below makes a distinct record (skip same-project reload / incognito).
    const switchingRemote = !!opts.remoteId
      && (!this.remoteLink || this.remoteLink.remoteId !== opts.remoteId);
    if (switchingRemote && !this.storage.temporary && this.activeProjectId != null
        && !this.storage.incognito) {
      this.storage.save();
      this.storage.newTemporary();
      this.activeProjectId = null;
    }
    // A temporary editor receiving its first image becomes a real project (the final
    // storage.save() persists it; other tabs see it). Exception: incognito editors stay
    // unsaved — do NOT promote, image/lines live in memory only.
    if (!replaceInPlace && (this.storage.temporary || this.activeProjectId == null) && !this.storage.incognito) {
      this.storage.promoteTemporaryToProject();
      this.tabs.reportActive(this.activeProjectId);
    }

    // Store base name and extension for use in download filenames
    const dotIdx = file.name.lastIndexOf('.');
    if (dotIdx !== -1) {
      this.imageBaseName = file.name.slice(0, dotIdx);
      this.imageExt = file.name.slice(dotIdx + 1).toLowerCase();
    } else {
      this.imageBaseName = file.name;
      this.imageExt = 'png';
    }

    // An explicit project name (extension copy-numbering) overrides the filename-
    // derived base used to auto-name the project on first save.
    if (opts.name) this.imageBaseName = opts.name;

    // Provenance comes from the caller (add-by-URL / extension); a plain local
    // upload passes neither, which clears any provenance carried by a prior image.
    this.imageSource = opts.source || null;
    this.imageResource = opts.resource || null;

    // Server linkage. opts.remoteId → reopen an EXISTING server project (link the
    // session, restore its layout below). opts.address WITHOUT a remoteId → CREATE
    // this freshly-loaded image on that server after it loads. Neither → local only.
    // Keep the existing link when replacing in place; otherwise (re)derive it from opts.
    if (!replaceInPlace) {
      this.remoteLink = (opts.address && opts.remoteId)
        ? { address: opts.address, remoteId: opts.remoteId, version: opts.version || 0 }
        : null;
    }
    // The create-on-server target is explicit (opts.address) or armed by a prior
    // newEditor({ address }) — consumed here on the first image load after it. Incognito
    // never creates on a server (central guard covering openImageHere / createBlankImage /
    // the console API). A reopen (opts.remoteId) links an existing project, never creates.
    const armedAddress = opts.address || (opts.remoteId ? null : this.pendingRemoteAddress);
    this.pendingRemoteAddress = null;   // one-shot: consumed (or cleared) by this load
    const remoteCreateAddress = (armedAddress && !opts.remoteId && !this.storage.incognito) ? armedAddress : null;
    const remoteLayout = opts.layout || null;

    const reader = new FileReader();
    reader.onload = event => {
      this.originalImage = new Image();
      this.originalImage.onload = async () => {
        // Auto-crop center to the page aspect (cut surplus sides) via album/portrait
        // detection; original kept, working canvas shows only this region. opts.crop
        // (external-launch) overrides. A reopened server project restores its saved
        // rotation + crop from the layout — rotation FIRST, because the crop rect lives
        // in rotated-original pixel space (#roundRect/defaultCropRect read rotationQuarters).
        this.rotationQuarters = (remoteLayout && Number.isInteger(remoteLayout.rotationQuarters))
          ? remoteLayout.rotationQuarters
          : 0;
        // Quick pre-load edits (load-by-URL modal): opts.page sets page size before the
        // auto-crop, opts.album forces orientation, opts.noCrop loads the full frame.
        if (opts.page) {
          const n = normalizePageSize(opts.page);
          if (n) {
            this.pageSize = n;
            setVal('page-size', n);
          }
        }
        // Restore the project's page format before the crop (defaultCropRect uses the aspect).
        if ((opts.remoteId || opts.adoptLayout) && remoteLayout) this.remoteSync.adoptServerPageFormat(remoteLayout);
        if (remoteLayout && remoteLayout.cropRect) {
          this.cropRect = this.imageModel.roundRect(remoteLayout.cropRect);
        } else if (opts.crop) {
          this.cropRect = this.imageModel.roundRect(opts.crop);
        } else if (opts.noCrop) {
          const { w: iw, h: ih } = this.imageModel.rotatedOriginalDims();
          this.cropRect = this.imageModel.roundRect({ x: 0, y: 0, width: iw, height: ih }, iw, ih);
        } else {
          this.cropRect = this.imageModel.defaultCropRect(opts.album);
        }
        this.imageModel.rebuildCroppedImage();

        // Replacing in place: keep the existing annotations (when asked) over the new image,
        // else start clean — never run the pending-lines re-upload flow.
        if (replaceInPlace) {
          this.lines = keptLines || [];
        }
        // If pending lines exist from a previous session where image couldn't be stored,
        // apply them automatically when user re-uploads an image of matching (crop) size
        else if (this.pendingLines && this.pendingLines.length > 0) {
          const ps = this.pendingImageSize;
          if (!ps || (ps.w === this.canvas.width && ps.h === this.canvas.height)) {
            this.lines = this.pendingLines;
            this.pendingLines = null;
            this.pendingImageSize = null;
            this.storage.showImageMissingBanner(false);
            this.showSaveStatus('Drawing restored!', 'var(--success)', 'check');
          } else {
            if (await this.confirm(`Saved drawing was for a ${ps.w}×${ps.h} image but this image is ${this.canvas.width}×${this.canvas.height}. Apply saved lines anyway?`, { title: 'Size mismatch' }))
              this.lines = this.pendingLines;
            this.pendingLines = null;
            this.pendingImageSize = null;
            this.storage.showImageMissingBanner(false);
          }
        } else {
          this.lines = [];
        }

        // Reopened server project (or an adoptLayout incognito copy): adopt its stored lines +
        // filter/tint (no paste prompts); no stored layout resets the filter to 'none' so the
        // prior project's filter doesn't bleed in (matches desktop openServerProject).
        if (opts.remoteId || opts.adoptLayout) {
          if (remoteLayout) {
            const verdict = validateLayout(remoteLayout, {
              hasImage: true,
              imgW: this.canvas.width,
              imgH: this.canvas.height,
              hasExistingLines: false,
            });
            if (verdict.ok) this.lines = verdict.lines;
            this.remoteSync.adoptServerFilter(remoteLayout);
            this.remoteSync.adoptServerFormulas(remoteLayout);
          } else {
            this.remoteSync.adoptServerFilter({});     // no saved filter — reset to 'none'
            this.remoteSync.adoptServerFormulas({});   // no saved formulas — reset to off
          }
        }

        this.currentLine = null;
        this.history.reset(this.lines);
        // Blank-fill colour for this session: a blank load (createBlankImage / recolour) passes it;
        // any ordinary image load clears it (opts.blankColor undefined → ""). A replace-in-place
        // recolour keeps it. This drives the meta blank/blankColor persisted by storage.save().
        if (opts.blankColor != null) this.blankColor = opts.blankColor;
        else if (!replaceInPlace) this.blankColor = '';
        // File-origin provenance: a .stencil open passes fromFile; any other fresh load clears
        // it (a replace-in-place keeps the project's existing origin).
        if (!replaceInPlace) this.fromFile = !!opts.fromFile;
        // A blank recolor keeps the SAME dimensions (setBlankColor reads them off the
        // current canvas) — nothing to refit, and doing it anyway threw away whatever
        // zoom/pan the user had. `replaceProjectImage`'s swap-in of a different file can
        // genuinely change size/aspect, so that path keeps the fit.
        if (!opts.keepZoom) this.zoomPan.fitToWindow();
        this.updateInfo();
        this.coordTable.update(this.lines.length > 0 ? this.lines[this.lines.length - 1].points : null);
        this.renderer.redraw();
        // Every fresh image gets the dust-assembly arrival (ghostIn — the clear's ghostOut
        // reversed), falling back to the drop-point flight when it can't play. Only an
        // in-place replace is exempt; callers can opt out with `landing: false`.
        if (!replaceInPlace && opts.landing !== false) {
          // Synchronously, in this same tick — a frame's delay would flash the finished
          // image before hiding it. redraw() above already filled the backing store.
          playCanvasArrival(this.canvas, { from: opts.from });
        }
        this.updateButtons();
        this.updateCoordStatus();
        this.storage.save();

        // Adopt a reopened server project's accent colour into the local meta (local-only —
        // the server already holds it), then repaint the name. Applied even when empty so a
        // peer CLEARING the colour propagates too (empty restores the neutral-grey fallback).
        if ((opts.remoteId || opts.adoptLayout) && opts.color != null && this.activeProjectId != null) {
          this.storage.store.setColor(this.activeProjectId, normalizeHex(opts.color) || '');
          this.updateProjectTitle();
        }
        // Restore a .stencil project's search keywords into the local meta (same local-only
        // adopt as the accent colour above; applied via the store so the projects list matches).
        if ((opts.remoteId || opts.adoptLayout) && Array.isArray(opts.keywords) && opts.keywords.length && this.activeProjectId != null) {
          this.storage.store.setKeywords(this.activeProjectId, opts.keywords);
        }

        // Replace-in-place post-steps: optional rename, unpin the OLD image, push the new
        // original to the server when this project is server-linked.
        if (replaceInPlace) {
          if (opts.rename) {
            if (this.activeProjectId != null) this.renameProject(this.activeProjectId, this.imageBaseName);
            this.updateProjectTitle();
          }
          this.#requestUnpin(oldImageSource, oldImageResource, this.imageBaseName);
          if (this.remoteLink) await this.#replaceServerOriginal(file);
        }

        // Create-on-server: push this just-loaded original to the chosen server and
        // link the session so later saves write back. Best-effort — a failure leaves
        // the project local and surfaces a toast.
        if (remoteCreateAddress) {
          try { await this.#createRemoteForSession(remoteCreateAddress, file); }
          catch (err) { notify(`Could not save to server — ${err.message}`, 'fail'); }
        }
        this.#reportIncognitoSession();   // refresh our incognito peer entry with the new name
      };
      this.originalImage.src = event.target.result;
      // Store base64 of the ORIGINAL for persistence (the crop is stored as a
      // rectangle, never baked into the saved image).
      this.imageDataUrl = event.target.result;
    };
    reader.readAsDataURL(file);
  }

  // ── External launch (extension / desktop / bot): `#stencil=<encodeURIComponent(JSON)>` ──
  // Schema/precedence live in normalizeLaunchPayload. Fragment (not query) keeps the payload
  // off servers/logs; consumed once, stripped, routed through the normal upload.
  // `open:'resume'` switches to an existing same-source project; else import a new one.
  applyExternalLaunch() {
    const hash = location.hash || '';
    const prefix = '#stencil=';
    if (!hash.startsWith(prefix)) return;
    // Strip the fragment immediately so a reload doesn't re-import the image.
    history.replaceState(null, '', location.pathname + location.search);

    let payload;
    // Chrome caps fragments around 2M chars, but lax environments don't: bound
    // the raw hash before decode/parse (same cap normalizeLaunchPayload applies
    // to the decoded dataUrl).
    if (hash.length > LAUNCH_DATA_URL_MAX) {
      notify('Stencil: could not read the shared image', 'fail');
      return;
    }
    try {
      payload = JSON.parse(decodeURIComponent(hash.slice(prefix.length)));
    } catch {
      notify('Stencil: could not read the shared image', 'fail');
      return;
    }
    const launch = normalizeLaunchPayload(payload);
    if (!launch) return;

    // Page size must be applied BEFORE loading so the crop aspect and pixel↔page
    // conversion match the size the image was cropped for by the sender.
    if (launch.page) this.#setExternalPage(launch.page);

    if (launch.incognito) {
      this.storage.incognito = true;
      this.updateIncognitoUI();
    }

    if (launch.kind === 'server') {
      this.#applyServerLaunch(launch)
        .catch(err => notify(`Could not open the server project — ${err.message}`, 'fail'));
      return;
    }

    const name = launch.name || 'image.png';
    const source = launch.source;

    // Resume: if we hold project(s) for this source, switch instead of re-importing.
    // Several matches → open the projects list to pick. No match (stale ledger / expired
    // project) falls through to a fresh import.
    if (launch.open === 'resume' && !this.storage.incognito && (source || name)
        && this.#resumeBySource(source, name)) {
      return;
    }

    // Fresh import (`open:'copy'` takes the same path).
    importInlineImage(this, launch)
      .catch(() => notify('Stencil: failed to load the shared image', 'fail'));
  }

  // The extension bridge's entry point: import a hand-off into THIS tab instead of a fresh one.
  // For 'new', do explicitly what the fragment path gets free from a fresh page: flush + reset
  // to a blank editor (loadImageFromFile only promotes a TEMPORARY one), and apply the page
  // size BEFORE the load so the crop aspect matches the sender's. A replace keeps identity.
  importExternalImage(launch, { mode = 'new' } = {}) {
    if (mode === 'new') {
      const incognito = this.storage.incognito;
      if (!incognito) this.storage.save();
      this.newEditor();
      if (incognito) { this.storage.incognito = true; this.updateIncognitoUI(); }
      if (launch.page) this.#setExternalPage(launch.page);
    }
    return importInlineImage(this, launch, { mode });
  }

  // Switch to an existing project matching this image, without importing. Returns true when
  // it switched (so the caller can stop). Shared by the resume launch path above and the
  // extension's "resume in the open editor tab" nudge (stencil:switch-to-source), which lets
  // the extension re-focus this tab instead of spawning a new one.
  #resumeBySource(source, name) {
    const baseName = stripExt(name || '');
    const matches = this.storage.store.findByImage(source, baseName);
    if (matches.length && this.switchToProject(matches[0].id)) {
      if (matches.length > 1) {
        notify(`Resumed "${shortName(matches[0].name)}" — ${matches.length} projects share this image`, 'ok');
        // This fires from the BOOT path (an external launch, before the very first
        // frame has necessarily painted) — a click landing before the toolbar has a
        // real, laid-out box sends the modal's icon-origin flight measuring a 0×0
        // rect, and it falls back to dropping in from above instead of the icon a
        // moment later shows as perfectly visible. One frame is enough to be sure.
        requestAnimationFrame(() => document.getElementById('projects-btn')?.click());
      }
      return true;
    }
    return false;
  }

  // Open a server project referenced by an external launch: connect to the server the
  // way a user would from the connect modal (reuse the live connection, else a saved
  // token, else mint one via POST /auth/token), then open the project — as an unlinked
  // incognito copy when the launch asked for incognito, else as the normal linked open.
  async #applyServerLaunch(launch) {
    let url;
    try { url = normalizeUrl(launch.server.url); } catch { throw new Error('bad server URL'); }
    if (!this.connections.has(url)) {
      const saved = loadSavedServers().find(s => {
        try { return normalizeUrl(s.url) === url; } catch { return false; }
      });
      // A link can name ANY server — don't let a drive-by URL silently add a
      // (persisted) connection to an origin this browser has never used. Known
      // origins (live or saved) skip the prompt.
      if (!saved && !(await this.confirm(
        `This link opens a shared project on ${url}. Connect to that server?`,
        { title: 'Open shared project', confirmLabel: 'Connect', confirmIcon: 'link' }))) {
        return;
      }
      try {
        await this.connections.connect({ url, token: (saved && saved.token) || '' });
      } catch (err) {
        // The normal connect error path: surface the failure and open the connect modal
        // so the user can supply a token / fix the URL.
        notify(`Could not connect to ${url} — ${err.message}`, 'fail');
        document.getElementById('connect-btn')?.click();
        return;
      }
    }
    if (launch.incognito) {
      await this.copyServerProjectToIncognito({ serverUrl: url, id: launch.server.id }, {});
    } else {
      await this.openRemoteProject({ serverUrl: url, id: launch.server.id });
    }
  }

  openRemoteProject(meta) { return this.projectTransfer.openRemoteProject(meta); }

  // Apply a page size handed in by the external launch and reflect it in the UI.
  // page.width/height are in cm (only used for the 'custom' size).
  #setExternalPage(page) {
    const size = normalizePageSize(page.size) || 'A3';
    this.pageSize = size;
    if (size === 'custom') {
      const w = parseFloat(page.width), h = parseFloat(page.height);
      if (!isNaN(w) && w > 0) this.customPageWidth = w;
      if (!isNaN(h) && h > 0) this.customPageHeight = h;
    }
    const sel = document.getElementById('page-size');
    if (sel) sel.value = size;
    revealControls(document.getElementById('custom-size-group'), size === 'custom');
    this.applyUnitToUI();   // refresh the custom width/height inputs in the active unit
    this.coordTable.update();
  }

  // Crop / quarter-turn rotation / image geometry live in ImageModel (imageModel.js), backed
  // by cropGeometry.js. Callers reach it directly via app.imageModel.<method>() — storage, the
  // crop modal, and the window.stencil facade (defaultCropRect / effectiveOriginalDims /
  // effectiveOriginalDataUrl / rebuildCroppedImage / rotateImage / applyCrop).

  // Selection-panel DOM sync lives in ui/selectionPanel.js; thin delegators (public — used
  // by ImageModel's after-geometry-change refresh, controlsBinder, and fullscreenLayer too).
  hideSelectionPanels() { selectionPanel.hideSelectionPanels(); }

  // `opts.from` is the drop point, when this came from a drag-and-drop (controlsBinder).
  loadJSONFromFile(file, opts = {}) {
    const reader = new FileReader();
    reader.onload = event => {
      try {
        const data = JSON.parse(event.target.result);
        if (!data || !Array.isArray(data.lines)) {
          notify('File is not a valid layout', 'fail');
          return;
        }
        this.export.applyPastedLayout(data, opts.from);
      } catch (err) {
        notify('Error loading JSON: ' + err.message, 'fail');
      }
    };
    reader.readAsText(file);
  }

  startDrawingMode(opts = {}) {
    if (this.compareReadOnly()) return; // read-only compare view
    if (!this.image) {
      notify('Please upload an image first', 'fail');
      return;
    }
    this.isDrawing = true;

    // Continuation: a line is selected → connect the new drawing to it and
    // adopt its style (new points/rects become part of the selected line).
    if (opts.connect !== false && this.selectedLineIdx >= 0 && this.lines[this.selectedLineIdx]) {
      this.continueLineIdx = this.selectedLineIdx;
      const line = this.lines[this.continueLineIdx];
      this.continueInsertIdx = resolveInsertIdx(line, {
        coordLineIdx: this.coordLineIdx,
        selectedLineIdx: this.selectedLineIdx,
        focusedPtIdx: this.focusedPtIdx
      });
      this.currentLine = null;
      this.undonePoints = [];
      this.coordLineIdx = this.continueLineIdx;
      this.coordTable.update(line.points, this.continueLineIdx);
      this.updateButtons();
      this.renderer.redraw();
      notify('Continuing selected line — new points connect to it', 'info');
      return;
    }

    this.continueLineIdx = -1;
    this.continueInsertIdx = -1;
    this.currentLine = {
      points: [],
      color: this.color,
      // Resolved AT DRAW TIME (empty setting → the current line colour) so a later
      // line-colour change never recolours already-drawn points.
      pointColor: this.pointColor || this.color,
      thickness: this.thickness,
      pointSize: this.pointSize,
      style: this.style
    };
    if (!opts.keepSelection) {
      this.selectedLineIdx = -1;
      this.hideSelectionPanels();
    }
    this.undonePoints = []; // stack for redo while drawing
    this.updateButtons();
    this.renderer.redraw();
  }

  // The Draw group's single Start/Stop control. Driven from updateButtons(), which every
  // isDrawing transition already ends with — one place knows what the button should say.
  syncDrawToggleUI() {
    const btn = document.getElementById('draw-toggle');
    if (!btn) return;
    const on = !!this.isDrawing;
    const face = (stop) => icon(stop ? 'stop' : 'play', { size: 13 }) +
      `<span>${stop ? 'Stop' : 'Start'}</span>`;
    // Pin the box to the WIDER of the two faces before swapping into either, so the
    // toggle never resizes under the cursor (measured, not guessed — see motion.js).
    pinWidestFace(btn, [face(false), face(true)]);
    // The face swaps on the shared transition (motion.js): the markup is written
    // synchronously, so however fast the toggling, what the button shows is isDrawing.
    swapContent(btn, face(on), { key: on ? 'stop' : 'start' });
    btn.classList.toggle('active', on);
    // The tooltip's hotkey follows the state too: Alt+A starts, Alt+S stops.
    btn.dataset.hkTitle = on ? 'stopDraw' : 'startDraw';
    btn.dataset.title = on ? 'Stop Drawing' : 'Start Drawing';
    btn.dataset.tip = composeControlTitle(btn, hotkeys.isMac, id => hotkeys.get(id));
  }

  setDrawMode(mode) {
    this.drawMode = (mode === 'rect') ? 'rect' : 'line';
    this.syncDrawModeUI();
  }

  syncDrawModeUI() {
    const btn = document.getElementById('draw-mode-toggle');
    if (btn) {
      const rect = this.drawMode === 'rect';
      const face = (r) => (r ? DRAW_MODE_ICON.rect : DRAW_MODE_ICON.line) +
        (r ? '<span>Rect</span>' : '<span>Line</span>');
      // Same pin and the same swap as Start/Stop — one idiom for the whole Draw group.
      // The two pairs may settle on different widths; only each button's own stability
      // matters. ("Line" and "Rect" are near-identical, "Start"/"Stop" are not.)
      pinWidestFace(btn, [face(false), face(true)]);
      swapContent(btn, face(rect), { key: this.drawMode });
      btn.dataset.title = this.drawMode === 'rect'
        ? 'Drawing mode: Rectangle (click to switch to Line)'
        : 'Drawing mode: Line (click to switch to Rectangle)';
      btn.dataset.tip = composeControlTitle(btn, hotkeys.isMac, id => hotkeys.get(id));
    }
  }

  stopDrawingMode() {
    // Continuation drawing: the line is already in this.lines — just commit & reset
    if (this.continueLineIdx >= 0) {
      const li = this.continueLineIdx;
      this.continueLineIdx = -1;
      this.continueInsertIdx = -1;
      this.currentLine = null;
      this.isDrawing = false;
      if (this.lines[li]) this.coordTable.update(this.lines[li].points, li);
      this.saveHistory();
      this.renderer.redraw();
      this.updateButtons();
      return;
    }
    if (this.currentLine && this.currentLine.points.length > 0) {
      if (this.currentLine.points.length > 1) {
        this.lines.push(this.currentLine);
        this.coordLineIdx = this.lines.length - 1;
        this.saveHistory();
      } else {
        this.coordLineIdx = -1;
      }
      this.coordTable.update(this.currentLine.points, this.coordLineIdx);
    } else {
      this.coordTable.update();
    }
    this.currentLine = null;
    this.isDrawing = false;
    this.renderer.redraw();
    this.updateButtons();
  }

  // The set of currently-selected line indices. In ordinary single-select mode `selectedLines`
  // is empty and this returns [selectedLineIdx] (or []) — so nothing else changes; in multi-select
  // mode it returns the explicit set. Always filtered to valid, in-range indices.
  selectedIndices() {
    const src = this.selectedLines.length ? this.selectedLines : (this.selectedLineIdx >= 0 ? [this.selectedLineIdx] : []);
    return src.filter((i) => i >= 0 && i < this.lines.length);
  }

  // True while `i` is part of the current selection (single or multi) — drives the renderer glow.
  isLineSelected(i) {
    return this.selectedLines.length ? this.selectedLines.includes(i) : i === this.selectedLineIdx;
  }

  // Ctrl/⌘+Shift+click: add/remove `idx` from the multi-select set. Clicking a line already in the
  // set removes it. With exactly one line left, we drop back to normal single-select (its editor
  // reappears); with 2+, the single-line editor is hidden (ambiguous which line to edit).
  #toggleLineSelection(idx) {
    // Seed the set from the current single selection the first time you Ctrl+Shift+click.
    if (!this.selectedLines.length && this.selectedLineIdx >= 0 && this.selectedLineIdx !== idx)
      this.selectedLines = [this.selectedLineIdx];
    const at = this.selectedLines.indexOf(idx);
    if (at >= 0) this.selectedLines.splice(at, 1);
    else this.selectedLines.push(idx);

    if (this.selectedLines.length === 1) {
      // Back to a single selection — restore its editor + coord table.
      this.selectedLineIdx = this.selectedLines[0];
      this.selectedLines = [];
      this.showSelectionPanel(this.lines[this.selectedLineIdx]);
      this.coordLineIdx = this.selectedLineIdx;
      this.focusedPtIdx = -1;
      this.coordTable.update(this.lines[this.selectedLineIdx].points, this.selectedLineIdx);
    } else {
      // 0 or 2+ selected: no single-line editor.
      this.selectedLineIdx = -1;
      this.hideSelectionPanels();
    }
    this.updateMultiSelectStatus();
    this.renderer.redraw();
  }

  // Show a brief "N lines selected" note in the status line while multi-selecting (2+); clear it
  // otherwise. Mirrors the desktop status bar.
  updateMultiSelectStatus() {
    const el = document.getElementById('coord-status');
    if (!el) return;
    const n = this.selectedLines.length;
    if (n >= 2) el.textContent = `${n} lines selected — ⌘/Ctrl+Shift+click to add/remove · Alt+Shift+drag to move all · Ctrl+Shift+scroll to rotate all`;
    else if (el.dataset.multi) el.textContent = '';
    el.dataset.multi = n >= 2 ? '1' : '';
    this.renderLinesList();
  }

  // Select a single line from the "Lines" tab list (or console) — mirrors the canvas
  // "click on a segment" path (canvasClick priority 2), but keyed by index so the list
  // and the canvas stay in sync. Clears any multi-selection first. `ctrlShift` toggles it
  // into/out of the multi-select set instead (so the list mirrors ⌘/Ctrl+Shift+click).
  selectLineFromList(idx, ctrlShift = false) {
    if (idx < 0 || idx >= this.lines.length) return this;
    if (ctrlShift) { this.#toggleLineSelection(idx); return this; }
    this.selectedLines = [];
    this.selectedLineIdx = idx;
    this.showSelectionPanel(this.lines[idx]);
    this.coordLineIdx = idx;
    this.focusedPtIdx = -1;
    this.coordTable.update(this.lines[idx].points, idx);
    this.updateMultiSelectStatus();
    this.renderer.redraw();
    return this;
  }

  // Tint the Lines-list row matching the line under the canvas cursor. Class toggle only —
  // the list is never scrolled by a canvas hover.
  applyLinesListHover() {
    const el = document.getElementById('lines-list');
    if (!el) return;
    el.querySelectorAll('.lines-row').forEach(r => {
      r.classList.toggle('lines-row-hover', parseInt(r.dataset.idx) === this.hoverLineIdx);
    });
  }

  // Hovering a Lines-list row glows that line on the canvas (the reverse direction of
  // applyLinesListHover). -1 / out-of-range clears the glow.
  setListHoverLine(idx) {
    const i = (typeof idx === 'number' && idx >= 0 && idx < this.lines.length) ? idx : -1;
    if (i === this.listHoverLineIdx) return;
    this.listHoverLineIdx = i;
    this.renderer.redraw();
  }

  // Rebuild the "Lines" tab list — one row per committed line (color chip, index, point/segment
  // count, area badge), reflecting the current selection. Rows single-select on click (⌘/Ctrl+Shift
  // toggles multi-select) and carry a 🗑 to remove the line. No-ops unless the Lines tab is showing,
  // so the redraw/updateButtons hooks that call it stay cheap while the Points tab is active.
  renderLinesList() {
    const el = document.getElementById('lines-list');
    if (!el || el.style.display === 'none') return;
    el.replaceChildren();
    if (!this.lines.length) {
      const empty = document.createElement('div');
      empty.className = 'lines-empty';
      empty.textContent = 'No lines yet.';
      el.appendChild(empty);
      return;
    }
    this.lines.forEach((line, i) => {
      const row = document.createElement('div');
      row.className = 'lines-row' + (this.isLineSelected(i) ? ' lines-row-selected' : '');
      if (i === this.hoverLineIdx) row.classList.add('lines-row-hover');
      row.dataset.idx = String(i);
      // Hovering the row glows its line on the canvas (and clears on leave).
      row.addEventListener('mouseenter', () => this.setListHoverLine(i));
      row.addEventListener('mouseleave', () => this.setListHoverLine(-1));

      const swatch = document.createElement('span');
      swatch.className = 'lines-swatch';
      swatch.style.background = line.locked && !fillState(line, this.defaultFillColor).enabled
        ? 'transparent' : (line.color || '#ffff00');
      swatch.style.borderColor = line.color || '#ffff00';

      const label = document.createElement('span');
      label.className = 'lines-label';
      const np = line.points.length;
      const parts = [`Line ${i + 1}`, `${np} pt${np === 1 ? '' : 's'}`];
      if (line.locked) parts.push('area');
      label.textContent = parts.join(' · ');

      const rm = document.createElement('button');
      rm.className = 'lines-remove btn-icon';
      rm.type = 'button';
      rm.dataset.title = 'Remove line';
      rm.setAttribute('aria-label', `Remove line ${i + 1}`);
      rm.innerHTML = icon('trash', { size: 13 });
      rm.addEventListener('click', (e) => {
        e.stopPropagation();
        if (this.compareReadOnly()) return; // read-only compare view
        // Collapse the row away first — renderLinesList() then rebuilds without it.
        leaveThenRemove(row, () => this.removeLine(i));
      });

      row.addEventListener('click', (e) => {
        this.selectLineFromList(i, (e.ctrlKey || e.metaKey) && e.shiftKey);
      });
      // Focusable so Delete/Backspace can be scoped to this list, matching the points
      // table's rows. Bare key, same core path as the row's 🗑; the global Alt+Delete
      // (which works from anywhere, on the canvas selection) is untouched.
      row.tabIndex = 0;
      row.addEventListener('keydown', (e) => {
        if (e.key !== 'Delete' && e.key !== 'Backspace') return;
        if (this.compareReadOnly()) return; // read-only compare view
        e.preventDefault();
        e.stopPropagation();
        this.removeLine(i);
        const rows = el.querySelectorAll('.lines-row');
        if (rows.length) rows[Math.min(i, rows.length - 1)].focus();
      });
      row.append(swatch, label, rm);
      el.appendChild(row);
    });
  }

  canvasClick(e) {
    // No image → the canvas is an empty void; don't let clicks drop points.
    if (!this.image) return;
    // Compare view is read-only — clicks never add/select/edit.
    if (this.compareReadOnly()) return;
    // Ctrl/⌘+Shift+click → multi-select: add/toggle the clicked line (handled BEFORE the alt/shift
    // early-returns below). Clicking empty space keeps the current set.
    if ((e.ctrlKey || e.metaKey) && e.shiftKey) {
      const { x, y } = this.canvasCoords(e.clientX, e.clientY);
      const nearPt = this.findNearestPointWithIdx(x, y);
      const idx = (nearPt && nearPt.lineIdx !== -1) ? nearPt.lineIdx : this.findLineAt(x, y);
      if (idx !== -1) this.#toggleLineSelection(idx);
      return;
    }
    // Ignore click that ended a pan gesture or point drag
    if (e.altKey) return;
    if (e.shiftKey) return; // Shift+drag is for zoom-area rect
    if (this.dragJustEnded) return;
    const { x, y } = this.canvasCoords(e.clientX, e.clientY);

    if (this.isDrawing) {
      if (this.drawMode === 'rect') return; // rect areas are created by dragging

      // Ctrl/Cmd+click on an existing committed segment → insert a point BETWEEN
      // that segment's two endpoints (same as Ctrl+click outside drawing mode),
      // instead of appending it at the line's tail with a connecting segment.
      if (e.ctrlKey || e.metaKey) {
        const nearSeg = this.findNearestSegmentWithIdx(x, y);
        if (nearSeg) {
          this.insertPointOnSegment(nearSeg.lineIdx, nearSeg.ptIdx2, x, y);
          // Inserting shifts later indices right by one; keep the continuation
          // tail anchored to the same logical spot on the line we're extending.
          if (nearSeg.lineIdx === this.continueLineIdx && nearSeg.ptIdx2 <= this.continueInsertIdx)
            this.continueInsertIdx++;
          return;
        }
        // No segment under the cursor → fall through to normal drawing behavior.
      }

      // A click near the first point closes the stroke into a locked area — whichever
      // stroke is being drawn (continued or fresh).
      if (this.tryCloseShapeAt(x, y)) return;

      // Continuation drawing: extend the selected line at the insert point
      if (this.continueLineIdx >= 0 && this.lines[this.continueLineIdx]) {
        const line = this.lines[this.continueLineIdx];
        line.points.splice(this.continueInsertIdx, 0, { x, y });
        this.strokeFx.flyIn(line, this.continueInsertIdx);
        this.focusedPtIdx = this.continueInsertIdx;
        this.continueInsertIdx++;
        this.coordTable.update(line.points, this.continueLineIdx);
        this.renderer.redraw();
        this.updateButtons();
        return;
      }

      this.undonePoints = [];
      this.currentLine.points.push({ x, y });
      this.strokeFx.flyIn(this.currentLine, this.currentLine.points.length - 1);
      this.renderer.redraw();
      this.updateButtons();
      return;
    }

    // Ctrl/Cmd click → add or insert a point
    if (e.ctrlKey || e.metaKey) {
      const nearSeg = this.findNearestSegmentWithIdx(x, y);
      if (nearSeg) {
        // Hovering a line → insert a point between its two connecting points
        this.insertPointOnSegment(nearSeg.lineIdx, nearSeg.ptIdx2, x, y);
      } else {
        // Empty space → add a new point (connected to selection if any)
        this.#addConnectedPoint(x, y);
      }
      return;
    }

    // A plain click leaves multi-select mode (back to single-line selection).
    this.selectedLines = [];
    this.updateMultiSelectStatus();

    // Non-drawing mode: priority 1 — click on a point of any committed line
    // → select that line, focus the clicked point in the coord table
    const nearPt = this.findNearestPointWithIdx(x, y);
    if (nearPt && nearPt.lineIdx !== -1) {
      this.selectedLineIdx = nearPt.lineIdx;
      this.showSelectionPanel(this.lines[nearPt.lineIdx]);
      this.coordLineIdx = nearPt.lineIdx;
      this.focusedPtIdx = nearPt.ptIdx;
      this.coordTable.update(this.lines[nearPt.lineIdx].points, nearPt.lineIdx);
      this.renderer.redraw();
      const row = this.coordinatesBody.querySelector(`tr[data-pt-idx="${nearPt.ptIdx}"]`);
      if (row && typeof row.scrollIntoView === 'function') {
        row.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
      }
      return;
    }

    // Priority 2 — click on a line segment → select the line
    const idx = this.findLineAt(x, y);
    if (idx !== -1) {
      this.selectedLineIdx = idx;
      this.showSelectionPanel(this.lines[idx]);
      this.coordLineIdx = idx;
      this.focusedPtIdx = -1;
      this.coordTable.update(this.lines[idx].points, idx);
    } else {
      this.deselectLine();
    }
    this.renderer.redraw();
  }

  // Like every other hit test here, the close grab is a screen radius divided by the
  // zoom — at 25% a fixed image-pixel radius was three screen pixels and unhittable.
  // core adds its own +8, so hand it the size that makes the total screen-constant.
  // Zoomed out only: magnifying must never make the dots harder to hit than at 1:1.
  static #CLOSE_SLACK = 8;
  #closeGrabSize(line) {
    const ps = line.pointSize ?? this.pointSize;
    const scale = this.scale || 1;
    if (scale >= 1) return ps;
    const slack = DrawingApp.#CLOSE_SLACK;
    return Math.max(ps, (ps + slack) / scale - slack);
  }

  // Would a point at (x, y) close the stroke being drawn? If so, close it into a locked
  // area and report it. The one close route: the click path and hold-to-draw both come
  // here, so a shape closes however the last point is put down.
  tryCloseShapeAt(x, y) {
    if (!this.isDrawing) return false;
    if (this.continueLineIdx >= 0 && this.lines[this.continueLineIdx]) {
      const line = this.lines[this.continueLineIdx];
      if (!this.#shouldCloseShape(line.points, x, y, this.#closeGrabSize(line))) return false;
      this.#closeContinuedShape();
      return true;
    }
    if (!this.currentLine) return false;
    if (!this.#shouldCloseShape(this.currentLine.points, x, y, this.#closeGrabSize(this.currentLine)))
      return false;
    this.#closeCurrentShape();
    return true;
  }

  // Close the in-progress line into a locked, fillable area.
  #closeCurrentShape() {
    this.#closeShape({ line: this.currentLine, isContinuation: false });
  }

  // Close a line that is being extended (continuation drawing) into a locked area.
  #closeContinuedShape() {
    this.#closeShape({ line: this.lines[this.continueLineIdx], idx: this.continueLineIdx, isContinuation: true });
  }

  // Unified close: append a coincident closing point, lock + default-fill the
  // line, commit it, and select the resulting area. A fresh shape is pushed
  // into this.lines; a continued shape is already there (reset continue state).
  #closeShape({ line, idx, isContinuation }) {
    if (!line || line.points.length < 3) return;
    // Append a closing point coincident with the first, then lock it
    line.points.push({ x: line.points[0].x, y: line.points[0].y });
    line.locked = true;
    if (line.fillColor === undefined) line.fillColor = 'transparent';
    let areaIdx;
    if (isContinuation) {
      this.continueLineIdx = -1;
      this.continueInsertIdx = -1;
      areaIdx = idx;
    } else {
      this.lines.push(line);
      areaIdx = this.lines.length - 1;
    }
    this.currentLine = null;
    this.isDrawing = false;
    // Ends exactly like finishing an ordinary line (stopDrawingMode): the coordinate
    // table follows it and nothing is selected, so no bar pops up over the new shape.
    this.coordLineIdx = areaIdx;
    this.coordTable.update(this.lines[areaIdx].points, areaIdx);
    // A CONTINUED shape was drawn on an already-selected line, so its bar is already up:
    // repopulate it (it just became an area and grew a Fill control) without opening it —
    // the panel is visible, so this replays no animation.
    if (this.selectedLineIdx === areaIdx) this.showSelectionPanel(this.lines[areaIdx]);
    this.saveHistory();
    this.renderer.redraw();
    this.updateButtons();
    notify('Shape closed — locked area created', 'ok');
  }

  insertPointOnSegment(lineIdx, insertIdx, x, y) {
    const line = this.lines[lineIdx];
    if (!line) return;
    line.points.splice(insertIdx, 0, { x, y });
    // An inserted vertex comes out of the segment it split — from its own foot on the
    // old straight line, so the bend grows rather than appearing.
    this.strokeFx.flyIn(line, insertIdx, strokeFoot(line.points[insertIdx - 1], line.points[insertIdx + 1], x, y));
    this.selectedLineIdx = lineIdx;
    this.coordLineIdx = lineIdx;
    this.focusedPtIdx = insertIdx;
    this.showSelectionPanel(line);
    this.coordTable.update(line.points, lineIdx);
    this.saveHistory();
    this.renderer.redraw();
    this.updateButtons();
  }

  // Add a new standalone point — or, if a line/point is selected, connect the
  // new point to that line's last point (or to the focused point), inheriting
  // the selected line's style (subtask: connect new geometry to selection).
  #addConnectedPoint(x, y) {
    if (this.selectedLineIdx >= 0 && this.lines[this.selectedLineIdx]) {
      const line = this.lines[this.selectedLineIdx];
      const insertIdx = resolveInsertIdx(line, {
        coordLineIdx: this.coordLineIdx,
        selectedLineIdx: this.selectedLineIdx,
        focusedPtIdx: this.focusedPtIdx
      });
      line.points.splice(insertIdx, 0, { x, y });
      this.strokeFx.flyIn(line, insertIdx);
      this.coordLineIdx = this.selectedLineIdx;
      this.focusedPtIdx = insertIdx;
      this.showSelectionPanel(line);
      this.coordTable.update(line.points, this.selectedLineIdx);
      this.saveHistory();
      this.renderer.redraw();
      this.updateButtons();
      return;
    }
    const newLine = {
      points: [{ x, y }],
      color: this.color,
      pointColor: this.pointColor || this.color,   // resolved at draw time (see startDrawingMode)
      thickness: this.thickness,
      pointSize: this.pointSize,
      style: this.style
    };
    this.lines.push(newLine);
    this.strokeFx.flyIn(newLine, 0);
    const idx = this.lines.length - 1;
    this.selectedLineIdx = idx;
    this.coordLineIdx = idx;
    this.focusedPtIdx = 0;
    this.showSelectionPanel(newLine);
    this.coordTable.update(newLine.points, idx);
    this.saveHistory();
    this.renderer.redraw();
    this.updateButtons();
  }

  // Create a rectangle (4 corner points, locked/fillable area). If a line is
  // selected, the rect's corners are appended to it (connecting to its last/
  // focused point) using that line's style; otherwise a new locked line is made.
  createRect(x1, y1, x2, y2, connect = false) {
    const xa = Math.min(x1, x2);
    const xb = Math.max(x1, x2);
    const ya = Math.min(y1, y2);
    const yb = Math.max(y1, y2);
    const corners = [
      { x: xa, y: ya }, { x: xb, y: ya },
      { x: xb, y: yb }, { x: xa, y: yb }
    ];
    // Continuation drawing → append the corners to the line being extended
    if (this.continueLineIdx >= 0 && this.lines[this.continueLineIdx]) {
      const line = this.lines[this.continueLineIdx];
      const insertIdx = this.continueInsertIdx;
      line.points.splice(insertIdx, 0, ...corners);
      this.strokeFx.flyInRange(line, insertIdx, corners.length);
      this.continueInsertIdx = insertIdx + corners.length;
      this.coordLineIdx = this.continueLineIdx;
      this.focusedPtIdx = this.continueInsertIdx - 1;
      this.coordTable.update(line.points, this.continueLineIdx);
      this.saveHistory();
      this.renderer.redraw();
      this.updateButtons();
      return;
    }
    if (connect && this.selectedLineIdx >= 0 && this.lines[this.selectedLineIdx]) {
      const line = this.lines[this.selectedLineIdx];
      const insertIdx = resolveInsertIdx(line, {
        coordLineIdx: this.coordLineIdx,
        selectedLineIdx: this.selectedLineIdx,
        focusedPtIdx: this.focusedPtIdx
      });
      line.points.splice(insertIdx, 0, ...corners);
      this.strokeFx.flyInRange(line, insertIdx, corners.length);
      this.coordLineIdx = this.selectedLineIdx;
      this.focusedPtIdx = insertIdx;
      this.showSelectionPanel(line);
      this.coordTable.update(line.points, this.selectedLineIdx);
      this.saveHistory();
      this.renderer.redraw();
      this.updateButtons();
      return;
    }
    const rect = {
      points: corners,
      color: this.color,
      pointColor: this.pointColor || this.color,   // resolved at draw time (see startDrawingMode)
      thickness: this.thickness,
      pointSize: this.pointSize,
      style: this.style,
      locked: true,
      fillColor: 'transparent'
    };
    this.lines.push(rect);
    this.strokeFx.flyInRange(rect, 0, corners.length);
    const idx = this.lines.length - 1;
    this.selectedLineIdx = idx;
    this.coordLineIdx = idx;
    this.focusedPtIdx = -1;
    this.showSelectionPanel(rect);
    this.coordTable.update(rect.points, idx);
    this.saveHistory();
    this.renderer.redraw();
    this.updateButtons();
  }

  canvasMouseMove(e) {
    this.mouseOverCanvas = true;
    this.lastMouseClientX = e.clientX;
    this.lastMouseClientY = e.clientY;

    // No image → empty void: no coordinate tooltip, no hover cursor, idle status.
    if (!this.image) {
      this.tooltipMgr.hide();
      this.canvas.style.cursor = 'default';
      this.updateCoordStatus();
      return;
    }

    // Mid drag/hold, the tooltip has no business on screen (same states tooltip.js
    // refresh() excludes) — drop one already showing and don't offer a new one.
    if (this.isPanning || this.isDraggingPoint || this.isDraggingSegment ||
        this.isDraggingLine || this.isZoomRectDragging || this.isRectDrawDragging ||
        this.input.holdEngaged) {
      this.tooltipMgr.hide();
      return;
    }

    const { x, y } = this.canvasCoords(e.clientX, e.clientY);

    // Persistent cursor-coordinate readout (mirrors the desktop status bar). A passive
    // readout of where the cursor IS, so it must come BEFORE the compare returns below —
    // otherwise the strip freezes for the whole compare session.
    this.updateCoordStatus(x, y);

    // Split compare: show a resize cursor over the movable divider (skip the normal hover
    // cursor + tooltip so the affordance reads clearly). Dragging is handled in PointerController.
    if (!e.altKey && !e.shiftKey && !e.ctrlKey && !e.metaKey && !this.compareHoldOriginal &&
        (this.nearCompareDivider(e.clientX, e.clientY) || this.isDraggingCompareSplit)) {
      this.canvas.style.cursor = this.compareMode === 'vertical' ? 'col-resize' : 'row-resize';
      this.tooltipMgr.hide();
      return;
    }

    // Compare view is read-only for EDITING — no hover ring, no edit cursor, no drag.
    // The coordinate tooltip is display, so it still answers here, for a point or line
    // visible in the edited half (tooltipMgr.applyHover applies that gate).
    if (this.compareReadOnly()) {
      this.canvas.style.cursor = 'default';
      this.tooltipMgr.applyHover(e.clientX, e.clientY, x, y, e);
      return;
    }

    // Track hovered point on ANY line (drives the hover ring), and keep the
    // coord-table row highlight in sync when the point belongs to the shown line.
    const nearPtIdx = this.findNearestPointWithIdx(x, y);
    const newHoverPt = nearPtIdx ? { lineIdx: nearPtIdx.lineIdx, ptIdx: nearPtIdx.ptIdx } : null;
    const hoverChanged =
      (!!this.hoverPt !== !!newHoverPt) ||
      (this.hoverPt && newHoverPt &&
        (this.hoverPt.lineIdx !== newHoverPt.lineIdx || this.hoverPt.ptIdx !== newHoverPt.ptIdx));
    this.hoverPt = newHoverPt;
    let newHover = -1;
    if (nearPtIdx && nearPtIdx.lineIdx === this.coordLineIdx) newHover = nearPtIdx.ptIdx;
    const rowChanged = newHover !== this.hoveredPtIdx;
    if (rowChanged) { this.hoveredPtIdx = newHover; this.coordTable.applyRowHighlight(); }
    if (hoverChanged || rowChanged) this.renderer.redraw();

    // Track the LINE under the cursor too (a point hit names its line; else a stroke hit):
    // it tints the matching Lines-list row — the reverse of the list-row hover glow. Never
    // scrolls the list.
    const overLineIdx = (nearPtIdx && nearPtIdx.lineIdx !== -1)
      ? nearPtIdx.lineIdx : this.findLineAt(x, y);
    if (overLineIdx !== this.hoverLineIdx) {
      this.hoverLineIdx = overLineIdx;
      this.applyLinesListHover();
    }

    // Alt key held → drag-ready cursors, no tooltip
    if (e.altKey) {
      if (e.shiftKey) {
        // Alt+Shift: whole-line drag mode
        this.canvas.style.cursor = overLineIdx !== -1 ? 'move' : 'grab';
      } else {
        // Alt: point drag > segment drag > pan
        const nearSeg = nearPtIdx ? null : this.findNearestSegmentWithIdx(x, y);
        this.canvas.style.cursor = (nearPtIdx || nearSeg) ? 'move' : 'grab';
      }
      this.tooltipMgr.hide();
      return;
    }

    if ((e.ctrlKey || e.metaKey) && !e.shiftKey) {
      // Ctrl → point-add mode
      this.canvas.style.cursor = 'copy';
    } else if (e.shiftKey && !this.isZoomRectDragging) {
      this.canvas.style.cursor = 'zoom-in';
    } else if (this.isDrawing && this.drawMode === 'rect') {
      this.canvas.style.cursor = 'crosshair';
    } else if (!this.isDrawing) {
      this.canvas.style.cursor = overLineIdx !== -1 ? 'pointer' : 'crosshair';
    } else {
      this.canvas.style.cursor = 'crosshair';
    }

    this.tooltipMgr.applyHover(e.clientX, e.clientY, x, y, e);
  }

  canvasDblClick(e) {
    if (this.isDrawing) return;
    if (this.compareReadOnly()) return; // read-only compare view — no double-click delete
    if (e.altKey) return; // Alt+dblclick is reserved for zoom reset

    const { x, y } = this.canvasCoords(e.clientX, e.clientY);

    const idx = this.findLineAt(x, y);
    if (idx !== -1) {
      this.lines.splice(idx, 1);
      this.hoverPt = null;        // indices shifted (see removeLine)
      this.hoverLineIdx = -1;
      this.listHoverLineIdx = -1;
      if (this.selectedLineIdx === idx) this.deselectLine(false);
      else if (this.selectedLineIdx > idx) this.selectedLineIdx--;
      this.saveHistory();
      this.renderer.redraw();
      this.updateButtons();
      this.coordTable.update();
    }
  }

  // Hit-testing lives in hitTest.js (pure functions); these delegators supply the model and
  // the zoom-aware default thresholds (screen-px radius divided by the zoom, so hits stay
  // constant on screen).
  findLineAt(x, y, threshold = 8 / (this.scale || 1)) {
    return hitTest.findLineAt(this.lines, x, y, threshold);
  }

  showSelectionPanel(line) { selectionPanel.showSelectionPanel(this, line); }

  applyFill() { selectionPanel.applyFill(this); }

  syncFsSelectionPanel(line) { selectionPanel.syncFsSelectionPanel(this, line); }

  // A click on the empty canvas area OUTSIDE the image (the letterbox inside
  // #canvas-viewport). Same intent as clicking blank space inside the image — drop the
  // selection — but it can't go through canvasClick(), which is bound to the <canvas>
  // and needs image coordinates. Guards mirror canvasClick's: no-op while drawing, in a
  // read-only compare view, right after a drag, or with a modifier held (those are
  // multi-select / pan / zoom-rect gestures, not a plain click).
  deselectEmptyArea(e) {
    if (!this.image) return;
    if (this.isDrawing) return;
    if (this.compareReadOnly()) return;
    if (this.dragJustEnded) return;
    if (e && (e.altKey || e.shiftKey || e.ctrlKey || e.metaKey)) return;
    if (this.selectedLineIdx === -1 && !this.selectedLines.length && this.focusedPtIdx === -1) return;
    this.deselectLine();
  }

  deselectLine(redraw = true) {
    this.selectedLineIdx = -1;
    this.selectedLines = [];
    this.updateMultiSelectStatus();
    this.coordLineIdx = -1;
    this.hoveredPtIdx = -1;
    this.focusedPtIdx = -1;
    this.hideSelectionPanels();
    const trigger = document.getElementById('fs-top-trigger');
    if (trigger) trigger.style.height = '8px';
    if (redraw) this.renderer.redraw();
  }

  applySelectionChange(prop, value) {
    if (this.compareReadOnly()) return; // read-only compare view
    if (this.selectedLineIdx === -1) return;
    const line = this.lines[this.selectedLineIdx];
    // Recolouring the stroke must never recolour the points: a line still on the inherit
    // fallback ('' pointColor — pre-pointColor layouts) pins its rendered colour first.
    if (prop === 'color' && !line.pointColor) line.pointColor = line.color;
    line[prop] = value;
    this.saveHistory();
    this.renderer.redraw();
  }

  findNearestPoint(x, y, threshold = 10 / (this.scale || 1)) {
    return hitTest.findNearestPoint(this.lines, this.currentLine, x, y, threshold);
  }

  // The Alt-drag gesture engine (point/segment/whole-line) lives in dragGestures.js; these
  // delegators keep the shared entry points the mouse + touch controllers call.
  beginSegmentDrag(nearSeg, x, y) { dragGestures.beginSegmentDrag(this, nearSeg, x, y); }

  // Alt+Ctrl/⌘+drag: pull a NEW point out of the line under the cursor and drag it. On a
  // closed area the same gesture breaks it open at that spot (dragGestures.pullOutPoint),
  // so the seam appears where the user pulled. Returns whether a drag actually began.
  beginPullOutDrag(x, y) {
    if (this.compareReadOnly()) return false;
    const nearPt = this.findNearestPointWithIdx(x, y);
    const target = (nearPt && nearPt.lineIdx !== -1)
      ? { kind: 'point', ptIdx: nearPt.ptIdx, lineIdx: nearPt.lineIdx }
      : this.findNearestSegmentWithIdx(x, y);
    if (!target || target.lineIdx === undefined || target.lineIdx < 0) return false;
    const line = this.lines[target.lineIdx];
    if (!line) return false;
    const wasArea = !!line.locked;
    const idx = dragGestures.pullOutPoint(line, target.kind ? target : { ...target, kind: 'segment' }, x, y);
    if (idx < 0) return false;
    this.strokeFx.flyIn(line, idx, { x, y });
    this.selectedLineIdx = target.lineIdx;
    this.coordLineIdx = target.lineIdx;
    this.focusedPtIdx = idx;
    this.isDraggingPoint = true;
    this.draggingPoint = { lineIdx: target.lineIdx, ptIdx: idx };
    this.showSelectionPanel(line);
    this.coordTable.update(line.points, target.lineIdx);
    this.renderer.redraw();
    this.updateButtons();
    if (wasArea) notify('Area unchained — drag the loose end', 'ok');
    return true;
  }

  // Turn the selected area back into an open line (the ✂ in the selection panel).
  unchainSelectedLine() {
    if (this.compareReadOnly()) return;
    const line = this.lines[this.selectedLineIdx];
    if (!dragGestures.unchainLine(line)) { notify('Selected line is not an area', 'info'); return; }
    this.focusedPtIdx = -1;
    this.showSelectionPanel(line);
    this.coordTable.update(line.points, this.selectedLineIdx);
    this.saveHistory();
    this.renderer.redraw();
    this.updateButtons();
    notify('Area unchained — it is an open line again', 'ok');
  }

  movePointTo(dp, x, y) { dragGestures.movePointTo(this, dp, x, y); }

  endPointDrag(dp, altKey) { dragGestures.endPointDrag(this, dp, altKey); }

  endSegmentDrag(altKey) { dragGestures.endSegmentDrag(this, altKey); }

  dragMove(clientX, clientY, shiftKey) { dragGestures.dragMove(this, clientX, clientY, shiftKey); }

  // Does a click at (x,y) close the in-progress shape? (>= 3 points and within
  // pointSize + 8 image px of the first point.) Shared C++ core (wasm) when
  // loaded; the JS below is the reference + fallback.
  #shouldCloseShape(points, x, y, pointSize) {
    const fn = core.op('shouldCloseShape');
    if (fn) return fn(points, { x, y }, pointSize);
    if (points.length < 3) return false;
    const p0 = points[0];
    return Math.hypot(p0.x - x, p0.y - y) <= pointSize + 8;
  }

  getPageDimensions() {
    // Shared C++ core (wasm) owns the named-size table + landscape swap when
    // loaded; the JS below is the reference + fallback (PAGE_SIZES mirrors it).
    const fn = core.op('pageDimensions');
    if (fn) {
      return fn(this.pageSize, this.canvas.width, this.canvas.height,
        this.customPageWidth, this.customPageHeight);
    }
    if (this.pageSize === 'custom') return { width: this.customPageWidth, height: this.customPageHeight };
    const ps = { ...PAGE_SIZES[this.pageSize] };
    // Swap to landscape if image is wider than tall
    if (this.canvas.width > this.canvas.height) return { width: ps.height, height: ps.width };
    return ps;
  }

  pixelToPageCoords(x, y) {
    const ps = this.getPageDimensions();
    // Raw pixel→cm via the shared core when loaded; formula.apply itself already
    // routes through the wasm parser (see FormulaEngine).
    const pixelToPageRaw = core.op('pixelToPageRaw');
    const raw = pixelToPageRaw
      ? pixelToPageRaw(x, y, ps, this.canvas.width, this.canvas.height)
      : { x: (ps.width / this.canvas.width) * x, y: (ps.height / this.canvas.height) * y };
    return {
      x: this.formula.apply(this.formulaX, 'x', raw.x, this.allowFormulas),
      y: this.formula.apply(this.formulaY, 'y', raw.y, this.allowFormulas)
    };
  }

  // Live cursor-coordinate readout in the status bar below the canvas. Mirrors the desktop
  // status bar (mainWindow.cpp onHovered): ALWAYS shows Pixel + Page (cm) regardless of the
  // tooltip's per-row toggles; appends To edge (cm). No args / no image → idle hint.
  updateCoordStatus(x, y) {
    const el = this.coordStatus ??= document.getElementById('coord-status');
    if (!el) return;
    if (!this.image || x === undefined) {
      // Empty either way: this bar reads out the cursor, and off-canvas / imageless there is
      // nothing to read (desktop parity — support/mainWindow.cpp updateStatusIdle).
      el.textContent = '';
      return;
    }
    const page = this.pixelToPageCoords(x, y);
    const ps = this.getPageDimensions();
    const lbl = unitLabel(this.unit);
    const fx = v => cmToUnit(v, this.unit).toFixed(2);
    el.textContent =
      `Pixel (${Math.round(x)}, ${Math.round(y)})` +
      `   ·   Page (${fx(page.x)}, ${fx(page.y)}) ${lbl}` +
      `   ·   To edge (${fx(ps.width - page.x)}, ${fx(ps.height - page.y)}) ${lbl}`;
  }

  // Reflect the active display unit across the UI: unit dropdown, custom page-size inputs
  // (stored cm → shown in active unit), their unit label, and the coord-table's two
  // page-column headers. Model values are never mutated — only their presentation.
  applyUnitToUI() {
    const lbl = unitLabel(this.unit);
    const sel = document.getElementById('unit-select');
    if (sel) sel.value = this.unit;
    // Named page-size option labels ("A4 (21 × 29.7 cm)") re-render in the active unit.
    // Re-asserting the model value routes through enhanceSelect's wrapped setter, which
    // refreshes the visible dropdown trigger to the relabelled option (and, at boot,
    // moves the select off its markup default — Custom… is the FIRST option — onto the
    // app's default page).
    const psSel = document.getElementById('page-size');
    if (psSel) {
      for (const opt of psSel.options)
        if (opt.value !== 'custom') opt.textContent = pageFormatLabel(opt.value, this.unit);
      psSel.value = this.pageSize;
    }
    const w = document.getElementById('custom-page-width');
    const h = document.getElementById('custom-page-height');
    if (w) w.value = +cmToUnit(this.customPageWidth, this.unit).toFixed(2);
    if (h) h.value = +cmToUnit(this.customPageHeight, this.unit).toFixed(2);
    const ths = document.querySelectorAll('#coordinates-table thead th');
    if (ths[3]) ths[3].textContent = `X ${lbl}`;
    if (ths[4]) ths[4].textContent = `Y ${lbl}`;
  }

  finishDragGesture(altKey) { dragGestures.finishDragGesture(this, altKey); }

  findNearestPointWithIdx(x, y, threshold = 12 / (this.scale || 1)) {
    return hitTest.findNearestPointWithIdx(this.lines, this.currentLine, x, y, threshold);
  }

  findNearestSegmentWithIdx(x, y, threshold = 12 / (this.scale || 1)) {
    return hitTest.findNearestSegmentWithIdx(this.lines, x, y, threshold);
  }

  // Alt+wheel: bump the thickness of the line under the cursor by ±1 (clamped 1–20).
  adjustThicknessAtCursor(e) {
    const { x, y } = this.canvasCoords(e.clientX, e.clientY);
    const delta = e.deltaY > 0 ? -1 : 1;
    const nearPt = this.findNearestPointWithIdx(x, y);
    let lineIdx = -1;
    if (nearPt && nearPt.lineIdx !== -1) lineIdx = nearPt.lineIdx;
    else { const li = this.findLineAt(x, y); if (li !== -1) lineIdx = li; }
    if (lineIdx === -1) return false;
    const line = this.lines[lineIdx];
    const newT = Math.max(1, Math.min(20, (line.thickness || 1) + delta));
    if (newT === line.thickness) return true;
    line.thickness = newT;
    if (lineIdx === this.selectedLineIdx) {
      setVal('sel-thickness', newT);
      setVal('fs-sel-thickness', newT);
    }
    this.renderer.redraw();
    notify('Line thickness: ' + newT, 'info');
    clearTimeout(this.#thicknessSaveTimer);
    this.#thicknessSaveTimer = setTimeout(() => { this.saveHistory(); this.storage.save(); }, 280);
    return true;
  }

  // Bounding-box centre of a point list, via the shared C++ core (wasm) with a JS fallback.
  #bboxCenterOf(pts) {
    const bboxCenter = core.op('boundingBoxCenter');
    if (bboxCenter) return bboxCenter(pts);
    let minX = Infinity; let minY = Infinity; let maxX = -Infinity; let maxY = -Infinity;
    for (const p of pts) {
      if (p.x < minX) minX = p.x;
      if (p.x > maxX) maxX = p.x;
      if (p.y < minY) minY = p.y;
      if (p.y > maxY) maxY = p.y;
    }
    return { x: (minX + maxX) / 2, y: (minY + maxY) / 2 };
  }

  // Rotate every point in `pts` about (cx, cy) by `angle`, via the shared C++ core with a JS fallback.
  #rotatePointsAbout(pts, cx, cy, angle) {
    const rotate = core.op('rotatePoints');
    if (rotate) { rotate(pts, cx, cy, angle); return; }
    const cos = Math.cos(angle);
    const sin = Math.sin(angle);
    pts.forEach((p) => {
      const dx = p.x - cx;
      const dy = p.y - cy;
      p.x = cx + dx * cos - dy * sin;
      p.y = cy + dx * sin + dy * cos;
    });
  }

  // Mirror every point in `pts` about (cx, cy) — horizontal flips left↔right (x' = 2cx - x),
  // vertical flips top↔bottom (y' = 2cy - y) — via the shared C++ core with a JS fallback.
  #flipPointsAbout(pts, horizontal, cx, cy) {
    const flip = core.op('flipPoints');
    if (flip) { flip(pts, horizontal, cx, cy); return; }
    pts.forEach((p) => {
      if (horizontal) p.x = 2 * cx - p.x;
      else p.y = 2 * cy - p.y;
    });
  }

  // Debounced history+storage save shared by the in-place geometry transforms (rotate/flip):
  // a burst of key-repeats or wheel steps collapses into a single undo step / persist.
  #scheduleTransformSave() {
    clearTimeout(this.#rotateSaveTimer);
    this.#rotateSaveTimer = setTimeout(() => { this.saveHistory(); this.storage.save(); }, 280);
  }

  // Apply an in-place per-line transform `op(points, cx, cy)` to the current selection about the
  // selection's bounding-box centre (≥2 selected → combined centre; exactly 1 → that line's
  // centre). Redraws, refreshes the coord table for a focused single line, and schedules the
  // debounced save. Shared by flipSelectedLine and the centre-pivot rotate path; no-op when the
  // selection is empty or a single line has fewer than 2 points.
  #transformSelection(op) {
    const sel = this.selectedIndices();
    if (sel.length >= 2) {
      const lines = sel.map((i) => this.lines[i]).filter((l) => l && l.points.length);
      if (!lines.length) return;
      const { x: cx, y: cy } = this.#bboxCenterOf(lines.flatMap((l) => l.points));
      for (const l of lines) op(l.points, cx, cy);
    } else {
      const line = this.lines[this.selectedLineIdx];
      if (!line || line.points.length < 2) return;
      const { x: cx, y: cy } = this.#bboxCenterOf(line.points);
      op(line.points, cx, cy);
      if (this.coordLineIdx === this.selectedLineIdx) this.coordTable.update(line.points, this.selectedLineIdx);
    }
    this.renderer.redraw();
    this.#scheduleTransformSave();
  }

  rotateSelectedLine(angle) {
    // 2+ selected → rotate the whole set about their combined centre (the shared bbox path).
    const sel = this.selectedIndices();
    if (sel.length >= 2) {
      this.#transformSelection((pts, cx, cy) => this.#rotatePointsAbout(pts, cx, cy, angle));
      return;
    }
    const line = this.lines[this.selectedLineIdx];
    if (!line || line.points.length < 2) return;
    // One selected: pivot on the focused point when there is one, else the line's bbox centre —
    // a rotate-only special case, so it doesn't go through #transformSelection's centre pivot.
    let cx;
    let cy;
    if (this.coordLineIdx === this.selectedLineIdx && this.focusedPtIdx >= 0
        && line.points[this.focusedPtIdx]) {
      cx = line.points[this.focusedPtIdx].x;
      cy = line.points[this.focusedPtIdx].y;
    } else {
      ({ x: cx, y: cy } = this.#bboxCenterOf(line.points));
    }
    this.#rotatePointsAbout(line.points, cx, cy, angle);
    this.renderer.redraw();
    if (this.coordLineIdx === this.selectedLineIdx) this.coordTable.update(line.points, this.selectedLineIdx);
    this.#scheduleTransformSave();
  }

  // Flip the selected line(s) about the selection's bounding-box centre — the same pivot the
  // arbitrary-angle rotate uses. `horizontal` mirrors left↔right, else top↔bottom. Debounced
  // history save like rotateSelectedLine; no selection is a no-op.
  flipSelectedLine(horizontal) {
    this.#transformSelection((pts, cx, cy) => this.#flipPointsAbout(pts, horizontal, cx, cy));
  }

  // Rotate the selected line(s) a quarter turn (dir > 0 → +90° CW, dir < 0 → -90°) — reuses the
  // arbitrary-angle rotate path so the pivot and debounced save are identical.
  rotateSelectedLineQuarter(dir) {
    this.rotateSelectedLine(dir * (Math.PI / 2));
  }

  // Translate every selected line by (dx, dy) image-space px — the arrow-key nudge. Mirrors the
  // drag-move translation (dragMove) but keyboard-driven; the history save is debounced (like
  // rotateSelectedLine) so a burst of key-repeats collapses into one undo step.
  nudgeSelected(dx, dy) {
    if (!dx && !dy) return this;
    const sel = this.selectedIndices();
    if (!sel.length) return this;
    for (const li of sel) {
      const line = this.lines[li];
      if (line) line.points.forEach(p => { p.x += dx; p.y += dy; });
    }
    this.renderer.redraw();
    if (sel.includes(this.coordLineIdx) && this.lines[this.coordLineIdx])
      this.coordTable.update(this.lines[this.coordLineIdx].points, this.coordLineIdx);
    this.#scheduleTransformSave();
    return this;
  }

  saveHistory() {
    this.history.push(this.lines);
    this.storage.save();
    this.remoteSync.scheduleRemoteSync();
  }

  // Live co-edit push/pull + server writes live in RemoteSyncController (remoteSyncController.js).
  // Callers reach it directly via app.remoteSync.<method>() — saveHistory, the setters, ImageModel,
  // the stencilApi facade, and the connection event feed (scheduleRemoteSync / onServerProjectEvent /
  // reloadRemoteActive / saveToServer). The adoptServer*/fetchRemoteOriginal/renderResultBytes
  // helpers are also on the controller (loadImageFromFile + the project-transfer helpers drive them).

  undo() {
    if (this.compareReadOnly()) return; // read-only compare view
    if (this.isDrawing && this.currentLine) {
      if (this.currentLine.points.length > 0) {
        this.undonePoints.push(this.currentLine.points.pop());
        this.renderer.redraw();
        this.updateButtons();
      }
      return;
    }
    const result = this.history.undo();
    if (result !== null) {
      this.strokeFx.cancel();     // the snapshot's points are not the ones in the air
      this.lines = result;
      this.hoverPt = null;        // the restored snapshot may not contain the hovered indices
      this.hoverLineIdx = -1;
      this.listHoverLineIdx = -1;
      this.renderer.redraw();
      this.updateButtons();
      this.coordTable.update();
    }
  }

  redo() {
    if (this.compareReadOnly()) return; // read-only compare view
    if (this.isDrawing && this.currentLine) {
      if (this.undonePoints && this.undonePoints.length > 0) {
        this.currentLine.points.push(this.undonePoints.pop());
        this.renderer.redraw();
        this.updateButtons();
      }
      return;
    }
    const result = this.history.redo();
    if (result !== null) {
      this.strokeFx.cancel();     // see undo()
      this.lines = result;
      this.hoverPt = null;        // see undo(): indices may be stale against the snapshot
      this.hoverLineIdx = -1;
      this.listHoverLineIdx = -1;
      this.renderer.redraw();
      this.updateButtons();
      if (this.lines.length > 0) this.coordTable.update(this.lines[this.lines.length - 1].points);
    }
  }

  // The app-wide control gating sweep lives in ui/controlState.js; this delegator stays the
  // single entry point every state change calls.
  updateButtons() { updateControlState(this); }

  // Reflect the active project's name in the tab title AND topbar field. Field editable only
  // with a saved active project; shows the image-derived name for a fresh one (see projectsStore
  // meta init). `force` re-syncs even while focused (commit/cancel); default respects focus so
  // updateButtons() can't clobber a name being typed.
  updateProjectTitle(force = false) {
    let name = '';
    let editable = false;
    if (this.storage.incognito) {
      name = 'Incognito';
    } else if (this.activeProjectId != null) {
      name = this.storage.store.getMeta(this.activeProjectId)?.name || this.imageBaseName || 'Untitled';
      editable = true;
    } else if (this.image) {
      name = this.imageBaseName || 'Untitled';
    }
    document.title = name ? `${name} — Stencil` : 'Stencil';
    const input = document.getElementById('project-name-input');
    const editBtn = document.getElementById('project-name-edit');
    if (input && (force || document.activeElement !== input)) {
      input.value = name;
      // Shrink-wrap the field to the name so what follows it (the rename controls and the
      // "?" bubble) sits beside the text, not at the end of a fixed slot. Editing keeps a
      // roomier box so a longer name can be typed without the field jumping per keystroke.
      input.size = Math.max(8, Math.min(30, (this.nameEditing ? 24 : name.length) || 10));
      // `editable` (a saved, non-incognito project) only gates the rename affordance;
      // the field itself stays a read-only title until the user enters edit mode.
      input.disabled = !editable;
      if (!this.nameEditing) input.readOnly = true;
      input.placeholder = editable ? 'Untitled' : (this.storage.incognito ? 'Incognito (unsaved)' : 'No project');
      if (editBtn && !this.nameEditing) editBtn.style.display = editable ? '' : 'none';
      this.nameEditor?.refresh();                      // set ✓ enabled/disabled state
    }
    // Paint the name field in the project's custom colour; unset falls back to the CSS
    // neutral grey. Show the colour swatch only for a saved (non-incognito) project.
    if (input) {
      const projColor = (editable && this.activeProjectId != null)
        ? (this.storage.store.getMeta(this.activeProjectId)?.color || '')
        : '';
      // The legibility shadow is left ENTIRELY to CSS (--project-name-shadow) so it re-flips
      // live on theme toggle — setting it inline would freeze it to the paint-time theme.
      input.style.color = projColor || '';
      input.style.textShadow = '';
      const colorBtn = document.getElementById('project-color-btn');
      if (colorBtn && !this.nameEditing) {
        colorBtn.style.display = editable ? '' : 'none';
        // The chip is accent-FILLED, so its glyph is white like the pencil's beside it when
        // the project has no colour of its own — muted grey on the accent read as disabled
        // (user report, with a picture). A project WITH a colour still wears it: that is
        // what the control says.
        colorBtn.style.color = projColor || '#fff';
      }
    }
    // Outside edit mode (no project, incognito, post-commit, click-away) the ✓/✗
    // rename controls must never linger — they belong to edit mode only.
    if (!this.nameEditing) {
      const a = document.getElementById('project-name-accept');
      const c = document.getElementById('project-name-cancel');
      if (a) a.style.display = 'none';
      if (c) c.style.display = 'none';
    }
    // Server-editing indicator: a golden badge by the name + a golden outline on the
    // canvas, so it's obvious this session is editing a project stored on a server.
    const remote = this.remoteLink;
    const badge = document.getElementById('project-remote-badge');
    if (badge) {
      badge.style.display = remote ? 'inline-flex' : 'none';
      if (remote) badge.dataset.title = `Editing a project stored on ${remote.address}`;
    }
    const canvasViewport = document.getElementById('canvas-viewport');
    if (canvasViewport) canvasViewport.classList.toggle('remote-editing', !!remote);
  }

  updateInfo() {
    const info = document.getElementById('image-info');
    const blankBtn = document.getElementById('blank-color-btn');
    const blankSwatch = document.getElementById('blank-color-swatch');
    // A blank project writes "· blank" into the size readout + reveals the recolour swatch.
    const isBlank = this.activeIsBlank();
    if (this.image) {
      const blankTag = isBlank ? '  ·  blank' : '';
      // Just the image size — the shortcut hints live in the "?" popup (hints-btn), not this bar.
      info.textContent = `Image Size: ${this.canvas.width} × ${this.canvas.height} px${blankTag}`;
    } else {
      info.textContent = 'No image loaded. Upload an image to start.';
    }
    // Announce incognito here, beside the image facts — the "?" bubble is about the IMAGE,
    // so an empty incognito editor had nowhere else to say it. data-size keeps that bubble
    // reading the size line alone.
    info.dataset.size = info.textContent;
    if (this.storage.incognito) {
      // A muted divider, and it exists only when the tag it separates does — so the
      // line reads "Image Size: … px | (glyph) Incognito — not saved" in incognito and has
      // no dangling bar otherwise. Decoration, so it is hidden from assistive tech.
      const sep = document.createElement('span');
      sep.className = 'info-divider';
      sep.setAttribute('aria-hidden', 'true');
      sep.textContent = '|';
      const tag = document.createElement('span');
      tag.className = 'info-incognito';
      // The app's OWN incognito glyph — the one the toolbar toggle wears — not an
      // emoji: it renders identically on every platform and, drawn with
      // stroke="currentColor", takes the tag's accent colour for free.
      tag.innerHTML = `${icon('incognito', { size: 13 })}<span>Incognito — not saved</span>`;
      info.append(sep, tag);
    }
    // Blank-fill recolour swatch: sits beside the size pill, shown only for a blank project.
    if (blankBtn) blankBtn.style.display = (this.image && isBlank) ? 'inline-flex' : 'none';
    if (blankSwatch && isBlank) blankSwatch.style.background = this.blankColor || '#ffffff';
  }

  // Persistence chatter (saved / not saved / restored / cleared), shown in the toast stack.
  // `color` stays in the signature — every call site picks a status token, and it selects
  // the toast kind.
  showSaveStatus(msg, color, _iconName = null) {
    const kind = String(color).includes('danger') ? 'fail'
               : String(color).includes('success') ? 'ok' : 'info';
    // One running status, not a stream: `key` makes the newer message replace the older
    // toast (a single blank-image create saves twice).
    notify(msg, kind, { key: 'save-status' });
  }

  restoreFromLocalStorage() {
    this.storage.restore();
  }

  // ── Multi-project navigation + lifecycle ──────
  // The implementations live in ProjectTransferController (projectTransferController.js);
  // these delegators keep every call site (stencilApi, controlsBinder, projects modal glue)
  // untouched.
  switchToProject(id) { return this.projectTransfer.switchToProject(id); }

  openProjectInNewTab(id, win = null) { this.projectTransfer.openProjectInNewTab(id, win); }

  openRemoteProjectInNewTab(meta, win = null) { this.projectTransfer.openRemoteProjectInNewTab(meta, win); }

  // Consume a "?open=<id>" deep link captured at boot: strip it from the URL (so
  // a reload doesn't re-trigger) and switch to the project if it still exists.
  // Called once from the entrypoint after every component is wired.
  applyProjectDeepLink() {
    const id = this.pendingOpenProjectId;
    if (id == null) return false;
    // Drop the query param but keep the path + any fragment.
    history.replaceState(null, '', location.pathname + location.hash);
    return this.switchToProject(id);
  }

  // Promise-based confirmation, replacing native confirm(). Delegates to the
  // <stencil-confirm-modal> component; falls back to native confirm only if the
  // modal isn't present (e.g. before wiring, or in non-DOM test contexts).
  // opts: { title, confirmLabel, cancelLabel, danger }.
  confirm(message, opts = {}) {
    const el = document.getElementById('confirm-modal-overlay');
    if (el && typeof el.ask === 'function') return el.ask(message, opts);
    return Promise.resolve(typeof window !== 'undefined' && window.confirm ? window.confirm(message) : true);
  }

  // True when leaving the tab would interrupt an active editing session — an image is
  // loaded, or the user has drawn something. Drives the beforeunload leave-guard wired
  // in index.js (mirrors the desktop app's quit-confirmation). Kept synchronous:
  // beforeunload can't await the async confirm() modal, so the browser's own native
  // "Leave site?" prompt is used instead.
  hasEditingSession() {
    return !!this.image || (typeof this.history?.canUndo === 'function' && this.history.canUndo());
  }

  // Promise-based single-choice picker (shares the confirm modal). Resolves the
  // chosen option value, or null on cancel. opts.options: [{ value, label }].
  // Used to pick a target server when moving a project to a server.
  choose(message, opts = {}) {
    const el = document.getElementById('confirm-modal-overlay');
    if (el && typeof el.choose === 'function') return el.choose(message, opts);
    // No modal (tests / pre-wire): fall back to the first option, if any.
    const first = (opts.options || [])[0];
    return Promise.resolve(first ? first.value : null);
  }

  // Promise-based three-way ask (shares the confirm modal): Cancel | alt | confirm.
  // Resolves 'confirm', 'alt', or null. opts: { title, confirmLabel, altLabel }.
  // Used for "combine or replace?" when a layout lands on top of existing lines.
  askAlt(message, opts = {}) {
    const el = document.getElementById('confirm-modal-overlay');
    if (el && typeof el.askAlt === 'function') return el.askAlt(message, opts);
    return Promise.resolve(null);   // no modal (tests / pre-wire) → treat as cancelled
  }

  // Promise-based text prompt (shares the confirm modal). Resolves the trimmed string, or
  // null on cancel. opts: { title, confirmLabel, defaultValue }. Used for copy-with-name.
  prompt(message, opts = {}) {
    const el = document.getElementById('confirm-modal-overlay');
    if (el && typeof el.prompt === 'function') return el.prompt(message, opts);
    return Promise.resolve(opts.defaultValue ?? null);
  }

  // Start a fresh blank (unsaved) editor. `keepChat` forwards to newTemporary (the
  // assistant's in-place incognito adoption resets the editor without losing its turn).
  newEditor({ keepChat = false } = {}) {
    this.remoteLink = null;
    this.pendingRemoteAddress = null;   // drop any un-consumed newEditor({ address }) arming
    this.blankColor = '';               // fresh editor is not a blank project until one is created
    this.fromFile = false;              // …nor a file-origin project until a .stencil is opened
    this.stencilSync?.unlink();         // drop any .stencil live-sync link so a new/empty project
                                        // can't auto-save over the previous project's linked file
    this.storage.newTemporary({ keepChat });
    // Dropping the image can move the viewport's top (the toolbar reflows) — re-measure, or
    // the empty editor keeps the height it had with a picture in it.
    this.zoomPan.syncViewportHeight();
    this.tabs.reportActive(null);
    this.#reportIncognitoSession();   // newTemporary clears incognito → drop our peer entry
  }

  // Turn THIS editor into a fresh incognito session, the way openImageHere's incognito
  // branch does (and desktop's openSourceHere): flush the outgoing project so nothing is
  // lost, reset to a blank editor, then switch incognito on. Used by the assistant's §10
  // `openUrl` with incognito, so the conversation is deliberately kept (`keepChat`);
  // the caller loads the picture straight after.
  adoptIncognitoHere() {
    if (!this.storage.incognito) this.storage.save();
    this.newEditor({ keepChat: true });
    this.storage.incognito = true;
    this.updateIncognitoUI();
  }

  // Open-image dialog action: replace the current editor with `file`. A non-incognito
  // current project already persists (so it stays in the projects list) — flush it,
  // then reset to a fresh editor and load the new file (as incognito if requested).
  // `address` (a connected server URL) creates+links the project on that server. Incognito
  // wins over a server target: incognito content is never created on a server (publish an
  // open incognito session explicitly via publishIncognitoToServer instead).
  // `opts.crop` — an explicit crop rect {x,y,width,height} in original-image pixels chosen in
  // the Open dialog's inline crop editor; applied on load in place of the default page-aspect crop.
  openImageHere(file, incognito = false, address = null, opts = {}) {
    if (!file) return;
    const toServer = !!address && !incognito;
    if (toServer) requireConnection(this.connections, address);   // validate up front
    if (!this.storage.incognito) this.storage.save();
    this.newEditor();
    if (incognito) { this.storage.incognito = true; this.updateIncognitoUI(); }
    this.loadImageFromFile(file, this.#applyOpenOpts(toServer ? { address } : {}, opts));
  }

  // Copy the Open-Image dialog's per-source options onto a launch target (loader opts or a
  // fragment payload): an explicit `crop` wins; else `noCrop` imports the whole frame (not
  // the default page-aspect auto-crop); provenance (source/resource) rides along. Returns
  // the mutated target so callers can inline it.
  #applyOpenOpts(target, opts) {
    if (opts.crop) target.crop = opts.crop;
    else if (opts.noCrop) target.noCrop = true;
    if (opts.source) target.source = opts.source;
    if (opts.resource) target.resource = opts.resource;
    if (opts.landing) target.landing = true;
    if (opts.from) target.from = opts.from;
    return target;
  }

  // Open-image dialog action: launch `file` in a NEW browser tab via the #stencil=
  // fragment hand-off (consumed by applyExternalLaunch, which honors `incognito` and `crop`).
  // `opts.crop` rides the fragment payload so the new tab imports with the same crop.
  openImageNewTab(file, incognito = false, opts = {}) {
    if (!file) return;
    const reader = new FileReader();
    reader.onload = () => {
      const base = location.origin + location.pathname;
      const payload = this.#applyOpenOpts(
        { dataUrl: reader.result, name: file.name, incognito: !!incognito }, opts);
      const url = buildExternalLaunchUrl(base, payload);
      window.open(url, '_blank');
    };
    reader.onerror = () => notify('Could not read the image', 'fail');
    reader.readAsDataURL(file);
  }

  // Open-image dialog action: replace the CURRENT project's image in place (same project id /
  // server link) instead of creating a new project. `rename` adopts the new file's name
  // (default off); `keepAnnotations` keeps the existing lines over the new image (default on);
  // `crop` is an explicit rect {x,y,width,height} in the NEW image's pixels, overriding the
  // default page-aspect auto-crop (the extension import's `crop` — the dialog passes none).
  // Any image change unpins the now-stale extension pin (handled in loadImageFromFile).
  replaceProjectImage(file, { rename = false, keepAnnotations = true, crop = null } = {}) {
    if (!file) return;
    this.loadImageFromFile(file, { replaceInPlace: true, rename, keepAnnotations, ...(crop ? { crop } : {}) });
  }

  // Generate a solid-colour PNG blob of size w×h — the raster backing a blank project. Shared by
  // createBlankImage (new blank) and setBlankColor (recolour an existing blank in place).
  #blankFillBlob(w, h, color) {
    const cnv = document.createElement('canvas');
    cnv.width = w; cnv.height = h;
    const ctx = cnv.getContext('2d');
    ctx.fillStyle = color || '#ffffff';
    ctx.fillRect(0, 0, w, h);
    return new Promise((resolve, reject) => {
      cnv.toBlob(blob => (blob ? resolve(blob) : reject(new Error('Could not create the image'))), 'image/png');
    });
  }

  // Create a solid-color blank image and load it (shared with the console API). width/height
  // in px (clamped 1–8192); omitted → current page size. `address` also creates+links the
  // project on that server, validated up front. Resolves { width, height } once handed off.
  createBlankImage({ color = '#ffffff', width, height, address } = {}) {
    if (this.storage.incognito) address = undefined;   // incognito never creates on a server
    if (address) requireConnection(this.connections, address);
    const dims = (width != null && height != null)
      ? { width, height }
      : defaultBlankSizePx(this.pageSize === 'custom'
        ? { width: this.customPageWidth, height: this.customPageHeight }
        : (PAGE_SIZES[this.pageSize] || PAGE_SIZES.A4));
    const w = Math.max(1, Math.min(8192, Math.round(dims.width)));
    const h = Math.max(1, Math.min(8192, Math.round(dims.height)));
    const fill = normalizeHex(color) || '#ffffff';
    // A blank's colour IS the page: a filter left over from the previous image
    // would repaint the fill (bw of a red page is flat gray), so start clean.
    if (this.imageFilter !== 'none') this.settings.setImageFilter('none');
    // blankColor marks this as a (recolourable) blank project; it's persisted into project meta.
    return this.#blankFillBlob(w, h, fill).then(blob => {
      this.loadImageFromFile(new File([blob], `blank-${w}x${h}.png`, { type: 'image/png' }),
        { address: address || undefined, blankColor: fill });
      return { width: w, height: h };
    });
  }

  // True while the active session is a blank project (recolourable solid background).
  activeIsBlank() { return !!this.blankColor; }

  // Recolour the ACTIVE blank project's solid background to `color`, KEEPING every drawn line
  // (lines are a separate vector overlay). No-op unless this is a blank image. Regenerates the fill
  // at the current dimensions in place, persists blank/blankColor (storage.save via replaceInPlace),
  // updates the registry meta + peer tabs, and pushes to the server for a server-linked project —
  // mirroring setProjectColor. `color` accepts any form normalizeHex understands.
  setBlankColor(color) {
    if (!this.activeIsBlank() || !this.image) return this;
    const next = normalizeHex(color);
    if (!next || next === this.blankColor) return this;
    const w = this.canvas.width, h = this.canvas.height;
    this.#blankFillBlob(w, h, next).then(blob => {
      this.loadImageFromFile(new File([blob], `blank-${w}x${h}.png`, { type: 'image/png' }),
        { replaceInPlace: true, keepAnnotations: true, blankColor: next, keepZoom: true });
      if (this.activeProjectId != null) {
        this.storage.store.setBlankColor(this.activeProjectId, next);
        this.tabs.projectsChanged({ id: this.activeProjectId, action: PROJECT_ACTION.UPDATED });
        this.projectTransfer.pushProjectFieldToServer(this.activeProjectId, { blankColor: next }, 'Could not set blank color on the server');
      }
      this.updateButtons();
    }).catch(() => notify('Could not recolor the blank image', 'fail'));
    return this;
  }

  // ── Server-backed sessions ───────────────────────────────────────
  // Create the just-loaded original on `address` and link the session. Reads the
  // File's raw bytes (the server is codec-free, so dimensions are passed in).
  async #createRemoteForSession(address, file) {
    const conn = requireConnection(this.connections, address);
    const bytes = new Uint8Array(await file.arrayBuffer());
    this.remoteLink = await createRemoteProject(conn, {
      name: this.imageBaseName || 'Untitled',
      source: this.imageSource || '',
      resource: this.imageResource || '',
      bytes,
      ext: this.imageExt || 'png',
      w: this.originalImage ? this.originalImage.width : 0,
      h: this.originalImage ? this.originalImage.height : 0,
    });
    notify(`Saved to ${conn.url}`, 'ok');
    return this.remoteLink;
  }

  // Arm `address` as the create target for the NEXT image load, rather than creating a
  // project now: the server forbids image-less projects and there is no image yet at
  // newEditor time, so the upcoming blank()/open creates it WITH real bytes (via
  // #createRemoteForSession) and links the session. Backs stencil.newEditor({ address }).
  async createRemoteBlank(address) {
    const conn = requireConnection(this.connections, address);   // fail fast if not connected
    this.pendingRemoteAddress = conn.url;
    return { address: conn.url };
  }

  // The session as an "Open in…" hand-off payload (the #stencil= fragment shape): a
  // server reference for a linked session, else the inline image + full layout.
  // `id` hands off a SAVED project instead (the projects list offers this per row),
  // read from its stored record; null when nothing is stored under that id.
  openInLaunchPayload({ incognito = false, id = null } = {}) {
    if (id != null && id !== this.activeProjectId) return this.#storedLaunchPayload(id, incognito);
    return DrawingApp.#launchPayload({
      remote: this.remoteLink
        && { url: this.remoteLink.address, id: this.remoteLink.remoteId, version: this.remoteLink.version },
      dataUrl: this.imageDataUrl,
      name: `${this.imageBaseName || 'image'}.${this.imageExt || 'png'}`,
      layout: this.currentLayoutPayload(),
      source: this.imageSource,
      resource: this.imageResource,
      incognito,
    });
  }

  // The saved-project half of openInLaunchPayload: a server-linked project hands over its
  // reference, a purely local one its stored bytes + layout, exactly as the live session does.
  #storedLaunchPayload(id, incognito) {
    const meta = this.storage.store.getMeta(id);
    const proj = this.storage.store.get(id);
    if (!meta || !proj) return null;
    const layout = proj.payload?.layout || {};
    return DrawingApp.#launchPayload({
      remote: meta.remoteId && meta.address
        && { url: meta.address, id: meta.remoteId, version: meta.remoteVersion },
      dataUrl: proj.payload?.image || null,
      name: `${layout.imageBaseName || meta.name || 'image'}.${meta.imageExt || layout.imageExt || 'png'}`,
      layout,
      source: meta.source || layout.imageSource,
      resource: meta.resource || layout.imageResource,
      incognito,
    });
  }

  // The one #stencil= fragment shape both hand-offs above answer with: a server reference
  // (the receiver re-fetches — no bytes, no token), else the inline image + full layout.
  static #launchPayload({ remote, dataUrl, name, layout, source, resource, incognito }) {
    if (remote) {
      const p = { server: { url: remote.url, id: remote.id, version: remote.version || 0 } };
      if (incognito) p.incognito = true;
      return p;
    }
    const p = { dataUrl, name, layout };
    if (source) p.source = source;
    if (resource) p.resource = resource;
    if (incognito) p.incognito = true;
    return p;
  }

  // Build the full layout payload (lines + filter/crop/rotation/page/formulas) from the
  // current editor state. Shared by the server push (saveToServer / publishIncognitoToServer)
  // and ExportService's download/copy actions, so it's a public method rather than private.
  currentLayoutPayload() {
    return buildLayoutPayload({
      imageWidth: this.canvas.width,
      imageHeight: this.canvas.height,
      lines: this.lines,
      imageFilter: this.imageFilter,
      filterColor: this.filterColor,
      cropRect: this.cropRect,
      rotationQuarters: this.rotationQuarters,
      pageSize: this.pageSize,
      customPageWidth: this.customPageWidth,
      customPageHeight: this.customPageHeight,
      allowFormulas: this.allowFormulas,
      formulaX: this.formulaX,
      formulaY: this.formulaY,
    });
  }

  // ── .stencil project files (portable single-file projects) ──────────────
  // Gather this session into the shape projectFile.buildProjectFile wants (ORIGINAL image +
  // export layout + metadata + opt-in theme); JSON/IO live in ExportService + projectFile.js.
  projectFileState({ includeTheme = true } = {}) {
    const meta = (this.activeProjectId != null && this.storage.store.getMeta(this.activeProjectId)) || {};
    const state = {
      name: meta.name || this.imageBaseName || 'Untitled',
      color: meta.color || '',
      keywords: Array.isArray(meta.keywords) ? meta.keywords : [],
      source: this.imageSource || '',
      resource: this.imageResource || '',
      blank: !!this.blankColor,
      blankColor: this.blankColor || '',
      layout: this.currentLayoutPayload(),
    };
    if (this.imageDataUrl) {
      state.image = {
        dataUrl: this.imageDataUrl,          // the ORIGINAL (crop/rotation live in layout)
        ext: this.imageExt || 'png',
        w: this.originalImage ? this.originalImage.width : 0,
        h: this.originalImage ? this.originalImage.height : 0,
      };
    }
    if (includeTheme) state.theme = { mode: this.theme, accent: this.customAccent || this.accent };
    return state;
  }

  // Apply a parsed .stencil as a NEW local project (flush → reset → load ORIGINAL image + adopt
  // its layout/metadata via the server-reopen path, then the theme only if the file carried one).
  // Returns the project name.
  async applyProjectFile(project) {
    if (!project || !project.image || !project.image.dataUrl) throw new Error('Project file has no image');
    const name = project.name || 'Untitled';
    const blob = await (await fetch(project.image.dataUrl)).blob();
    const ext = project.image.ext || 'png';
    const file = new File([blob], `${name}.${ext}`, { type: blob.type || 'image/png' });
    // Open as a distinct project (mirror openImageHere: flush current, reset, then load).
    if (!this.storage.incognito) this.storage.save();
    this.newEditor();
    this.loadImageFromFile(file, {
      name,
      source: project.source || '',
      resource: project.resource || '',
      layout: project.layout,
      adoptLayout: true,
      color: project.color || '',
      keywords: project.keywords || [],
      blankColor: project.blank ? (project.blankColor || '') : undefined,
      fromFile: true,   // mark provenance so the projects list shows a bronze .stencil outline
    });
    // Theme is a global setting; apply it ONLY when the file opted to carry one, so opening a
    // themeless project never changes the user's current theme.
    const t = project.theme;
    if (t) {
      if (t.mode) this.setTheme(t.mode);
      if (t.accent) { if (isAccent(t.accent)) this.setAccent(t.accent); else this.setCustomAccent(t.accent); }
    }
    return name;
  }

  // Update the CURRENT project's layout in place from a parsed .stencil (live file sync, no new
  // editor) via the server-co-edit adopt path, optionally union-merging lines on a conflict.
  applyProjectFileInPlace(project, opts = {}) {
    if (!project || !this.image) return;
    const layout = project.layout || {};
    const verdict = validateLayout(layout, {
      hasImage: !!this.image, imgW: this.canvas.width, imgH: this.canvas.height,
      hasExistingLines: !!(this.lines && this.lines.length),
    });
    if (!verdict.ok) return;
    this.lines = opts.mergeLines ? mergeLines(verdict.lines, this.lines) : verdict.lines;
    if (Number.isInteger(layout.rotationQuarters)) this.rotationQuarters = layout.rotationQuarters;
    if (layout.cropRect) this.cropRect = this.imageModel.roundRect(layout.cropRect);
    this.imageModel.rebuildCroppedImage();
    this.remoteSync.adoptServerFilter(layout);
    this.remoteSync.adoptServerFormulas(layout);
    this.remoteSync.adoptServerPageFormat(layout);
    this.currentLine = null;
    this.history.reset(this.lines);
    this.zoomPan.fitToWindow();
    this.updateInfo();
    this.coordTable.update(this.lines.length > 0 ? this.lines[this.lines.length - 1].points : null);
    this.renderer.redraw();
    this.updateButtons();
    this.storage.save();
  }

  // A 3-way conflict prompt for live file sync (the file changed AND you have un-synced edits),
  // built from two confirms so it reuses the existing modal: → 'theirs' | 'merge' | 'mine'.
  async chooseFileConflict(name = '.stencil') {
    if (await this.confirm(
      `“${name}” was changed outside the app and conflicts with your unsaved edits. Reload the file’s version (discard yours)?`,
      { title: 'File changed', confirmLabel: 'Take file’s version', confirmIcon: 'download', cancelLabel: 'Keep / merge…' })) {
      return 'theirs';
    }
    return (await this.confirm(
      'Merge instead — combine your lines with the file’s?',
      { title: 'Merge changes', confirmLabel: 'Merge both', confirmIcon: 'layers', cancelLabel: 'Keep mine (overwrite file)' }))
      ? 'merge' : 'mine';
  }

  // Reflect the live-sync button state (label/enabled/active). Called on link/unlink/toggle.
  updateStencilSyncUI() {
    const btn = document.getElementById('live-sync-btn');
    if (!btn) return;
    const s = this.stencilSync;
    const on = s.supported && s.linked && s.liveSync;
    btn.classList.toggle('active', on);
    btn.disabled = !(s.supported && s.linked);
    // The greyed-out tooltip line (controlTooltip's data-disabled-reason), like the delete
    // button's: the markup default is the "not linked" case, unsupported browsers differ.
    btn.dataset.disabledReason = s.supported ? 'Open or save a .stencil file first'
      : 'Live file sync needs a Chromium browser (File System Access API)';
    btn.dataset.title = !s.supported ? 'Live file sync needs a Chromium browser (File System Access API)'
      : !s.linked ? 'Open or save a .stencil file first to enable live sync'
        : on ? `Live sync ON — auto-saving to ${s.name} and watching it for changes`
          : `Live sync OFF — click to auto-save to ${s.name} and watch it for changes`;
    // Delete is only meaningful for a file-linked project (there's a retained handle to remove).
    const del = document.getElementById('delete-project-btn');
    if (del) {
      del.disabled = !s.linked;
      del.dataset.title = s.linked ? `Delete “${s.name}” from disk (the project stays open here)`
        : 'Open or save a .stencil file first';
    }
  }

  // Ask the browser extension (if installed) to UNPIN an image — fired on any in-project image
  // change, since the project no longer holds that image. Posts a same-window message the editor
  // bridge content script relays to the extension; a harmless no-op when no extension is present.
  // `resource` (the page the image was pinned on) is what keys the extension's pin entry.
  #requestUnpin(source, resource, name) {
    if (!source && !resource) return;   // nothing identifiable to unpin
    try {
      window.postMessage({
        source: 'stencil-editor-bridge', type: 'unpin',
        pinSource: source || '', resource: resource || source || '', name: name || '', kind: 'image',
      }, '*');
    } catch { /* postMessage unavailable (non-DOM context) — nothing to do */ }
  }

  // Replace the linked server project's stored `original` with `file`'s bytes, then push the
  // new layout + rendered result. All-or-nothing on the sync toggle (matches edit-in-memory).
  async #replaceServerOriginal(file) {
    if (!this.remoteLink || !getSyncToServer()) return;
    try {
      const conn = requireConnection(this.connections, this.remoteLink.address);
      const bytes = new Uint8Array(await file.arrayBuffer());
      await conn.putFile(this.remoteLink.remoteId, 'original', bytes, {
        ext: this.imageExt || 'png',
        w: this.originalImage ? this.originalImage.width : 0,
        h: this.originalImage ? this.originalImage.height : 0,
      });
      await this.remoteSync.saveToServer();   // push the new layout + rendered result
    } catch (err) { notify(`Could not update the server image — ${err.message}`, 'fail'); }
  }

  // Publish the current incognito session to a server: create the project there, push the
  // annotated layout + rendered result, then LINK the session so it becomes a normal
  // server-backed project (incognito turns off). Still no local record — only the server
  // holds it, and getSyncToServer() drives later auto-sync. Returns the new remote link.
  async publishIncognitoToServer(address) {
    const conn = requireConnection(this.connections, address);
    if (!this.image || !this.imageDataUrl) throw new Error('Open an image first');
    // Decode the original (a data URL) to raw bytes for the codec-free server.
    const blob = await (await fetch(this.imageDataUrl)).blob();
    const bytes = new Uint8Array(await blob.arrayBuffer());
    const ext = (blob.type && blob.type.includes('/')) ? blob.type.split('/')[1] : (this.imageExt || 'png');
    const name = this.imageBaseName || 'Untitled';
    const link = await createRemoteProject(conn, {
      name, source: this.imageSource || '', resource: this.imageResource || '',
      bytes, ext,
      w: this.originalImage ? this.originalImage.width : 0,
      h: this.originalImage ? this.originalImage.height : 0,
    });
    // Push the annotated layout + result so the server holds the full project (explicit
    // publish, independent of the sync toggle).
    this.remoteLink = await saveRemoteProject(conn, link, {
      name,
      layout: this.currentLayoutPayload(),
      bytes: await this.remoteSync.renderResultBytes(), ext: 'png', w: this.canvas.width, h: this.canvas.height,
    });
    // Leave incognito: it's now a normal server-backed session (no local persistence).
    this.storage.incognito = false;
    this.updateButtons();   // incognito UI + title, and the project-meta buttons re-gate
    notify(`Published to ${conn.url}`, 'ok');
    return this.remoteLink;
  }

  // Leave incognito and keep what is on screen as a LOCAL project — the local twin of
  // publishIncognitoToServer, and the desktop's promoteIncognitoToLocal. Incognito's promise
  // is that the app writes nothing BY ITSELF; an explicit "save this" from the user is not the
  // app deciding, so it is honoured rather than refused. Returns the project id, or null when
  // there is nothing on screen to keep.
  promoteIncognitoToLocal() {
    if (!this.image) return null;
    this.storage.incognito = false;
    this.storage.promoteTemporaryToProject();
    this.storage.save();
    this.tabs.reportActive(this.activeProjectId);
    this.updateButtons();   // incognito UI + title, and the project-meta buttons re-gate
    notify('Left incognito — saved as a local project', 'ok');
    return this.activeProjectId;
  }

  clearAllProjects() { this.projectTransfer.clearAllProjects(); }

  // ── Shared editor setters ─────────────────────────────────────
  // Single source of truth for top-menu settings lives in SettingsController (settingsController.js):
  // toolbar handlers AND the console API (window.stencil) both call app.settings.<setter>() directly,
  // staying in sync. The formula UI helpers (syncFormulaUI/showFormulaError/refreshFormulaCoords)
  // are public on the controller because #wireFormulaControls and remoteSync.adoptServerFormulas
  // also drive them.

  // ── Point / line mutation (shared with the coord table + console) ──
  // Set one point's x or y in crop-local pixels. lineIdx === -1 targets the
  // in-progress currentLine (mirrors the coord table's target resolution).
  setPointCoord(lineIdx, ptIdx, axis, valuePx) {
    const line = lineIdx === -1 ? this.currentLine : this.lines[lineIdx];
    if (!line || !line.points[ptIdx] || (axis !== 'x' && axis !== 'y')) return this;
    const v = Number(valuePx);
    if (!Number.isFinite(v)) return this;
    line.points[ptIdx][axis] = v;
    this.saveHistory();
    this.renderer.redraw();
    this.coordTable.update(line.points, lineIdx);
    return this;
  }

  // Remove one point; if that empties a committed line, drop the line too. Keeps the
  // coord-table focus/active-line state consistent. Shared by the coord table UI and
  // the console (Point.remove / Line.remove).
  removePoint(lineIdx, ptIdx) {
    const line = lineIdx === -1 ? this.currentLine : this.lines[lineIdx];
    if (!line || !line.points[ptIdx]) return this;
    line.points.splice(ptIdx, 1);
    // Indices shifted: the cached canvas hover would ring a DIFFERENT point until the
    // next mousemove refreshes it.
    this.hoverPt = null;
    this.hoverLineIdx = -1;
    this.listHoverLineIdx = -1;
    if (line.points.length === 0 && lineIdx !== -1) {
      this.lines.splice(lineIdx, 1);
      if (this.selectedLineIdx === lineIdx) this.deselectLine(false);
      this.coordLineIdx = -1;
      this.focusedPtIdx = -1;
      this.coordTable.update(null);
    } else {
      if (this.focusedPtIdx >= line.points.length) this.focusedPtIdx = line.points.length - 1;
      this.coordTable.update(line.points, lineIdx);
    }
    this.saveHistory();
    this.renderer.redraw();
    this.updateButtons();
    return this;
  }

  removeLine(idx) {
    if (idx < 0 || idx >= this.lines.length) return this;
    this.lines.splice(idx, 1);
    // Line indices shifted: drop every cached hover (canvas ring + list glow/tint).
    this.hoverPt = null;
    this.hoverLineIdx = -1;
    this.listHoverLineIdx = -1;
    // Keep the selection + coord-table target consistent with the now-shifted indices:
    // drop them if they pointed at the removed line, else shift down past it.
    if (this.selectedLineIdx === idx) this.deselectLine(false);
    else if (this.selectedLineIdx > idx) this.selectedLineIdx -= 1;
    if (this.coordLineIdx === idx) { this.coordLineIdx = -1; this.focusedPtIdx = -1; }
    else if (this.coordLineIdx > idx) this.coordLineIdx -= 1;
    this.saveHistory();
    this.renderer.redraw();
    this.updateButtons();
    const target = this.coordLineIdx >= 0 ? this.lines[this.coordLineIdx] : null;
    this.coordTable.update(target ? target.points : null, this.coordLineIdx);
    return this;
  }

  // Remove EVERY selected line at once — the multi-select counterpart of removeLine.
  // Splices from the highest index down so the lower indices stay valid while removing,
  // then clears the selection wholesale (each removed line was, by definition, selected).
  // One history entry for the whole batch, so a single undo brings them all back.
  removeSelectedLines() {
    const sel = this.selectedIndices()
      .filter(i => i >= 0 && i < this.lines.length)
      .sort((a, b) => b - a);
    if (!sel.length) return this;
    for (const idx of sel) {
      this.lines.splice(idx, 1);
      // The coord table can point at a line that is NOT part of the selection; keep its
      // index valid the same way removeLine does.
      if (this.coordLineIdx === idx) { this.coordLineIdx = -1; this.focusedPtIdx = -1; }
      else if (this.coordLineIdx > idx) this.coordLineIdx -= 1;
    }
    // Clear the selection inline rather than via deselectLine(), which resets coordLineIdx
    // unconditionally — that would throw away the shift just computed and blank the coord
    // table even when it targets a surviving, unselected line (removeLine keeps it too).
    this.selectedLineIdx = -1;
    this.selectedLines = [];
    this.hoveredPtIdx = -1;
    this.hoverPt = null;          // indices shifted — stale hover would ring the wrong point
    this.hoverLineIdx = -1;
    this.listHoverLineIdx = -1;
    this.hideSelectionPanels();
    this.updateMultiSelectStatus();
    this.saveHistory();
    this.renderer.redraw();
    this.updateButtons();
    const target = this.coordLineIdx >= 0 ? this.lines[this.coordLineIdx] : null;
    this.coordTable.update(target ? target.points : null, this.coordLineIdx);
    return this;
  }

  renewProject(id) { return this.projectTransfer.renewProject(id); }

  setProjectExpiration(id, opts = {}) { return this.projectTransfer.setProjectExpiration(id, opts); }

  closeProject(id, opts = {}) {
    this.projectTransfer.closeProject(id, opts);
    return this;
  }

  renameProject(id, name) { return this.projectTransfer.renameProject(id, name); }

  setProjectColor(id, color) { return this.projectTransfer.setProjectColor(id, color); }

  setProjectKeywords(id, keywords) { return this.projectTransfer.setProjectKeywords(id, keywords); }
  setProjectDescription(id, description) { return this.projectTransfer.setProjectDescription(id, description); }

  setProjectBlankColor(id, color) { return this.projectTransfer.setProjectBlankColor(id, color); }

  removeProject(id) { this.projectTransfer.removeProject(id); }

  // ── Move / copy a project between local storage and a server ──────
  moveProjectToServer(id, address) { return this.projectTransfer.moveProjectToServer(id, address); }

  copyProjectToServer(id, address, opts = {}) { return this.projectTransfer.copyProjectToServer(id, address, opts); }

  moveProjectToLocal(meta) { return this.projectTransfer.moveProjectToLocal(meta); }

  copyServerProjectToLocal(meta, opts = {}) { return this.projectTransfer.copyServerProjectToLocal(meta, opts); }

  copyServerProjectToIncognito(meta, opts = {}) { return this.projectTransfer.copyServerProjectToIncognito(meta, opts); }

  // ── Cross-tab reactions + incognito mode ─────────────────────────
  // True only while the editor is blank (no project, no image, no lines) — the
  // window when incognito can be toggled, since adding content auto-saves.
  canToggleIncognito() {
    return this.storage.temporary && this.activeProjectId == null &&
      !this.image && this.lines.length === 0;
  }

  // No drag/draw/pan in progress → safe to swap content under the user.
  #isIdle() {
    return !this.isDrawing && !this.isPanning && !this.isDraggingPoint &&
      !this.isDraggingSegment && !this.isDraggingLine &&
      !this.isZoomRectDragging && !this.isRectDrawDragging;
  }

  // Reflect incognito state: toggle availability, button highlight, and the
  // editor outline. Called from updateButtons() so it tracks every state change.
  updateIncognitoUI() {
    const btn = document.getElementById('incognito-toggle');
    if (btn) {
      btn.disabled = !this.canToggleIncognito();
      btn.classList.toggle('active', this.storage.incognito);
    }
    document.body.classList.toggle('incognito-mode', this.storage.incognito);
    this.updateInfo();                // the info line carries the mode tag
    this.#reportIncognitoSession();   // keep other tabs' "incognito tabs" list current
  }

  // Broadcast this tab's incognito session (or null) to peers, for the projects modal's
  // "Incognito tabs" filter. Best-effort — tabs may not have a coordinator.
  #reportIncognitoSession() {
    const session = this.storage.incognito
      ? { name: this.imageBaseName || 'Incognito (unsaved)', updatedAt: Date.now() }
      : null;
    try { this.tabs.reportIncognito(session); } catch { /* no coordinator */ }
  }

  // Another tab changed the project set. If it touched OUR active project, sync.
  #onRemoteProjectsChange(detail) {
    const { id, action } = detail;
    // The registry is re-read fresh from localStorage, but project payloads live in a
    // per-tab IndexedDB mirror (projectsBackend.js) — pull the changed project's payload
    // in now, so the active-sync below (and any later open of a project another tab
    // just created) reads the peer's bytes instead of this tab's stale copy.
    const refreshed = Promise.resolve(id != null ? getProjectsBackend()?.refresh?.(id) : null);
    // Full teardown, not just storage.newTemporary(): the removed project may be
    // server-linked, and a bare storage reset leaves remoteLink (and the golden
    // server cues driven by it) pointing at a project that no longer exists.
    if (action === PROJECT_ACTION.REMOVED && id === this.activeProjectId) {
      this.newEditor();
      this.updateButtons();
      notify('This project was removed in another tab', 'info');
      return;
    }
    if (action === PROJECT_ACTION.CLEARED && this.activeProjectId != null) {
      this.newEditor();
      this.updateButtons();
      notify('All projects were cleared in another tab', 'info');
      return;
    }
    if (action === PROJECT_ACTION.CLOSE && id === this.activeProjectId) {
      this.newEditor();
      this.updateButtons();
      notify('This project was closed from another tab', 'info');
      return;
    }
    if (action === PROJECT_ACTION.UPDATED && id === this.activeProjectId) {
      refreshed.then(() => {
        if (this.#isIdle()) this.storage.syncActiveFromStorage();
        // A colour change lives in the registry meta (not the payload syncActiveFromStorage
        // reloads), so always repaint the name from the freshly-read meta.
        this.updateProjectTitle();
      });
    }
  }

  // Public: the composited "result" canvas (filtered image + drawn lines/points),
  // i.e. what download/copy/share/save-to-server emit. Used for project thumbnails
  // so previews show the EDITED result, not the untouched original. Delegates to
  // ExportService (which owns the shared offscreen-render used by every image action).
  renderResultCanvas() { return this.export.renderExportCanvas(); }

  // Export / clipboard / file-IO actions live in ExportService (exportService.js). Callers reach
  // it directly via app.export.<method>() — the toolbar, contextMenu, and window.stencil facade
  // (saveImage / shareImage / downloadJSON / uploadJSON / copyImageToClipboard /
  // copyLayoutToClipboard / applyPastedLayout). See stencilApi.js / the #wire* handlers.

  // ── Wipe every line on the canvas (confirms first) ──
  async clearAllLines() {
    if (this.compareReadOnly()) return; // read-only compare view
    if ((!this.lines || this.lines.length === 0) && (!this.currentLine || this.currentLine.points.length === 0)) {
      notify('No lines to clear', 'info');
      return;
    }
    if (!(await this.confirm('Wipe ALL lines from the canvas? This cannot be undone except via Undo.', { title: 'Clear all lines', danger: true, confirmIcon: 'eraser' }))) {
      notify('Clear canceled', 'info');
      return;
    }
    // Every row in the lines list scatters before the list is rebuilt empty.
    for (const row of document.querySelectorAll('#lines-list .lines-row')) leaveThenRemove(row);
    this.strokeFx.cancel();
    this.lines = [];
    if (this.currentLine) this.currentLine.points = [];
    this.selectedLineIdx = -1;
    this.coordLineIdx = -1;
    this.focusedPtIdx = -1;
    this.hoveredPtIdx = -1;
    this.hoverPt = null;
    this.hoverLineIdx = -1;
    this.listHoverLineIdx = -1;
    this.hideSelectionPanels();
    this.saveHistory();
    this.coordTable.update();
    this.renderer.redraw();
    this.updateButtons();
    notify('All lines cleared', 'ok');
  }
}
