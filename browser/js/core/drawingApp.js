import { setVal, notify, distToSegment, cmToUnit, unitLabel, defaultUnitFromLocale, composeControlTitle } from '../utils.js';
import constants from '../config/constants.json' with { type: 'json' };
const { PAGE_SIZES } = constants;
import { HistoryStack } from './historyStack.js';
import { FormulaEngine } from './formulaEngine.js';
import { Renderer } from './renderer.js';
import { Storage } from './storage.js';
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
import { InputController } from './inputController.js';
import { PointerController } from './pointerController.js';
import { ControlsBinder } from './controlsBinder.js';
import { core } from './stencilCore.js';
import { hotkeys } from './hotkeys.js';
import { DEFAULT_ACCENT, isAccent, applyAccentFavicon, normalizeHex, accentHex } from './accents.js';
import { buildLayoutPayload, validateLayout, resolveInsertIdx, fillState } from './layout.js';
import { readOpenProjectId, buildOpenProjectUrl, buildExternalLaunchUrl, normalizeLaunchPayload } from './deepLink.js';
import { normalizePageSize, pageFormatLabel } from './units.js';
import { icon } from '../ui/icons.js';
import { enhanceSelect } from '../ui/customSelect.js';
import { requireConnection, createRemoteProject, saveRemoteProject } from '../net/remoteSync.js';
import { getSyncToServer, loadSavedServers } from '../net/connectionStore.js';
import { normalizeUrl } from '../net/connectionManager.js';
import { OPEN_IN_DEFAULTS, loadOpenInConfig } from '../config/openInConfig.js';

// Inline SVG glyphs for the draw-mode toggle. `currentColor` makes them inherit
// the button's text color (theme + label match). line = diagonal segment with
// endpoint dots (polyline); rect = outlined rectangle.
export const DRAW_MODE_ICON = {
  line: '<svg class="draw-mode-icon" viewBox="0 0 16 16" width="13" height="13" aria-hidden="true">' +
    '<line x1="3" y1="13" x2="13" y2="3" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>' +
    '<circle cx="3" cy="13" r="2" fill="currentColor"/><circle cx="13" cy="3" r="2" fill="currentColor"/></svg>',
  rect: '<svg class="draw-mode-icon" viewBox="0 0 16 16" width="13" height="13" aria-hidden="true">' +
    '<rect x="2.5" y="3.5" width="11" height="9" rx="1" fill="none" stroke="currentColor" stroke-width="1.5"/></svg>',
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
  #saveStatusTimer = null;
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
    // original-image pixels. Line/marker points are crop-local. See applyCrop / #buildCroppedImage.
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
    this.thickness = 2;
    this.markerSize = 4;
    this.style = 'solid';
    this.showPoints = true;
    this.showLines = true;
    this.imageFilter = 'none'; // 'none' | 'bw' | 'sepia' | 'custom'
    this.filterColor = '#7c3aed'; // custom tint color
    this.pageSize = 'A3';
    this.customPageWidth = 21;
    this.customPageHeight = 29.7;
    this.selectedLineIdx = -1;
    // Multi-line selection set (Ctrl/⌘+Shift+click to add/toggle). Empty in ordinary
    // single-select mode — selectedIndices() then falls back to [selectedLineIdx], so every
    // existing single-line path is untouched. Populated only in multi-select mode; when it holds
    // 2+ lines, selectedLineIdx is -1 (the single-line editor hides) and move/rotate act on all.
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
    this.mouseOverCanvas = false;
    this.lastMouseClientX = 0;
    this.lastMouseClientY = 0;

    // ── Configurable visuals (persisted) ──
    this.selGlowColor = '#ffc800'; // selection highlight glow (lines + points)
    this.hoverRingColor = '#7c3aed'; // hover ring around points
    this.focusRingColor = '#7c3aed'; // focused/clicked point ring
    this.defaultFillColor = '#3399ff'; // default fill applied to new locked areas

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
    // Set a sensible initial viewport height
    const vp = document.getElementById('canvas-viewport');
    if (vp) vp.style.maxHeight = Math.max(300, window.innerHeight - 220) + 'px';
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
    this.controls.wireCanvasPointer();
    this.controls.wireSmoothZoom();
    this.pointer.wirePanDrag();
    this.input.wireHoldDraw();
    this.input.wireTouch();
    this.#wireExternalResume();
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
  setTheme(theme) { this.accents.setTheme(theme); }
  setAccent(key) { this.accents.setAccent(key); }
  setCustomAccent(hex) { return this.accents.setCustomAccent(hex); }

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
  canvasCoords(clientX, clientY) {
    const rect = this.canvas.getBoundingClientRect();
    const cssX = clientX - rect.left;
    const cssY = clientY - rect.top;
    return { cssX, cssY, x: cssX / this.scale, y: cssY / this.scale };
  }

  // opts.crop — explicit crop rect {x,y,width,height} in original-image pixels, overriding
  // the default centered page-aspect crop (external-launch path). opts.source/opts.resource —
  // provenance URLs for add-by-URL + extension hand-off; omitted for local uploads (clears prior).
  loadImageFromFile(file, opts = {}) {
    const replaceInPlace = !!opts.replaceInPlace;
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
        this.zoomPan.fitToWindow();
        this.updateInfo();
        this.coordTable.update(this.lines.length > 0 ? this.lines[this.lines.length - 1].points : null);
        this.renderer.redraw();
        this.updateButtons();
        this.updateCoordStatus();
        this.storage.save();

        // Adopt a reopened server project's accent colour into the local meta (local-only —
        // the server already holds it), then repaint the name. The field is applied even when
        // empty so a peer CLEARING the colour propagates here too (was gated on truthy, which
        // silently dropped clears); empty restores the neutral-grey fallback.
        if ((opts.remoteId || opts.adoptLayout) && opts.color != null && this.activeProjectId != null) {
          this.storage.store.setColor(this.activeProjectId, normalizeHex(opts.color) || '');
          this.updateProjectTitle();
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

  // ── External launch (browser extension / other front-ends) ───────
  // Extension, desktop app and Telegram bot hand a session off via URL fragment
  // `#stencil=<encodeURIComponent(JSON)>`, shape { dataUrl? | src? | server?, name?, crop?,
  // page?, source?, resource?, open?, incognito?, layout? } (see normalizeLaunchPayload for
  // the schema/precedence). Fragment (not query) keeps the payload off server/logs; consumed
  // once, stripped, routed through the normal upload. `server:{url,id}` opens a server project
  // (connecting like a fresh client — no token rides the link); `layout` restores annotations/
  // filter/crop/page for inline hand-offs. `open:'resume'` switches to an existing same-source
  // project (cross-origin, so the extension can't dedup itself); else import a new project,
  // auto-numbered "name (N)".
  applyExternalLaunch() {
    const hash = location.hash || '';
    const marker = '#stencil=';
    if (!hash.startsWith(marker)) return;
    // Strip the fragment immediately so a reload doesn't re-import the image.
    history.replaceState(null, '', location.pathname + location.search);

    let payload;
    try {
      payload = JSON.parse(decodeURIComponent(hash.slice(marker.length)));
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
    const crop = launch.crop;
    const source = launch.source;
    const resource = launch.resource;

    // Resume: if we hold project(s) for this source, switch instead of re-importing.
    // Several matches → open the projects list to pick. No match (stale ledger / expired
    // project) falls through to a fresh import.
    if (launch.open === 'resume' && !this.storage.incognito && (source || name)
        && this.#resumeBySource(source, name)) {
      return;
    }

    // Fresh import. Auto-number the name against existing same-source projects so repeats
    // become "name (1)", "name (2)", … (skipped for incognito, which never persists).
    // `open:'copy'` takes the same path.
    const opts = crop ? { crop } : {};
    if (!crop && launch.noCrop) opts.noCrop = true;   // Open-Image dialog "Crop off" → full frame
    opts.source = source;
    opts.resource = resource;
    if (!this.storage.incognito && source) opts.name = this.storage.store.copyName(this.#stripExt(name), source);
    // An inline layout (desktop/bot hand-off) restores annotations + filter + crop + page.
    if (launch.layout) {
      opts.layout = launch.layout;
      opts.adoptLayout = true;
    }

    // `src` launches carry an http(s) image URL instead of inline bytes (kept short for
    // links sent through chat). The fetch is best-effort: the host must allow CORS.
    const imageUrl = launch.kind === 'src' ? launch.src : launch.dataUrl;
    if (launch.kind === 'src' && !opts.source) opts.source = launch.src;
    fetch(imageUrl, launch.kind === 'src' ? { mode: 'cors' } : undefined)
      .then(r => { if (!r.ok) throw new Error(`HTTP ${r.status}`); return r.blob(); })
      .then(blob => this.loadImageFromFile(new File([blob], name, { type: blob.type || 'image/png' }), opts))
      .catch(() => notify('Stencil: failed to load the shared image', 'fail'));
  }

  // Switch to an existing project matching this image, without importing. Returns true when
  // it switched (so the caller can stop). Shared by the resume launch path above and the
  // extension's "resume in the open editor tab" nudge (stencil:switch-to-source), which lets
  // the extension re-focus this tab instead of spawning a new one.
  #resumeBySource(source, name) {
    const baseName = this.#stripExt(name || '');
    const matches = this.storage.store.findByImage(source, baseName);
    if (matches.length && this.switchToProject(matches[0].id)) {
      if (matches.length > 1) {
        notify(`Resumed "${matches[0].name}" — ${matches.length} projects share this image`, 'ok');
        document.getElementById('projects-btn')?.click();
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
        { title: 'Open shared project', confirmLabel: 'Connect' }))) {
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

  // Fetch a remote project's image + layout and load it into the editor, linking the
  // session for live co-edit. If a local project is already linked to this server
  // project, just switch to it — never create a duplicate local copy or re-download.
  // `meta` needs { serverUrl, id }; name/source enrich the fallback filename.
  async openRemoteProject(meta) {
    const linked = this.storage.store.list().find(m => m.remoteId === meta.id && m.address === meta.serverUrl);
    if (linked) { this.switchToProject(linked.id); return; }
    const conn = requireConnection(this.connections, meta.serverUrl);
    const full = await conn.getProject(meta.id);
    // Prefer the server's stored original bytes; if it holds none, fetch the `source`
    // URL directly (cross-origin, so it needs CORS — which typical image hosts send).
    const src = full.project?.source || meta.source || '';
    const blob = await this.remoteSync.fetchRemoteOriginal(conn, meta.id, src);
    if (!blob) throw new Error('no image bytes on the server');
    const ext = (blob.type && blob.type.split('/')[1]) || 'png';
    const name = full.project?.name || meta.name || 'image';
    const file = new File([blob], `${name}.${ext}`, { type: blob.type || 'image/png' });
    this.loadImageFromFile(file, {
      source: src,
      resource: full.project?.resource || '',
      color: full.project?.color || '',
      address: meta.serverUrl,
      remoteId: meta.id,
      version: full.project?.version || 0,
      layout: full.layout,
    });
  }

  // Base name without its file extension (for project naming / source matching).
  #stripExt(name) {
    const s = String(name || '');
    const dot = s.lastIndexOf('.');
    return dot > 0 ? s.slice(0, dot) : s;
  }

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
    const cg = document.getElementById('custom-size-group');
    if (cg) cg.style.display = size === 'custom' ? 'inline-flex' : 'none';
    this.applyUnitToUI();   // refresh the custom width/height inputs in the active unit
    this.coordTable.update();
  }

  // Crop / quarter-turn rotation / image geometry live in ImageModel (imageModel.js), backed
  // by cropGeometry.js. Callers reach it directly via app.imageModel.<method>() — storage, the
  // crop modal, and the window.stencil facade (defaultCropRect / effectiveOriginalDims /
  // effectiveOriginalDataUrl / rebuildCroppedImage / rotateImage / applyCrop).

  // Hide the selection panel and its fullscreen mirror. Public — used by ImageModel's
  // after-geometry-change refresh as well as the drawing/selection paths here.
  hideSelectionPanels() {
    const selPanel = document.getElementById('selection-panel');
    if (selPanel) selPanel.style.display = 'none';
    const fsPanel = document.getElementById('fs-selection-panel');
    if (fsPanel) fsPanel.style.display = 'none';
  }

  loadJSONFromFile(file) {
    const reader = new FileReader();
    reader.onload = event => {
      try {
        const data = JSON.parse(event.target.result);
        if (!data || !Array.isArray(data.lines)) {
          notify('File is not a valid layout', 'fail');
          return;
        }
        this.export.applyPastedLayout(data);
      } catch (err) {
        notify('Error loading JSON: ' + err.message, 'fail');
      }
    };
    reader.readAsText(file);
  }

  startDrawingMode(opts = {}) {
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
      document.getElementById('start-drawing').classList.add('active');
      document.getElementById('stop-drawing').disabled = false;
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
      thickness: this.thickness,
      markerSize: this.markerSize,
      style: this.style
    };
    if (!opts.keepSelection) {
      this.selectedLineIdx = -1;
      this.hideSelectionPanels();
    }
    this.undonePoints = []; // stack for redo while drawing
    document.getElementById('start-drawing').classList.add('active');
    document.getElementById('stop-drawing').disabled = false;
    this.updateButtons();
    this.renderer.redraw();
  }

  // Switch between polyline ('line') and rectangle ('rect') drawing.
  setDrawMode(mode) {
    this.drawMode = (mode === 'rect') ? 'rect' : 'line';
    this.syncDrawModeUI();
  }

  syncDrawModeUI() {
    const btn = document.getElementById('draw-mode-toggle');
    if (btn) {
      btn.innerHTML = (this.drawMode === 'rect' ? DRAW_MODE_ICON.rect : DRAW_MODE_ICON.line) +
        (this.drawMode === 'rect' ? '<span>Rect</span>' : '<span>Line</span>');
      btn.dataset.title = this.drawMode === 'rect'
        ? 'Drawing mode: Rectangle (click to switch to Line)'
        : 'Drawing mode: Line (click to switch to Rectangle)';
      btn.title = composeControlTitle(btn, hotkeys.isMac, id => hotkeys.get(id));
    }
    const lbl = document.getElementById('ctx-drawmode-label');
    if (lbl) lbl.textContent = this.drawMode === 'rect'
      ? 'Switch to Line Drawing' : 'Switch to Rectangle Drawing';
  }

  stopDrawingMode() {
    // Continuation drawing: the line is already in this.lines — just commit & reset
    if (this.continueLineIdx >= 0) {
      const li = this.continueLineIdx;
      this.continueLineIdx = -1;
      this.continueInsertIdx = -1;
      this.currentLine = null;
      this.isDrawing = false;
      document.getElementById('start-drawing').classList.remove('active');
      document.getElementById('stop-drawing').disabled = true;
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
    document.getElementById('start-drawing').classList.remove('active');
    document.getElementById('stop-drawing').disabled = true;
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

  // Rebuild the "Lines" tab list — one row per committed line (color chip, index, point/segment
  // count, area marker), reflecting the current selection. Rows single-select on click (⌘/Ctrl+Shift
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
      row.dataset.idx = String(i);

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
      rm.title = 'Remove line';
      rm.setAttribute('aria-label', `Remove line ${i + 1}`);
      rm.innerHTML = icon('trash', { size: 13 });
      rm.addEventListener('click', (e) => { e.stopPropagation(); this.removeLine(i); });

      row.addEventListener('click', (e) => {
        this.selectLineFromList(i, (e.ctrlKey || e.metaKey) && e.shiftKey);
      });
      row.append(swatch, label, rm);
      el.appendChild(row);
    });
  }

  canvasClick(e) {
    // No image → the canvas is an empty void; don't let clicks drop points.
    if (!this.image) return;
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

      // Continuation drawing: extend the selected line at the insert point
      if (this.continueLineIdx >= 0 && this.lines[this.continueLineIdx]) {
        const line = this.lines[this.continueLineIdx];
        // Click near the first point closes it into a locked area
        if (this.#shouldCloseShape(line.points, x, y, line.markerSize ?? this.markerSize)) {
          this.#closeContinuedShape();
          return;
        }
        line.points.splice(this.continueInsertIdx, 0, { x, y });
        this.focusedPtIdx = this.continueInsertIdx;
        this.continueInsertIdx++;
        this.coordTable.update(line.points, this.continueLineIdx);
        this.renderer.redraw();
        this.updateButtons();
        return;
      }

      const pts = this.currentLine.points;
      // Click on the first point closes the shape into a locked area
      if (this.#shouldCloseShape(pts, x, y, this.currentLine.markerSize ?? this.markerSize)) {
        this.#closeCurrentShape();
        return;
      }
      this.undonePoints = [];
      this.currentLine.points.push({ x, y });
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
    document.getElementById('start-drawing').classList.remove('active');
    document.getElementById('stop-drawing').disabled = true;
    // Select the new area so its fill control appears
    this.selectedLineIdx = areaIdx;
    this.coordLineIdx = areaIdx;
    this.focusedPtIdx = -1;
    this.showSelectionPanel(this.lines[areaIdx]);
    this.coordTable.update(this.lines[areaIdx].points, areaIdx);
    this.saveHistory();
    this.renderer.redraw();
    this.updateButtons();
    notify('Shape closed — locked area created', 'ok');
  }

  // Insert a new point into an existing line between two connecting points.
  insertPointOnSegment(lineIdx, insertIdx, x, y) {
    const line = this.lines[lineIdx];
    if (!line) return;
    line.points.splice(insertIdx, 0, { x, y });
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
      thickness: this.thickness,
      markerSize: this.markerSize,
      style: this.style
    };
    this.lines.push(newLine);
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
      thickness: this.thickness,
      markerSize: this.markerSize,
      style: this.style,
      locked: true,
      fillColor: 'transparent'
    };
    this.lines.push(rect);
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

    // While panning or dragging point, don't update tooltip or cursor here
    if (this.isPanning || this.isDraggingPoint) return;

    const { x, y } = this.canvasCoords(e.clientX, e.clientY);

    // Persistent cursor-coordinate readout (mirrors the desktop status bar and
    // the Ctrl-held tooltip content), independent of which tooltip is showing.
    this.updateCoordStatus(x, y);

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

    // Alt key held → drag-ready cursors, no tooltip
    if (e.altKey) {
      if (e.shiftKey) {
        // Alt+Shift: whole-line drag mode
        const overLine = this.findLineAt(x, y) !== -1;
        this.canvas.style.cursor = overLine ? 'move' : 'grab';
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
      const overLine = this.findLineAt(x, y) !== -1;
      this.canvas.style.cursor = overLine ? 'pointer' : 'crosshair';
    } else {
      this.canvas.style.cursor = 'crosshair';
    }

    this.tooltipMgr.applyHover(e.clientX, e.clientY, x, y, e);
  }

  canvasDblClick(e) {
    if (this.isDrawing) return;
    if (e.altKey) return; // Alt+dblclick is reserved for zoom reset

    const { x, y } = this.canvasCoords(e.clientX, e.clientY);

    const idx = this.findLineAt(x, y);
    if (idx !== -1) {
      this.lines.splice(idx, 1);
      if (this.selectedLineIdx === idx) this.deselectLine(false);
      else if (this.selectedLineIdx > idx) this.selectedLineIdx--;
      this.saveHistory();
      this.renderer.redraw();
      this.updateButtons();
      this.coordTable.update();
    }
  }

  findLineAt(x, y, threshold = 8) {
    // Check proximity to any point or segment in each line
    for (let i = this.lines.length - 1; i >= 0; i--) {
      const line = this.lines[i];
      const pts = line.points;

      for (const p of pts)
        if (Math.hypot(p.x - x, p.y - y) <= threshold + 4) return i;

      for (let j = 0; j < pts.length - 1; j++)
        if (distToSegment(x, y, pts[j], pts[j + 1]) <= threshold) return i;
    }
    return -1;
  }

  showSelectionPanel(line) {
    document.getElementById('sel-color').value = line.color;
    document.getElementById('sel-thickness').value = line.thickness;
    document.getElementById('sel-marker-size').value = line.markerSize ?? this.markerSize;
    document.getElementById('sel-style').value = line.style;
    // Fill control appears only for locked areas
    const fillGroup = document.getElementById('sel-fill-group');
    if (fillGroup) {
      if (line.locked) {
        fillGroup.style.display = 'flex';
        const fs = fillState(line, this.defaultFillColor);
        document.getElementById('sel-fill-enabled').checked = fs.enabled;
        document.getElementById('sel-fill').value = fs.value;
      } else {
        fillGroup.style.display = 'none';
      }
    }
    document.getElementById('selection-panel').style.display = 'block';
    // Sync fullscreen overlay panel
    this.syncFsSelectionPanel(line);
    this.renderLinesList();
  }

  // Apply the locked-area fill from the selection panel controls.
  applyFill() {
    if (this.selectedLineIdx === -1) return;
    const line = this.lines[this.selectedLineIdx];
    if (!line) return;
    const enabled = document.getElementById('sel-fill-enabled').checked;
    const color = document.getElementById('sel-fill').value;
    line.fillColor = enabled ? color : 'transparent';
    this.saveHistory();
    this.renderer.redraw();
    this.storage.save();
  }

  syncFsSelectionPanel(line) {
    const fsPanel = document.getElementById('fs-selection-panel');
    if (!fsPanel) return;
    const isFS = document.body.classList.contains('fullscreen-mode');
    if (!isFS || !line) { fsPanel.style.display = 'none'; return; }
    // Always start at top:0; updateFsSelectionTop (called from show/hideControlsPanel) handles offset
    const fsCtrls = document.getElementById('fs-controls-panel');
    const ctrlsVisible = fsCtrls && fsCtrls.classList.contains('fs-panel-visible');
    fsPanel.style.transition = 'none'; // no transition on initial placement
    fsPanel.style.top = ctrlsVisible ? fsCtrls.getBoundingClientRect().height + 'px' : '0px';
    fsPanel.style.display = 'block';
    // Re-enable transition after placement
    requestAnimationFrame(() => { fsPanel.style.transition = ''; });
    // Expand top trigger to cover the selection panel
    requestAnimationFrame(() => {
      const trigger = document.getElementById('fs-top-trigger');
      if (trigger) trigger.style.height = Math.max(8, fsPanel.getBoundingClientRect().bottom) + 'px';
    });
    const fs = fillState(line, this.defaultFillColor);
    fsPanel.innerHTML = `<div class="selection-panel-inner">
            <span class="selection-label">${icon('pencil', { size: 14 })} Selected Line:</span>
            <div class="control-group"><label>Color:</label>
                <input type="color" id="fs-sel-color" value="${line.color}" style="width:60px;height:34px;cursor:pointer;border:1px solid var(--border-main);border-radius:4px;"></div>
            <div class="control-group"><label>Thickness:</label>
                <input type="number" id="fs-sel-thickness" value="${line.thickness}" min="1" max="20" style="width:70px;background:var(--input-bg);color:var(--input-text);border:1px solid var(--border-main);border-radius:4px;padding:6px 8px;font-size:14px;"></div>
            <div class="control-group"><label>Marker Size:</label>
                <input type="number" id="fs-sel-marker-size" value="${line.markerSize ?? this.markerSize}" min="1" max="30" style="width:70px;background:var(--input-bg);color:var(--input-text);border:1px solid var(--border-main);border-radius:4px;padding:6px 8px;font-size:14px;"></div>
            <div class="control-group"><label>Style:</label>
                <select id="fs-sel-style" style="background:var(--input-bg);color:var(--input-text);border:1px solid var(--border-main);border-radius:4px;padding:6px 8px;font-size:14px;">
                    <option value="solid"${line.style==='solid'?' selected':''}>Solid</option>
                    <option value="dashed"${line.style==='dashed'?' selected':''}>Dashed</option>
                    <option value="dotted"${line.style==='dotted'?' selected':''}>Dotted</option>
                </select></div>
            ${line.locked ? `<div class="control-group"><label>Fill:</label>
                <input type="checkbox" id="fs-sel-fill-enabled"${fs.enabled?' checked':''} style="vertical-align:middle;">
                <input type="color" id="fs-sel-fill" value="${fs.value}" style="width:60px;height:34px;cursor:pointer;border:1px solid var(--border-main);border-radius:4px;">
                <button id="fs-sel-fill-clear" type="button" title="Clear fill (make transparent)" style="background:#e67e22;color:#fff;border:none;padding:6px 10px;border-radius:4px;cursor:pointer;font-size:13px;">${icon('x', { size: 13 })}</button></div>` : ''}
            <button id="fs-sel-deselect" class="btn-icon-text" style="background:#e67e22;color:#fff;border:none;padding:6px 12px;border-radius:4px;cursor:pointer;font-size:13px;">${icon('x', { size: 13 })}<span>Deselect</span></button>
        </div>`;
    fsPanel.querySelector('#fs-sel-color').addEventListener('input', e => {
      this.applySelectionChange('color', e.target.value);
      document.getElementById('sel-color').value = e.target.value;
    });
    fsPanel.querySelector('#fs-sel-thickness').addEventListener('change', e => {
      this.applySelectionChange('thickness', parseInt(e.target.value));
      document.getElementById('sel-thickness').value = e.target.value;
    });
    fsPanel.querySelector('#fs-sel-marker-size').addEventListener('change', e => {
      this.applySelectionChange('marker-size', parseInt(e.target.value));
      document.getElementById('sel-marker-size').value = e.target.value;
    });
    fsPanel.querySelector('#fs-sel-style').addEventListener('change', e => {
      this.applySelectionChange('style', e.target.value);
      document.getElementById('sel-style').value = e.target.value;
    });
    const fsFillEnabled = fsPanel.querySelector('#fs-sel-fill-enabled');
    const fsFill = fsPanel.querySelector('#fs-sel-fill');
    if (fsFillEnabled && fsFill) {
      const applyFsFill = () => {
        if (this.selectedLineIdx === -1) return;
        const ln = this.lines[this.selectedLineIdx];
        if (!ln) return;
        ln.fillColor = fsFillEnabled.checked ? fsFill.value : 'transparent';
        const mainEnabled = document.getElementById('sel-fill-enabled');
        const mainFill = document.getElementById('sel-fill');
        if (mainEnabled) mainEnabled.checked = fsFillEnabled.checked;
        if (mainFill) mainFill.value = fsFill.value;
        this.saveHistory(); this.renderer.redraw(); this.storage.save();
      };
      fsFillEnabled.addEventListener('change', applyFsFill);
      fsFill.addEventListener('input', () => { fsFillEnabled.checked = true; applyFsFill(); });
      const fsFillClear = fsPanel.querySelector('#fs-sel-fill-clear');
      if (fsFillClear) fsFillClear.addEventListener('click', () => {
        fsFillEnabled.checked = false; applyFsFill();
        notify('Fill cleared (transparent)', 'ok');
      });
    }
    fsPanel.querySelector('#fs-sel-deselect').addEventListener('click', () => this.deselectLine());
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
    if (this.selectedLineIdx === -1) return;
    this.lines[this.selectedLineIdx][prop] = value;
    this.saveHistory();
    this.renderer.redraw();
  }

  findNearestPoint(x, y, threshold = 10) {
    const allPoints = [];

    this.lines.forEach(line => {
      line.points.forEach(p => allPoints.push(p));
    });

    if (this.currentLine) this.currentLine.points.forEach(p => allPoints.push(p));

    for (let point of allPoints) {
      const dist = Math.sqrt((point.x - x) ** 2 + (point.y - y) ** 2);
      if (dist < threshold) return point;
    }
    return null;
  }

  // Begin dragging a segment (shared by the mouse Alt-drag and the touch grab). Snapshots
  // the two endpoints + the whole line so a mid-drag Shift can translate the shape.
  // (x, y) are the grab point in canvas coords.
  beginSegmentDrag(nearSeg, x, y) {
    const line = this.lines[nearSeg.lineIdx];
    this.isDraggingSegment = true;
    this.draggingSegment = {
      lineIdx: nearSeg.lineIdx, ptIdx1: nearSeg.ptIdx1, ptIdx2: nearSeg.ptIdx2,
      startX: x, startY: y,
      origPt1: { x: line.points[nearSeg.ptIdx1].x, y: line.points[nearSeg.ptIdx1].y },
      origPt2: { x: line.points[nearSeg.ptIdx2].x, y: line.points[nearSeg.ptIdx2].y },
      origPoints: line.points.map(p => ({ x: p.x, y: p.y })),
    };
  }

  // Move the currently-dragged point (dp = draggingPoint) to canvas coords (x, y) and
  // refresh its coordinate row. Shared by the mouse and touch point-drag paths.
  movePointTo(dp, x, y) {
    const line = dp.lineIdx === -1 ? this.currentLine : this.lines[dp.lineIdx];
    if (!line) return;
    line.points[dp.ptIdx].x = x;
    line.points[dp.ptIdx].y = y;
    this.renderer.redraw();
    this.coordTable.refreshCoordRow(dp.ptIdx);
  }

  // Finish a point drag: clear state, save history (only for a placed line), commit.
  endPointDrag(dp, altKey) {
    this.isDraggingPoint = false;
    this.draggingPoint = null;
    if (dp && dp.lineIdx !== -1) this.saveHistory();
    this.finishDragGesture(altKey);
  }

  // Finish a segment drag: clear state, save history, commit.
  endSegmentDrag(altKey) {
    this.isDraggingSegment = false;
    this.draggingSegment = null;
    this.saveHistory();
    this.finishDragGesture(altKey);
  }

  // Apply the active segment/whole-line drag at the cursor. `shiftKey` decides the mode
  // live: held → translate the entire line shape; released → move only the grabbed segment's
  // two endpoints. Both derive from the original snapshot, so toggling Shift never accumulates.
  dragMove(clientX, clientY, shiftKey) {
    const { x, y } = this.canvasCoords(clientX, clientY);

    if (this.isDraggingSegment && this.draggingSegment) {
      const ds = this.draggingSegment;
      const line = this.lines[ds.lineIdx];
      if (!line) return;
      const dx = x - ds.startX;
      const dy = y - ds.startY;
      if (shiftKey) {
        line.points.forEach((p, i) => { p.x = ds.origPoints[i].x + dx; p.y = ds.origPoints[i].y + dy; });
        if (this.coordLineIdx === ds.lineIdx) this.coordTable.update(line.points, ds.lineIdx);
      } else {
        line.points.forEach((p, i) => { p.x = ds.origPoints[i].x; p.y = ds.origPoints[i].y; });
        line.points[ds.ptIdx1].x = ds.origPt1.x + dx;
        line.points[ds.ptIdx1].y = ds.origPt1.y + dy;
        line.points[ds.ptIdx2].x = ds.origPt2.x + dx;
        line.points[ds.ptIdx2].y = ds.origPt2.y + dy;
        this.coordTable.refreshCoordRow(ds.ptIdx1);
        this.coordTable.refreshCoordRow(ds.ptIdx2);
      }
      this.renderer.redraw();
      return;
    }

    if (this.isDraggingLine && this.draggingLine) {
      const dl = this.draggingLine;
      const line = this.lines[dl.lineIdx];
      if (!line) return;
      const dx = x - dl.startX;
      const dy = y - dl.startY;
      // Multi-select drag: translate EVERY selected line together (whole-line move).
      if (dl.multiOrig) {
        for (const { li, pts } of dl.multiOrig) {
          const l = this.lines[li];
          if (l) l.points.forEach((p, i) => { p.x = pts[i].x + dx; p.y = pts[i].y + dy; });
        }
        this.renderer.redraw();
        return;
      }
      // Whole line while Shift held (or if we never resolved a segment to fall back to).
      if (shiftKey || dl.ptIdx1 == null) {
        line.points.forEach((p, i) => { p.x = dl.origPoints[i].x + dx; p.y = dl.origPoints[i].y + dy; });
      } else {
        line.points.forEach((p, i) => { p.x = dl.origPoints[i].x; p.y = dl.origPoints[i].y; });
        [dl.ptIdx1, dl.ptIdx2].forEach(pi => {
          line.points[pi].x = dl.origPoints[pi].x + dx;
          line.points[pi].y = dl.origPoints[pi].y + dy;
        });
      }
      this.renderer.redraw();
      if (this.coordLineIdx === dl.lineIdx) this.coordTable.update(line.points, dl.lineIdx);
      return;
    }
  }

  // Does a click at (x,y) close the in-progress shape? (>= 3 points and within
  // markerSize + 8 image px of the first point.) Shared C++ core (wasm) when
  // loaded; the JS below is the reference + fallback.
  #shouldCloseShape(points, x, y, markerSize) {
    const fn = core.op('shouldCloseShape');
    if (fn) return fn(points, { x, y }, markerSize);
    if (points.length < 3) return false;
    const p0 = points[0];
    return Math.hypot(p0.x - x, p0.y - y) <= markerSize + 8;
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
      el.textContent = this.image ? 'Ready' : 'Open an image to begin';
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
    const cul = document.getElementById('custom-unit-label');
    if (cul) cul.textContent = lbl;
    const ths = document.querySelectorAll('#coordinates-table thead th');
    if (ths[3]) ths[3].textContent = `X ${lbl}`;
    if (ths[4]) ths[4].textContent = `Y ${lbl}`;
  }

  // Reset drag flags & cursor after any Alt-drag gesture (point/segment/line)
  finishDragGesture(altKey) {
    this.dragJustEnded = true;
    setTimeout(() => { this.dragJustEnded = false; }, 50);
    this.canvas.style.cursor = altKey ? 'grab' : 'crosshair';
  }

  // Find nearest point and return { lineIdx, ptIdx, point }
  findNearestPointWithIdx(x, y, threshold = 12) {
    // Check currentLine first
    if (this.currentLine) {
      for (let i = 0; i < this.currentLine.points.length; i++) {
        const p = this.currentLine.points[i];
        if (Math.hypot(p.x - x, p.y - y) < threshold) return { lineIdx: -1, ptIdx: i, point: p };
      }
    }
    for (let li = this.lines.length - 1; li >= 0; li--) {
      for (let pi = 0; pi < this.lines[li].points.length; pi++) {
        const p = this.lines[li].points[pi];
        if (Math.hypot(p.x - x, p.y - y) < threshold) return { lineIdx: li, ptIdx: pi, point: p };
      }
    }
    return null;
  }

  // Find the nearest line segment to (x,y) among completed lines.
  // Returns { lineIdx, ptIdx1, ptIdx2 } or null.
  findNearestSegmentWithIdx(x, y, threshold = 12) {
    let bestDist = Infinity;
    let best = null;
    for (let li = this.lines.length - 1; li >= 0; li--) {
      const pts = this.lines[li].points;
      for (let pi = 0; pi < pts.length - 1; pi++) {
        const d = distToSegment(x, y, pts[pi], pts[pi + 1]);
        if (d < threshold && d < bestDist) {
          bestDist = d;
          best = { lineIdx: li, ptIdx1: pi, ptIdx2: pi + 1 };
        }
      }
    }
    return best;
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

  // Rotate the selected line(s) together about their combined bounding-box centre.
  #rotateLines(indices, angle) {
    const lines = indices.map((i) => this.lines[i]).filter((l) => l && l.points.length);
    if (!lines.length) return;
    const { x: cx, y: cy } = this.#bboxCenterOf(lines.flatMap((l) => l.points));
    for (const l of lines) this.#rotatePointsAbout(l.points, cx, cy, angle);
    this.renderer.redraw();
    clearTimeout(this.#rotateSaveTimer);
    this.#rotateSaveTimer = setTimeout(() => { this.saveHistory(); this.storage.save(); }, 280);
  }

  rotateSelectedLine(angle) {
    // 2+ selected → rotate the whole set about their combined centre.
    const sel = this.selectedIndices();
    if (sel.length >= 2) { this.#rotateLines(sel, angle); return; }
    const line = this.lines[this.selectedLineIdx];
    if (!line || line.points.length < 2) return;
    // One selected: pivot on the focused point when there is one, else the line's bbox centre.
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
    clearTimeout(this.#rotateSaveTimer);
    this.#rotateSaveTimer = setTimeout(() => { this.saveHistory(); this.storage.save(); }, 280);
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
    clearTimeout(this.#rotateSaveTimer);
    this.#rotateSaveTimer = setTimeout(() => { this.saveHistory(); this.storage.save(); }, 280);
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
      this.lines = result;
      this.renderer.redraw();
      this.updateButtons();
      this.coordTable.update();
    }
  }

  redo() {
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
      this.lines = result;
      this.renderer.redraw();
      this.updateButtons();
      if (this.lines.length > 0) this.coordTable.update(this.lines[this.lines.length - 1].points);
    }
  }

  updateButtons() {
    // No image → nothing to draw on, so undo/redo are meaningless (and there's
    // no history to act on anyway). Keep them disabled until an image exists.
    if (!this.image) {
      document.getElementById('undo').disabled = true;
      document.getElementById('redo').disabled = true;
    } else if (this.isDrawing && this.currentLine) {
      document.getElementById('undo').disabled = this.currentLine.points.length === 0;
      document.getElementById('redo').disabled = !this.undonePoints || this.undonePoints.length === 0;
    } else {
      document.getElementById('undo').disabled = !this.history.canUndo();
      document.getElementById('redo').disabled = !this.history.canRedo();
    }
    // Fullscreen only makes sense with an image to view. Never disable while
    // already in fullscreen (so the user can always get back out).
    const fsBtn = document.getElementById('fullscreen-toggle');
    if (fsBtn && !document.body.classList.contains('fullscreen-mode'))
      fsBtn.disabled = !this.image;
    // Zoom controls are meaningless on an empty void — disable until an image
    // is loaded (the wheel/hotkey zoom paths are guarded in zoomPan + wheel).
    const noImage = !this.image;
    for (const id of ['zoom-in', 'zoom-out', 'zoom-fit', 'zoom-input']) {
      const el = document.getElementById(id);
      if (el) el.disabled = noImage;
    }
    // The blank-image creator icon lives on the empty canvas — only the idle
    // (imageless) state shows it; with an image loaded it would cover content.
    const idleCreate = document.getElementById('idle-create-wrap');
    if (idleCreate) idleCreate.style.display = noImage ? '' : 'none';

    // ── Gate every image/lines-dependent action ──────────────────
    // No image → nothing to draw/transform/export, so these are disabled; their
    // data-disabled-reason (in the markup) feeds the tooltip via composeControlTitle to
    // explain why. Layout export/clear also need at least one line.
    const hasImage = !!this.image;
    const hasLines = this.lines && this.lines.length > 0;
    const setDisabled = (id, off) => { const el = document.getElementById(id); if (el) el.disabled = off; };
    setDisabled('start-drawing', !hasImage || this.isDrawing);
    setDisabled('stop-drawing', !this.isDrawing);
    setDisabled('draw-mode-toggle', !hasImage);
    setDisabled('crop-image', !hasImage);
    setDisabled('rotate-left', !hasImage);
    setDisabled('rotate-right', !hasImage);
    setDisabled('image-filter', !hasImage);
    setDisabled('save-image', !hasImage);
    // Image Links edit the CURRENT image's provenance — nothing to edit without one.
    setDisabled('links-btn', !hasImage);
    setDisabled('download-json', !hasLines);
    setDisabled('copy-json-btn', !hasLines);
    setDisabled('clear-all-lines', !hasLines);
    // State-aware Image section: the compact "Load Image" button shows only when
    // empty; the image-actions group (download/copy/share/open) shows only with an
    // image. (The file input itself stays hidden — it's just the picker target.)
    const loadBtn = document.getElementById('load-image-btn');
    if (loadBtn) loadBtn.style.display = hasImage ? 'none' : '';
    const imgActions = document.getElementById('image-actions');
    if (imgActions) imgActions.style.display = hasImage ? 'inline-flex' : 'none';
    // "Open in…" hides entirely when neither target is available (nothing to open into),
    // so it never shows a dead/greyed control. Availability tracks the loaded config +
    // whether this is a server project (see openInAvailable).
    const openInBtn = document.getElementById('open-in-btn');
    if (openInBtn) openInBtn.style.display = (hasImage && this.openInAvailable()) ? '' : 'none';
    // Clear/remove-current-project is hidden for SERVER projects: a server project is removed
    // only from the projects list (its Remove action), so the toolbar never offers a local-only
    // clear that reads ambiguously ("did it delete on the server too?"). Local / temporary
    // editors keep it (clear a local project, or reset a blank editor).
    const clearBtn = document.getElementById('clear-storage');
    if (clearBtn) clearBtn.style.display = this.remoteLink ? 'none' : '';
    // Recompose tooltips so the reason line appears/clears with the disabled state
    // (and hotkey buttons keep their combo). Covers every control carrying either
    // a hotkey id or a disabled-reason.
    document.querySelectorAll('[data-disabled-reason], [data-hk-title]').forEach(el => {
      el.title = composeControlTitle(el, hotkeys.isMac, id => hotkeys.get(id));
    });

    this.updateIncognitoUI();
    this.updateProjectTitle();
    this.renderLinesList();
  }

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
      // `editable` (a saved, non-incognito project) only gates the rename affordance;
      // the field itself stays a read-only title until the user enters edit mode.
      input.disabled = !editable;
      if (!this.nameEditing) input.readOnly = true;
      input.placeholder = editable ? 'Untitled' : (this.storage.incognito ? 'Incognito (unsaved)' : 'No project');
      if (editBtn && !this.nameEditing) editBtn.style.display = editable ? '' : 'none';
      this.nameEditor?.refresh();                      // set ✓ enabled/disabled state
    }
    // Paint the name field in the project's custom colour; with no custom colour it falls back to
    // ONE fixed neutral grey in BOTH themes, with a theme-flipped shadow (dark on light, light on
    // dark) for legibility. The grey + shadow are set explicitly here (not via a CSS var) so a
    // stale-cached theme.css can never leave the name colourless/shadowless. Show the colour swatch
    // only for a saved (non-incognito) project.
    if (input) {
      const projColor = (editable && this.activeProjectId != null)
        ? (this.storage.store.getMeta(this.activeProjectId)?.color || '')
        : '';
      // Custom colour overrides the CSS default grey (--project-name-fg); clearing the inline
      // colour when unset lets CSS supply the grey. The legibility shadow is left ENTIRELY to CSS
      // (--project-name-shadow, a theme-flipped contrasting outline) so it re-flips live when the
      // theme is toggled — setting it inline here would freeze it to the paint-time theme.
      input.style.color = projColor || '';
      input.style.textShadow = '';
      const colorBtn = document.getElementById('project-color-btn');
      if (colorBtn && !this.nameEditing) {
        colorBtn.style.display = editable ? '' : 'none';
        colorBtn.style.color = projColor || 'var(--text-muted)';
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
      if (remote) badge.title = `Editing a project stored on ${remote.address}`;
    }
    const canvasViewport = document.getElementById('canvas-viewport');
    if (canvasViewport) canvasViewport.classList.toggle('remote-editing', !!remote);
  }

  updateInfo() {
    const info = document.getElementById('image-info');
    const sizeDisplay = document.getElementById('image-size-display');
    const blankBtn = document.getElementById('blank-color-btn');
    const blankSwatch = document.getElementById('blank-color-swatch');
    // A blank project writes "· blank" into the size readouts + reveals the recolour swatch.
    const isBlank = this.activeIsBlank();
    if (this.image) {
      const blankTag = isBlank ? '  ·  blank' : '';
      // Just the image size — the shortcut hints live in the "?" popup (hints-btn), not this bar.
      info.textContent = `Image Size: ${this.canvas.width} × ${this.canvas.height} px${blankTag}`;
      if (sizeDisplay) {
        sizeDisplay.innerHTML = `${icon('ruler', { size: 13 })} ${this.canvas.width} × ${this.canvas.height} px${isBlank ? ' · blank' : ''}`;
        sizeDisplay.style.display = 'inline-flex';
      }
    } else {
      info.textContent = 'No image loaded. Upload an image to start.';
      if (sizeDisplay) sizeDisplay.style.display = 'none';
    }
    // Blank-fill recolour swatch: sits beside the size pill, shown only for a blank project.
    if (blankBtn) blankBtn.style.display = (this.image && isBlank) ? 'inline-flex' : 'none';
    if (blankSwatch && isBlank) blankSwatch.style.background = this.blankColor || '#ffffff';
  }

  // Transient status line next to the toolbar. `iconName` (optional) prepends a
  // themed SVG glyph; `color` accepts a CSS color or a var() string so callers
  // use the shared status tokens (var(--success) etc.) instead of hex literals.
  showSaveStatus(msg, color, iconName = null) {
    const el = document.getElementById('save-status');
    if (!el) return;
    el.innerHTML = (iconName ? icon(iconName, { size: 13 }) : '') + `<span>${msg}</span>`;
    el.style.color = color;
    el.style.display = 'inline-flex';
    el.style.alignItems = 'center';
    el.style.gap = '4px';
    clearTimeout(this.#saveStatusTimer);
    this.#saveStatusTimer = setTimeout(() => { el.innerHTML = ''; }, 3000);
  }

  restoreFromLocalStorage() {
    this.storage.restore();
  }

  // ── Multi-project navigation (called by the projects modal) ──────
  // Switch the editor to a saved project, persisting the current one first.
  switchToProject(id) {
    if (id === this.activeProjectId) return false;
    if (!this.storage.temporary && this.activeProjectId != null) this.storage.save();
    if (this.storage.loadProject(id)) {
      this.activeProjectId = id;
      // Restore the remote link from meta so a reopened server-backed project keeps its
      // identity (outline + write-back); purely-local projects clear it.
      const meta = this.storage.store.getMeta(id);
      this.remoteLink = (meta && meta.remoteId && meta.address)
        ? { address: meta.address, remoteId: meta.remoteId, version: meta.remoteVersion || 0 }
        : null;
      // Restore the blank-fill colour so the blank-colour control reappears for a reopened blank.
      this.blankColor = (meta && meta.blank && meta.blankColor) ? meta.blankColor : '';
      this.tabs.reportActive(id);
      this.updateProjectTitle();   // reflect (or clear) the remote badge + outline now
      // Server-linked: pull the latest so a reopen shows peers' newest state, not stale cache.
      if (this.remoteLink && getSyncToServer()) this.remoteSync.reloadRemoteActive();
      return true;
    }
    return false;
  }

  // Open a saved project in a NEW browser tab, leaving this tab untouched. The
  // new tab boots with a "?open=<id>" deep link that applyProjectDeepLink()
  // consumes. Default open-in-current-tab behavior stays on switchToProject().
  openProjectInNewTab(id, win = null) {
    if (id == null) { if (win) win.close(); return; }
    const base = location.origin + location.pathname;
    const url = buildOpenProjectUrl(base, id);
    // `win` is a tab the caller pre-opened synchronously (inside the user gesture) so a strict
    // popup blocker can't swallow it after an async confirm; navigate it instead of opening anew.
    if (win) win.location = url; else window.open(url, '_blank');
  }

  // Open a SERVER project in a new tab via the server-launch fragment (consumed by
  // applyExternalLaunch on the new tab). Mirrors openProjectInNewTab for local ids.
  openRemoteProjectInNewTab(meta, win = null) {
    if (!meta || !meta.serverUrl || meta.id == null) { if (win) win.close(); return; }
    const base = location.origin + location.pathname;
    const url = buildExternalLaunchUrl(base, { server: { url: meta.serverUrl, id: meta.id, version: meta.version || 0 } });
    if (win) win.location = url; else window.open(url, '_blank');
  }

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

  // Promise-based text prompt (shares the confirm modal). Resolves the trimmed string, or
  // null on cancel. opts: { title, confirmLabel, defaultValue }. Used for copy-with-name.
  prompt(message, opts = {}) {
    const el = document.getElementById('confirm-modal-overlay');
    if (el && typeof el.prompt === 'function') return el.prompt(message, opts);
    return Promise.resolve(opts.defaultValue ?? null);
  }

  // Start a fresh blank (unsaved) editor.
  newEditor() {
    this.remoteLink = null;
    this.pendingRemoteAddress = null;   // drop any un-consumed newEditor({ address }) arming
    this.blankColor = '';               // fresh editor is not a blank project until one is created
    this.storage.newTemporary();
    this.tabs.reportActive(null);
    this.#reportIncognitoSession();   // newTemporary clears incognito → drop our peer entry
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
  // (default off); `keepAnnotations` keeps the existing lines over the new image (default on).
  // Any image change unpins the now-stale extension pin (handled in loadImageFromFile).
  replaceProjectImage(file, { rename = false, keepAnnotations = true } = {}) {
    if (!file) return;
    this.loadImageFromFile(file, { replaceInPlace: true, rename, keepAnnotations });
  }

  // Create a solid-color blank image and load it (blank-image creator's core, shared with the
  // console API). width/height in px (clamped 1–8192); omitted → current page size, like the
  // modal. Returns a Promise resolving { width, height } once handed to the loader, rejecting
  // if the canvas can't be encoded — so both callers can report accurately.
  // `address` (a connected server URL) also creates+links the project on that server
  // (mirrors loadImageFromFile's create-on-server path); validated up front so a bad
  // target rejects before the local image is replaced.
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
        { replaceInPlace: true, keepAnnotations: true, blankColor: next });
      if (this.activeProjectId != null) {
        this.storage.store.setBlankColor(this.activeProjectId, next);
        this.tabs.projectsChanged({ id: this.activeProjectId, action: PROJECT_ACTION.UPDATED });
        this.#pushProjectFieldToServer(this.activeProjectId, { blankColor: next }, 'Could not set blank colour on the server');
      }
      this.updateButtons();
    }).catch(() => notify('Could not recolour the blank image', 'fail'));
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

  // The current session as an "Open in…" hand-off payload (the #stencil= fragment shape):
  // a server reference for a linked session (the receiver re-fetches — no bytes, no token),
  // else the inline image + full layout. Consumed by the Open-in modal for the desktop
  // stencil:// link and by anything that mirrors this session into another front-end.
  openInLaunchPayload({ incognito = false } = {}) {
    const p = this.remoteLink
      ? {
        server: {
          url: this.remoteLink.address,
          id: this.remoteLink.remoteId,
          version: this.remoteLink.version || 0,
        },
      }
      : {
        dataUrl: this.imageDataUrl,
        name: `${this.imageBaseName || 'image'}.${this.imageExt || 'png'}`,
        layout: this.currentLayoutPayload(),
      };
    if (!this.remoteLink) {
      if (this.imageSource) p.source = this.imageSource;
      if (this.imageResource) p.resource = this.imageResource;
    }
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
    this.updateIncognitoUI();
    this.updateProjectTitle();
    notify(`Published to ${conn.url}`, 'ok');
    return this.remoteLink;
  }

  // Permanently delete every saved project, then drop to a blank editor.
  clearAllProjects() {
    this.storage.store.clearAll();
    this.storage.newTemporary();
    this.tabs.reportActive(null);
    this.tabs.projectsChanged({ action: PROJECT_ACTION.CLEARED });
  }

  // Prolong a project: reset its 7-day expiry window to start from now. Notifies
  // peers so their open project lists re-render with the new expiry.
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

  // Remove an entire committed line by index.
  removeLine(idx) {
    if (idx < 0 || idx >= this.lines.length) return this;
    this.lines.splice(idx, 1);
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

  renewProject(id) {
    const meta = this.storage.store.renew(id);
    if (meta) this.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
    return meta;
  }

  // Set a project's expiration fields exactly (from the expiration modal / facade).
  // opts: { expiresAt (0 = keep forever), refreshPeriod, autoRefresh }. Broadcasts so
  // the projects list + any open expiration dialog in other tabs re-render.
  setProjectExpiration(id, opts = {}) {
    const meta = this.storage.store.setExpiration(id, opts);
    if (meta) {
      this.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
      // An explicit expiry change (not a refreshPeriod/autoRefresh-only tweak, which are
      // local-only concepts) propagates to the collaboration server for a server-linked
      // project — best-effort, like setProjectColor. Server projects otherwise have no
      // expiry unless one is set here explicitly.
      if (Object.prototype.hasOwnProperty.call(opts, 'expiresAt')) {
        this.#pushProjectFieldToServer(id, { expiresAt: meta.expiresAt || 0 }, 'Could not set expiration on the server');
      }
    }
    return meta;
  }

  // Close a project's editor (without deleting the saved project). Active in THIS tab →
  // blank editor; open in ANOTHER tab → ask it to via a CLOSE broadcast. `fully` also closes
  // this tab/window (best-effort — only script-opened windows can self-close).
  closeProject(id, { fully = false } = {}) {
    if (id != null && id === this.activeProjectId) this.newEditor();
    else if (id != null) this.tabs.projectsChanged({ id, action: PROJECT_ACTION.CLOSE });
    if (fully) { try { window.close(); } catch { /* not closeable */ } }
    return this;
  }

  // Rename a project. Registry meta is the source of truth for the projects list, and
  // save()'s name fallback prefers it over imageBaseName, so an active-project rename
  // survives saves. Notifies peers to re-render. Returns updated meta (null for unknown id).
  renameProject(id, name) {
    const clean = String(name || '').trim();
    if (!clean) return null;
    // Names must be unique across projects. The UI surfaces null as "kept old name";
    // the console's Project.name setter checks store.nameExists() first to throw.
    if (this.storage.store.nameExists(clean, id)) {
      notify(`A project named “${clean}” already exists`, 'fail');
      return null;
    }
    const meta = this.storage.store.rename(id, clean);
    if (meta) {
      // The project name is THE name: keep the working/download name (imageBaseName)
      // in lockstep for the active project, no matter which surface renamed it
      // (topbar, projects list, links modal, console). No separate image name to track.
      if (id === this.activeProjectId) {
        this.imageBaseName = clean;
        this.updateProjectTitle();   // refresh tab title + topbar field
      }
      this.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
      // Push the rename to the server immediately (like setProjectColor), so peers see it live
      // — previously a rename only reached the server on the next layout save.
      this.#pushProjectFieldToServer(id, { name: clean }, 'Could not rename the project on the server');
    }
    return meta;
  }

  // Push a single field change (rename / colour) to the collaboration server for a
  // server-linked project. The active project uses its live remoteLink (and adopts the bumped
  // version); a non-active linked project uses its stored meta. Version-guarded + best-effort
  // (no-op when not linked / sync off) — a failure only notifies with `failMsg`.
  async #pushProjectFieldToServer(id, fields, failMsg) {
    if (!getSyncToServer()) return;
    const active = id === this.activeProjectId && !!this.remoteLink;
    const meta = this.storage.store.getMeta(id) || {};
    const address = active ? this.remoteLink.address : meta.address;
    const remoteId = active ? this.remoteLink.remoteId : meta.remoteId;
    if (!address || !remoteId) return;
    let conn;
    try {
      conn = requireConnection(this.connections, address);
    } catch (err) {
      notify(err.message, 'fail');
      return;
    }
    // Version-guarded write with a bounded conflict retry (mirrors the CLI's
    // putProjectField). A stale cached version — a concurrent field push / layout
    // save from THIS client racing on remoteLink.version, or a peer's edit — 409s;
    // re-read the server's current version and retry so the change isn't silently
    // lost. Single-field sets are idempotent, so last-writer-wins is correct here.
    let version = active ? this.remoteLink.version : (meta.remoteVersion || 0);
    for (let attempt = 0; attempt < 4; attempt++) {
      try {
        const rec = await conn.updateProject(remoteId, { ...fields, version });
        // Adopt the bumped version only if remoteLink still points at this same
        // project (the user may have switched projects during the await).
        if (rec && rec.version != null && this.activeProjectId === id
            && this.remoteLink && this.remoteLink.remoteId === remoteId) {
          this.remoteLink = { ...this.remoteLink, version: rec.version };
        }
        return;
      } catch (err) {
        if (err && err.status === 409 && attempt < 3) {
          version = await this.#currentRemoteVersion(conn, remoteId, version);
          continue;
        }
        notify(`${failMsg} — ${err.message}`, 'fail');
        return;
      }
    }
  }

  // Re-read a linked project's current server version (after a 409 or a file write
  // that bumps it without returning it), falling back to `fallback` on any error.
  async #currentRemoteVersion(conn, remoteId, fallback) {
    try {
      const full = await conn.getProject(remoteId);
      const v = full && full.project ? full.project.version : undefined;
      return v == null ? fallback : v;
    } catch {
      return fallback;
    }
  }

  // Set (or clear) a project's accent colour — the custom colour its NAME is painted in
  // wherever it appears. An empty/whitespace `color` clears it (back to the theme accent);
  // a valid hex is normalised to "#rrggbb". Invalid hex is rejected (keeps the old colour).
  // Persists to the registry, repaints the active-project UI, notifies peers, and pushes the
  // colour to the server for a server-linked project. Returns updated meta (null for unknown id).
  setProjectColor(id, color) {
    const raw = String(color == null ? '' : color).trim();
    let next = '';   // empty → explicit clear (theme fallback)
    if (raw) {
      next = normalizeHex(raw);
      if (!next) {
        notify(`“${color}” is not a valid hex colour`, 'fail');
        return null;
      }
    }
    const meta = this.storage.store.setColor(id, next);
    if (!meta) return null;
    if (id === this.activeProjectId) this.updateProjectTitle();
    this.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
    // Best-effort server push for a server-linked project (no-op when not linked).
    this.#pushProjectFieldToServer(id, { color: next }, 'Could not set project colour on the server');
    return meta;
  }

  // Set a project's search keywords (normalized by the store). Mirrors setProjectColor:
  // writes local meta, broadcasts to peer tabs, and best-effort pushes to the server for a
  // server-linked project. Returns the stored meta, or null on unknown id.
  setProjectKeywords(id, keywords) {
    const meta = this.storage.store.setKeywords(id, keywords);
    if (!meta) return null;
    this.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
    this.#pushProjectFieldToServer(id, { keywords: meta.keywords }, 'Could not set project keywords on the server');
    return meta;
  }

  // Set a project's blank-fill colour by id. No-op (null) for a non-blank project (only blanks have
  // a blank colour). When `id` is the ACTIVE project, recolours the visible background in place
  // (setBlankColor); otherwise updates the stored meta + peers + server. `color` is any normalizeHex
  // form. Returns the stored meta, or null.
  setProjectBlankColor(id, color) {
    const cur = this.storage.store.getMeta(id);
    if (!cur || !cur.blank) return null;
    const next = normalizeHex(color);
    if (!next) return null;
    if (id === this.activeProjectId) { this.setBlankColor(next); return this.storage.store.getMeta(id); }
    const meta = this.storage.store.setBlankColor(id, next);
    if (!meta) return null;
    this.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
    this.#pushProjectFieldToServer(id, { blankColor: meta.blankColor }, 'Could not set blank colour on the server');
    return meta;
  }

  // Remove one project; if it's the active one, drop to a blank editor.
  removeProject(id) {
    if (id === this.activeProjectId) {
      this.storage.store.remove(id);
      this.storage.newTemporary();
      this.tabs.reportActive(null);
    } else {
      this.storage.store.remove(id);
    }
    this.tabs.projectsChanged({ id, action: PROJECT_ACTION.REMOVED });
  }

  // ── Move / copy a project between local storage and a server ──────
  // Create a NEW server project from a local project's content (original bytes + annotated
  // layout) under `name`. Shared by move (then links the local) and copy (leaves local as-is).
  // Returns { link, proj, meta }. Flushes the active project first so the server gets latest.
  async #createServerFromLocal(id, address, name = null) {
    const conn = requireConnection(this.connections, address);
    if (id === this.activeProjectId && !this.storage.temporary) this.storage.save();   // flush latest
    const proj = this.storage.store.get(id);
    if (!proj) throw new Error('Project not found');
    const meta = this.storage.store.getMeta(id) || {};
    const payload = proj.payload || {};
    const layout = payload.layout || {};
    // Decode the stored original (a data URL) to raw bytes for the codec-free server.
    let bytes = null;
    let ext = meta.imageExt || layout.imageExt || 'png';
    const w = layout.imageWidth || meta.imageW || 0;
    const h = layout.imageHeight || meta.imageH || 0;
    if (payload.image) {
      const blob = await (await fetch(payload.image)).blob();
      bytes = new Uint8Array(await blob.arrayBuffer());
      if (blob.type && blob.type.includes('/')) ext = blob.type.split('/')[1];
    }
    const projName = (name && name.trim()) || meta.name || layout.imageBaseName || 'Untitled';
    const link = await createRemoteProject(conn, {
      name: projName,
      source: meta.source || layout.imageSource || '',
      resource: meta.resource || layout.imageResource || '',
      color: meta.color || '',
      bytes, ext, w, h,
    });
    // Push the annotated layout (lines + filter) so the server holds the full project.
    // The layout save bumps the server version again, so adopt the refreshed link it
    // returns — otherwise `link.version` stays at the create-time value and the next
    // version-guarded field push (colour / rename / expiry) 409s against the server.
    const savedLink = await saveRemoteProject(conn, link, {
      name: projName,
      layout: buildLayoutPayload({
        imageWidth: w, imageHeight: h,
        lines: layout.lines || [],
        imageFilter: layout.imageFilter,
        filterColor: layout.filterColor,
        cropRect: layout.cropRect,
        rotationQuarters: layout.rotationQuarters,
        pageSize: layout.pageSize,
        customPageWidth: layout.customPageWidth,
        customPageHeight: layout.customPageHeight,
        allowFormulas: layout.allowFormulas,
        formulaX: layout.formulaX,
        formulaY: layout.formulaY,
      }),
    });
    return { link: savedLink, proj, meta };
  }

  // Local → server: create the project on `address`, then LINK the local copy to it (keeping
  // the editor open + the row in place). Returns the new remote id.
  async moveProjectToServer(id, address) {
    const { link, proj, meta } = await this.#createServerFromLocal(id, address);
    const linkedMeta = { ...meta, id, address: link.address, remoteId: link.remoteId, remoteVersion: link.version };
    this.storage.store.upsert(linkedMeta, proj.payload || {});
    if (id === this.activeProjectId) {
      this.remoteLink = { address: link.address, remoteId: link.remoteId, version: link.version };
      this.updateProjectTitle();   // reflect the golden remote outline now
    }
    this.tabs.projectsChanged({ id, action: PROJECT_ACTION.UPDATED });
    return link.remoteId;
  }

  // Local → server COPY: create a new server project from the local one (default name
  // "<name>-copy") and LEAVE the local project untouched. Returns the new remote id.
  async copyProjectToServer(id, address, { name } = {}) {
    const base = this.storage.store.getMeta(id)?.name || 'Untitled';
    const copyName = (name && name.trim()) || `${base}-copy`;
    const { link } = await this.#createServerFromLocal(id, address, copyName);
    this.tabs.projectsChanged({ action: PROJECT_ACTION.UPDATED });   // refresh the remote rows
    return link.remoteId;
  }

  // Server → local: fetch the server project's image + layout, save it as a new
  // local project, then delete it from the server. `meta` is a remote-project meta
  // ({ id, serverUrl, name, source }). Returns the new local project id.
  async moveProjectToLocal(meta) {
    // If the moved server project is the open session (or its local cache), follow it to the
    // new local id so the editor stays open + focused instead of pointing at a deleted server id.
    const openCacheId = (this.remoteLink && this.remoteLink.remoteId === meta.id
      && this.remoteLink.address === meta.serverUrl) ? this.activeProjectId : null;
    const newId = await this.#importServerProjectToLocal(meta, { removeFromServer: true });
    if (openCacheId != null) {
      if (openCacheId !== newId) this.storage.store.remove(openCacheId);   // drop the now-stale cache
      this.switchToProject(newId);
    }
    return newId;
  }

  // Make a detached LOCAL copy of a server project, leaving the server copy in place. Default
  // name "<name>-copy" (override via `name`). Returns the new local project id; caller opens it.
  async copyServerProjectToLocal(meta, { name } = {}) {
    return this.#importServerProjectToLocal(meta, { removeFromServer: false, copy: true, name });
  }

  // Copy a server project into an INCOGNITO session (no local record, no server link). Current
  // tab: replace the editor with the image + annotations as incognito. New tab: hand off the
  // image via the external-launch URL (image only — the launch payload carries no annotations).
  async copyServerProjectToIncognito(meta, { newTab = false } = {}) {
    const conn = requireConnection(this.connections, meta.serverUrl);
    const full = await conn.getProject(meta.id);
    const src = full.project?.source || meta.source || '';
    const blob = await this.remoteSync.fetchRemoteOriginal(conn, meta.id, src);
    if (!blob) throw new Error('no image bytes on the server');
    const ext = (blob.type && blob.type.split('/')[1]) || 'png';
    const name = full.project?.name || meta.name || 'Untitled';
    if (newTab) {
      const dataUrl = await this.#blobToDataUrl(blob);
      const url = buildExternalLaunchUrl(location.origin + location.pathname, { dataUrl, name, incognito: true });
      window.open(url, '_blank');
      return;
    }
    const file = new File([blob], `${name}.${ext}`, { type: blob.type || 'image/png' });
    if (!this.storage.incognito) this.storage.save();   // flush any current project first
    this.newEditor();
    this.storage.incognito = true;
    this.updateIncognitoUI();
    // adoptLayout applies the lines/filter/crop/page/formulas without linking (no remoteId).
    this.loadImageFromFile(file, { source: src, resource: full.project?.resource || '', layout: full.layout, adoptLayout: true });
  }

  // Read a Blob into a data URL (used by the new-tab incognito hand-off).
  #blobToDataUrl(blob) {
    return new Promise((res, rej) => {
      const r = new FileReader();
      r.onload = () => res(r.result);
      r.onerror = () => rej(new Error('could not read image bytes'));
      r.readAsDataURL(blob);
    });
  }

  // Shared body of move/copy server→local: fetch image + layout, persist a fresh detached
  // local project (crop/rotation included), optionally delete the server copy. `copy` defaults
  // the name to "<base>-copy"; an explicit `name` overrides.
  async #importServerProjectToLocal(meta, { removeFromServer = false, copy = false, name = null } = {}) {
    const conn = requireConnection(this.connections, meta.serverUrl);
    const full = await conn.getProject(meta.id);
    const src = full.project?.source || meta.source || '';
    const blob = await this.remoteSync.fetchRemoteOriginal(conn, meta.id, src);
    const dataUrl = blob ? await this.#blobToDataUrl(blob) : null;
    const sl = full.layout || {};
    const newId = this.storage.store.createId();
    const base = full.project?.name || meta.name || 'Untitled';
    const projName = (name && name.trim()) || (copy ? `${base}-copy` : base);
    const localMeta = {
      id: newId,
      name: projName,
      color: full.project?.color || '',
      thumbnail: dataUrl,
      createdAt: Date.now(),
      hasImage: !!dataUrl,
      imageW: sl.imageWidth || 0,
      imageH: sl.imageHeight || 0,
      source: src || null,
      resource: full.project?.resource || null,
      address: null,
      remoteId: null,
      remoteVersion: 0,
    };
    this.storage.store.upsert(localMeta, {
      image: dataUrl,
      layout: {
        imageWidth: sl.imageWidth || 0,
        imageHeight: sl.imageHeight || 0,
        lines: Array.isArray(sl.lines) ? sl.lines : [],
        imageFilter: sl.imageFilter || 'none',
        filterColor: sl.filterColor || '#7c3aed',
        cropRect: sl.cropRect || null,
        rotationQuarters: sl.rotationQuarters || 0,
        // Carry page format + formulas so the detached local copy keeps them.
        pageSize: sl.pageSize || 'A3',
        customPageWidth: sl.customPageWidth || 21,
        customPageHeight: sl.customPageHeight || 29.7,
        allowFormulas: !!sl.allowFormulas,
        formulaX: sl.formulaX || '',
        formulaY: sl.formulaY || '',
        imageBaseName: projName,
        imageExt: (blob && blob.type && blob.type.includes('/')) ? blob.type.split('/')[1] : 'png',
        imageSource: src || null,
        imageResource: full.project?.resource || null,
      },
    });
    // Remove from the server only for a move (the live feed re-renders its golden row out).
    if (removeFromServer) await conn.deleteProject(meta.id);
    this.tabs.projectsChanged({ id: newId, action: PROJECT_ACTION.UPDATED });
    return newId;
  }

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
    if (action === PROJECT_ACTION.REMOVED && id === this.activeProjectId) {
      this.storage.newTemporary();
      this.tabs.reportActive(null);
      this.updateButtons();
      notify('This project was removed in another tab', 'info');
      return;
    }
    if (action === PROJECT_ACTION.CLEARED && this.activeProjectId != null) {
      this.storage.newTemporary();
      this.tabs.reportActive(null);
      this.updateButtons();
      notify('All projects were cleared in another tab', 'info');
      return;
    }
    if (action === PROJECT_ACTION.CLOSE && id === this.activeProjectId) {
      this.storage.newTemporary();
      this.tabs.reportActive(null);
      this.updateButtons();
      notify('This project was closed from another tab', 'info');
      return;
    }
    if (action === PROJECT_ACTION.UPDATED && id === this.activeProjectId) {
      if (this.#isIdle()) this.storage.syncActiveFromStorage();
      // A colour change lives in the registry meta (not the payload syncActiveFromStorage
      // reloads), so always repaint the name from the freshly-read meta.
      this.updateProjectTitle();
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
    if ((!this.lines || this.lines.length === 0) && (!this.currentLine || this.currentLine.points.length === 0)) {
      notify('No lines to clear', 'info');
      return;
    }
    if (!(await this.confirm('Wipe ALL lines from the canvas? This cannot be undone except via Undo.', { title: 'Clear all lines', danger: true }))) {
      notify('Clear canceled', 'fail');
      return;
    }
    this.lines = [];
    if (this.currentLine) this.currentLine.points = [];
    this.selectedLineIdx = -1;
    this.coordLineIdx = -1;
    this.focusedPtIdx = -1;
    this.hoveredPtIdx = -1;
    this.hideSelectionPanels();
    this.saveHistory();
    this.coordTable.update();
    this.renderer.redraw();
    this.updateButtons();
    notify('All lines cleared', 'ok');
  }
}
