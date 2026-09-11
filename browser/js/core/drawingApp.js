import { setVal, notify, compareEditedShows } from '../utils.js';
import * as hitTest from './hitTest.js';
import * as dragGestures from './dragGestures.js';
import * as shapeBuilder from './shapeBuilder.js';
import * as canvasClickRouter from './canvasClick.js';
import * as lineSelection from './lineSelection.js';
import * as transformOps from './transformOps.js';
import * as drawMode from './drawMode.js';
import * as lineEditOps from './lineEditOps.js';
import * as pageMetrics from './pageMetrics.js';
import * as unitDisplay from '../ui/unitDisplay.js';
import * as blankImage from './blankImage.js';
import * as projectFileIO from './projectFileIO.js';
import * as hoverController from './hoverController.js';
import * as selectionPanel from '../ui/selectionPanel.js';
import * as linesList from '../ui/linesList.js';
import * as projectTitle from '../ui/projectTitle.js';
import { updateButtons as updateControlState } from '../ui/controlState.js';
import * as drawToggleUI from '../ui/drawToggleUI.js';
import { HistoryStack } from './historyStack.js';
import { FormulaEngine } from './formulaEngine.js';
import { Renderer } from './renderer.js';
import { StrokeFx } from './strokeFx.js';
import { Storage } from './storage.js';
import { getProjectsBackend } from './projectsBackend.js';
import { TabsCoordinator } from './tabsCoordinator.js';
import { PROJECT_ACTION } from '../worker/messages.js';
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
import { wireControls } from '../ui/bindings/index.js';
import { createEditorState } from './editorState.js';
import { DEFAULT_ACCENT, isAccent } from './accents.js';
import { buildLayoutPayload } from './layout.js';
import { readOpenProjectId, buildExternalLaunchUrl } from './deepLink.js';
import * as launch from './launchController.js';
import * as imageLoadFlow from './imageLoadFlow.js';
import { StencilSync } from './stencilSync.js';
import { wireExtensionBridge } from './extensionBridge.js';
import { leaveThenRemove } from '../ui/motion.js';
import { requireConnection, createRemoteProject, saveRemoteProject } from '../net/remoteSync.js';
import { getSyncToServer } from '../net/connectionStore.js';
import { wireViewportSync } from './viewportSync.js';
import { OPEN_IN_DEFAULTS, loadOpenInConfig } from '../config/openInConfig.js';
import { publish, EVENTS } from '../bus/appBus.js';

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

    // Every plain-value field (see editorState.js) — collaborators and DOM below.
    Object.assign(this, createEditorState());
    this.#rectConnectOnce = false;   // one-shot: connect the next rect to the selection

    this.#wireCollaborators();
    this.initEventListeners();
    wireViewportSync(this);
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

  // Construct every collaborator, in dependency order — each takes the app and reaches
  // back through it. Called once, from the constructor, after the plain state exists.
  #wireCollaborators() {
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
      publish(EVENTS.accentChanged, next);
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
  }

  // Whether the current session can be handed to another app. Desktop needs a configured
  // URL scheme (opens any project — server ref or inline). Telegram needs a bot username
  // AND a server project (a 64-char t.me start payload can't carry image bytes). Drives
  // both the toolbar #open-in-btn visibility and the modal's per-button hiding.
  openInDesktopAvailable() { return !!this.openInConfig?.desktopScheme; }
  openInTelegramAvailable() { return !!this.openInConfig?.telegramBotUsername && !!this.remoteLink; }
  openInAvailable() { return this.openInDesktopAvailable() || this.openInTelegramAvailable(); }

  // The DOM wiring lives in js/ui/bindings/ — every listener the toolbars, keyboard and
  // canvas need, in one fixed order. Only the non-DOM collaborators are wired here.
  initEventListeners() {
    wireControls(this);
    this.pointer.wirePanDrag();
    this.input.wireHoldDraw();
    this.input.wireTouch();
    this.#wireExternalResume();
    // The extension's "editor mode" asks this tab about itself (project/image/preview) and
    // imports into it without opening a new tab — see js/core/extensionBridge.js.
    wireExtensionBridge(this);
  }

  // The extension's editorBridge dispatches switchToSource for "resume in the open editor
  // tab": switch to the matching project here, no reload. Ignored while incognito.
  #wireExternalResume() {
    window.addEventListener(EVENTS.switchToSource, (e) => {
      if (this.storage.incognito) return;
      const { source = '', name = '' } = e?.detail || {};
      if (!source && !name) return;
      launch.resumeBySource(this, source, name);
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

  // The rect is measured ONCE PER FRAME and reused: this runs 2-3x per mouse-move and again
  // per pointermove of a drag, and getBoundingClientRect forces a layout every time — with a
  // style write in between (the hover cursor) that was a read/write/read thrash. The cache
  // lives one animation frame — and only while the canvas's inline size is unchanged, so a
  // zoom/fit drops it at once. Where there is no rAF (node) every call measures, as before.
  canvasCoords(clientX, clientY) {
    let rect = this.canvasRect;
    const sized = this.canvas.style?.width;   // a zoom/fit rewrites it — a string read, no layout
    if (rect && this.canvasRectAt !== sized) rect = null;
    if (!rect) {
      rect = this.canvas.getBoundingClientRect();
      if (typeof requestAnimationFrame === 'function') {
        this.canvasRect = rect;
        this.canvasRectAt = sized;
        requestAnimationFrame(() => { this.canvasRect = null; });
      }
    }
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
  // The whole load flow lives in imageLoadFlow.js / imageSettle.js.
  loadImageFromFile(file, opts = {}) { return imageLoadFlow.loadImageFromFile(this, file, opts); }

  // ── External launch (extension / desktop / bot): `#stencil=<encodeURIComponent(JSON)>` ──
  // The whole launch tail (fragment parse, resume-by-source, server launch, page apply)
  // lives in launchController.js; these keep the names index.js + the bridge + tests call.
  applyExternalLaunch() { return launch.applyExternalLaunch(this); }

  importExternalImage(payload, { mode = 'new' } = {}) { return launch.importExternalImage(this, payload, { mode }); }

  openRemoteProject(meta) { return this.projectTransfer.openRemoteProject(meta); }

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

  // Start/stop drawing and the Line↔Rect mode live in drawMode.js; the two button
  // faces they drive are ui/drawToggleUI.js.
  startDrawingMode(opts = {}) { drawMode.startDrawingMode(this, opts); }

  syncDrawToggleUI() { drawToggleUI.syncDrawToggleUI(this); }

  setDrawMode(mode) { drawMode.setDrawMode(this, mode); }

  syncDrawModeUI() { drawToggleUI.syncDrawModeUI(this); }

  stopDrawingMode() { drawMode.stopDrawingMode(this); }

  // The selection set (single + multi) lives in lineSelection.js.
  selectedIndices() { return lineSelection.selectedIndices(this); }

  isLineSelected(i) { return lineSelection.isLineSelected(this, i); }

  updateMultiSelectStatus() { lineSelection.updateMultiSelectStatus(this); }

  selectLineFromList(idx, ctrlShift = false) { return lineSelection.selectLineFromList(this, idx, ctrlShift); }

  // The Lines-tab list is painted by ui/linesList.js; these keep the names its two
  // canvas-side callers (the hover coalescer, the redraw hooks) already use.
  applyLinesListHover() { linesList.applyLinesListHover(this); }

  renderLinesList() { linesList.renderLinesList(this); }

  // Hovering a Lines-list row glows that line on the canvas (the reverse direction of
  // applyLinesListHover). -1 / out-of-range clears the glow.
  setListHoverLine(idx) {
    const i = (typeof idx === 'number' && idx >= 0 && idx < this.lines.length) ? idx : -1;
    if (i === this.listHoverLineIdx) return;
    this.listHoverLineIdx = i;
    this.renderer.redraw();
  }

  // The click router lives in canvasClick.js.
  canvasClick(e) { canvasClickRouter.canvasClick(this, e); }

  // Shape building (close-into-area, insert / connect a point, rect) lives in
  // shapeBuilder.js; these keep the names the pointer + touch paths call.
  tryCloseShapeAt(x, y) { return shapeBuilder.tryCloseShapeAt(this, x, y); }

  insertPointOnSegment(lineIdx, insertIdx, x, y) { shapeBuilder.insertPointOnSegment(this, lineIdx, insertIdx, x, y); }

  createRect(x1, y1, x2, y2, connect = false) { shapeBuilder.createRect(this, x1, y1, x2, y2, connect); }


  // Canvas hover (cursor, hover ring, coordinate tooltip, Lines-row tint) and the
  // double-click delete live in hoverController.js; the bindings call these names.
  canvasMouseMove(e) { hoverController.canvasMouseMove(this, e); }

  canvasDblClick(e) { hoverController.canvasDblClick(this, e); }

  // Hit-testing lives in hitTest.js (pure functions); these delegators supply the model and
  // the zoom-aware default thresholds (screen-px radius divided by the zoom, so hits stay
  // constant on screen).
  findLineAt(x, y, threshold = 8 / (this.scale || 1)) {
    return hitTest.findLineAt(this.lines, x, y, threshold);
  }

  showSelectionPanel(line) { selectionPanel.showSelectionPanel(this, line); }

  applyFill() { selectionPanel.applyFill(this); }

  syncFsSelectionPanel(line) { selectionPanel.syncFsSelectionPanel(this, line); }

  // A click on the letterbox OUTSIDE the image: drops the selection like blank space inside
  // it, but canvasClick() is bound to the <canvas> and needs image coordinates. Guards mirror
  // it — no-op while drawing, in compare view, right after a drag, or with a modifier held.
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

  // Page metrics (wasm-parity) live in pageMetrics.js; the two unit views in ui/unitDisplay.js.
  getPageDimensions() { return pageMetrics.getPageDimensions(this); }

  pixelToPageCoords(x, y) { return pageMetrics.pixelToPageCoords(this, x, y); }

  updateCoordStatus(x, y) { unitDisplay.updateCoordStatus(this, x, y); }

  applyUnitToUI() { unitDisplay.applyUnitToUI(this); }

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

  // Rotate / flip / nudge over the selection live in transformOps.js.
  rotateSelectedLine(angle) { transformOps.rotateSelectedLine(this, angle); }

  flipSelectedLine(horizontal) { transformOps.flipSelectedLine(this, horizontal); }

  rotateSelectedLineQuarter(dir) { transformOps.rotateSelectedLineQuarter(this, dir); }

  nudgeSelected(dx, dy) { return transformOps.nudgeSelected(this, dx, dy); }

  saveHistory() {
    this.history.push(this.lines);
    this.storage.saveSoon();    // trailing-edge: a stroke persists once, when it stops
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

  // The topbar name field, colour chip, remote badge and the image-info line are painted
  // by ui/projectTitle.js; these keep the names every state change already calls.
  updateProjectTitle(force = false) { projectTitle.updateProjectTitle(this, force); }

  updateInfo() { projectTitle.updateInfo(this); }

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
    this.reportIncognitoSession();   // newTemporary clears incognito → drop our peer entry
  }

  // Turn THIS editor into a fresh incognito session, like openImageHere's incognito branch:
  // flush the outgoing project, reset, switch incognito on. Used by the assistant's §10
  // `openUrl`, which deliberately keeps the conversation (`keepChat`).
  adoptIncognitoHere() {
    if (!this.storage.incognito) this.storage.save();
    this.newEditor({ keepChat: true });
    this.storage.incognito = true;
    this.updateIncognitoUI();
  }

  // Open-image dialog action: replace the current editor with `file` — flush the outgoing
  // project, reset, load. `address` (a connected server URL) creates+links it there, but
  // incognito wins over a server target (publish explicitly via publishIncognitoToServer).
  // `opts.crop` overrides the default page-aspect crop with the dialog's own rect.
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

  // Replace the CURRENT project's image in place (same id / server link). `rename` adopts the
  // new file's name; `keepAnnotations` keeps the lines over it; `crop` is a rect in the NEW
  // image's pixels overriding the page-aspect auto-crop. The stale pin drops in loadImageFromFile.
  replaceProjectImage(file, { rename = false, keepAnnotations = true, crop = null } = {}) {
    if (!file) return;
    this.loadImageFromFile(file, { replaceInPlace: true, rename, keepAnnotations, ...(crop ? { crop } : {}) });
  }

  // Blank projects (a solid-colour raster you can recolour) live in blankImage.js.
  createBlankImage(opts = {}) { return blankImage.createBlankImage(this, opts); }

  activeIsBlank() { return blankImage.activeIsBlank(this); }

  setBlankColor(color) { return blankImage.setBlankColor(this, color); }

  // ── Server-backed sessions ───────────────────────────────────────
  // Arm `address` as the create target for the NEXT image load, rather than creating a
  // project now: the server forbids image-less projects and there is no image yet at
  // newEditor time, so the upcoming blank()/open creates it WITH real bytes (via
  // imageSettle's createRemoteForSession) and links the session. Backs newEditor({ address }).
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
  // Gather / apply / conflict-prompt and the live-sync button all live in projectFileIO.js;
  // these keep the names ExportService + StencilSync + controlState call.
  projectFileState(opts = {}) { return projectFileIO.projectFileState(this, opts); }

  applyProjectFile(project) { return projectFileIO.applyProjectFile(this, project); }

  applyProjectFileInPlace(project, opts = {}) { projectFileIO.applyProjectFileInPlace(this, project, opts); }

  chooseFileConflict(name = '.stencil') { return projectFileIO.chooseFileConflict(this, name); }

  updateStencilSyncUI() { projectFileIO.updateStencilSyncUI(this); }

  // Ask the browser extension (if installed) to UNPIN an image — fired on any in-project image
  // change, since the project no longer holds that image. Posts a same-window message the editor
  // bridge content script relays to the extension; a harmless no-op when no extension is present.
  // `resource` (the page the image was pinned on) is what keys the extension's pin entry.


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
  // publishIncognitoToServer. Incognito only promises the app writes nothing BY ITSELF, so an
  // explicit "save this" is honoured. Returns the project id, or null with nothing to keep.
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
  // Top-menu settings live in SettingsController: toolbar handlers AND the console API call
  // app.settings.<setter>() directly, so they stay in sync. Its formula UI helpers are public
  // because the formula binding and remoteSync.adoptServerFormulas drive them too.

  // ── Point / line mutation (shared with the coord table + console) — lineEditOps.js ──
  setPointCoord(lineIdx, ptIdx, axis, valuePx) { return lineEditOps.setPointCoord(this, lineIdx, ptIdx, axis, valuePx); }

  removePoint(lineIdx, ptIdx) { return lineEditOps.removePoint(this, lineIdx, ptIdx); }

  removeLine(idx) { return lineEditOps.removeLine(this, idx); }

  removeSelectedLines() { return lineEditOps.removeSelectedLines(this); }

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
    this.reportIncognitoSession();   // keep other tabs' "incognito tabs" list current
  }

  // Broadcast this tab's incognito session (or null) to peers, for the projects modal's
  // "Incognito tabs" filter. Best-effort — tabs may not have a coordinator.
  reportIncognitoSession() {
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
