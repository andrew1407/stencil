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
import { CoordTable } from '../ui/coordTable.js';
import { ZoomPan } from './zoomPan.js';
import { ExportService } from './exportService.js';
import { SettingsController } from './settingsController.js';
import { AccentController } from '../ui/accentController.js';
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

// DrawingApp: the orchestrator owning state + DOM wiring; the pure decision helpers live
// in ./layout.js so they unit-test without a DOM.
export class DrawingApp {
  draggingPoint = null;
  dragJustEnded = false;
  draggingSegment = null;
  draggingLine = null;
  continueLineIdx = -1;
  continueInsertIdx = -1;
  #rectConnectOnce = false;
// continueLineIdx/InsertIdx and the drag helpers are public: InputController and the mouse
// pan/drag path both use them. `nameEditor` is the topbar name editor ({ refresh }).
  nameEditor = null;
// The topbar name is in inline-edit mode (input unlocked, ✓/✗ shown).
  nameEditing = false;
  #thicknessSaveTimer = null;
// THIS user changed the filter since the last sync: a save imposes our filter, while a
// lines-only save preserves the shared server filter instead of clobbering a peer's.
  filterDirty = false;

  constructor() {
    this.canvas = document.getElementById('canvas');
    this.ctx = this.canvas.getContext('2d');
    this.tooltip = document.getElementById('tooltip');
    this.coordinatesBody = document.getElementById('coordinates-body');

    Object.assign(this, createEditorState());
    this.#rectConnectOnce = false;

    this.#wireCollaborators();
    this.initEventListeners();
    wireViewportSync(this);
// Boot synchronously into a blank temporary editor; the projects component decides
// whether to offer a chooser after readiness.
    this.restoreFromLocalStorage();
    this.storage.newTemporary();
// "?open=<id>" is read before any component wires, so the chooser stays closed;
// applyProjectDeepLink() loads it once everything is wired.
    this.pendingOpenProjectId = readOpenProjectId(location.search);
// Likewise a `#stencil=` hand-off keeps the chooser closed; read before applyExternalLaunch() strips it.
    this.hasExternalLaunch = (location.hash || '').startsWith('#stencil=');
// Seeded with defaults so the toolbar button gates correctly before the async config load.
    this.openInConfig = { ...OPEN_IN_DEFAULTS };
    loadOpenInConfig().then(cfg => { this.openInConfig = cfg; this.updateButtons(); });
    this.updateButtons();
    this.applyUnitToUI();
  }

// Every collaborator, in dependency order; each takes the app and reaches back through it.
  #wireCollaborators() {
    this.history = new HistoryStack();
    this.formula = new FormulaEngine();
    this.renderer = new Renderer(this);
// Vertices in flight: every route that adds a point hands it here (strokeFx.js).
    this.strokeFx = new StrokeFx(this);
    this.storage = new Storage(this);
    this.tabs = new TabsCoordinator();
    this.tabs.onProjectsChanged(detail => this.#onRemoteProjectsChange(detail || {}));
// A peer's accent change repaints live (no re-broadcast); a local custom accent wins.
    this.tabs.onAccent(key => {
      if (this.customAccent) return;
      const next = this.accents.applyAccent(key);
      publish(EVENTS.accentChanged, next);
    });
    this.coordTable = new CoordTable(this);
    this.export = new ExportService(this);
    this.settings = new SettingsController(this);
    this.accents = new AccentController(this);
    this.imageModel = new ImageModel(this);
    this.remoteSync = new RemoteSyncController(this);
// `host` is the narrow slice of app state/callbacks the controller needs; `getConnections`
// is a getter because stencilApi creates the manager lazily.
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
// Opt-in live sync with a .stencil on disk (File System Access / Chromium only).
    this.stencilSync = new StencilSync(this);
    this.input = new InputController(this);
    this.pointer = new PointerController(this);
// <stencil-tooltip> owns its render logic; aliased as tooltipMgr for existing callers.
    this.tooltip.app = this;
    this.tooltipMgr = this.tooltip;
    this.zoomPan = new ZoomPan(this);
  }

// Desktop needs a configured URL scheme; Telegram needs a bot username AND a server
// project (a 64-char t.me start payload can't carry image bytes).
  openInDesktopAvailable() { return !!this.openInConfig?.desktopScheme; }
  openInTelegramAvailable() { return !!this.openInConfig?.telegramBotUsername && !!this.remoteLink; }
  openInAvailable() { return this.openInDesktopAvailable() || this.openInTelegramAvailable(); }

// The DOM wiring lives in js/ui/bindings/, in one fixed order.
  initEventListeners() {
    wireControls(this);
    this.pointer.wirePanDrag();
    this.input.wireHoldDraw();
    this.input.wireTouch();
    this.#wireExternalResume();
// The extension's "editor mode" (js/core/extensionBridge.js).
    wireExtensionBridge(this);
  }

// The extension's editorBridge dispatches switchToSource: switch here, no reload. Ignored while incognito.
  #wireExternalResume() {
    window.addEventListener(EVENTS.switchToSource, (e) => {
      if (this.storage.incognito) return;
      const { source = '', name = '' } = e?.detail || {};
      if (!source && !name) return;
      launch.resumeBySource(this, source, name);
    });
  }






  get theme() { return document.documentElement.getAttribute('data-theme') === 'dark' ? 'dark' : 'light'; }
// Theme/accent writes live in AccentController; the getters stay here (they read the document element).
  setTheme(theme, originEl = null) { this.accents.setTheme(theme, originEl); }
  setAccent(key, originEl = null) { this.accents.setAccent(key, originEl); }
  setCustomAccent(hex, originEl = null) { return this.accents.setCustomAccent(hex, originEl); }
  previewAccent(key, originEl = null) { this.accents.previewAccent(key, originEl); }
  endAccentPreview(originEl = null) { this.accents.endAccentPreview(originEl); }

  get accent() {
    const a = document.documentElement.getAttribute('data-accent');
    return isAccent(a) ? a : DEFAULT_ACCENT;
  }

// The inline --accent override string, or null when a named preset is active.
  get customAccent() {
    return document.documentElement.style.getPropertyValue('--accent').trim() || null;
  }








// A comparison view (original / split, or the Alt+Shift+O peek) is read-only: hover,
// selection and every editing gesture are suppressed; only pan/zoom and the divider drag stay live.
  compareReadOnly() {
    return this.renderer.effectiveCompareMode() !== 'none';
  }

// The tooltip is DISPLAY, so it stays live in a comparison for points you can SEE — gated
// on the POINT's coordinates, never the cursor's.
  compareShowsPoint(x, y) {
    return compareEditedShows(this.renderer.effectiveCompareMode(), this.compareSplit,
      x, y, this.canvas.width, this.canvas.height);
  }

// Grab band of 8 CSS px around the split divider; shared by PointerController and the hover cursor.
  nearCompareDivider(clientX, clientY) {
    if (this.compareMode !== 'vertical' && this.compareMode !== 'horizontal') return false;
    const { cssX, cssY } = this.canvasCoords(clientX, clientY);
    const scale = this.scale || 1;
    const f = Math.min(1, Math.max(0, this.compareSplit ?? 0.5));
    return this.compareMode === 'vertical'
      ? Math.abs(cssX - this.canvas.width * f * scale) <= 8
      : Math.abs(cssY - this.canvas.height * f * scale) <= 8;
  }

// Measured ONCE PER FRAME: this runs 2-3x per mouse-move and getBoundingClientRect forces
// a layout each time (read/write/read thrash). No rAF ⇒ every call.
  canvasCoords(clientX, clientY) {
    let rect = this.canvasRect;
    const sized = this.canvas.style?.width;
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
// Through the LIVE on-screen size, not this.scale: mid zoom-transition the two disagree.
    const sx = (rect.width > 0 && this.canvas.width > 0) ? rect.width / this.canvas.width : this.scale;
    const sy = (rect.height > 0 && this.canvas.height > 0) ? rect.height / this.canvas.height : this.scale;
    return { cssX, cssY, x: cssX / sx, y: cssY / sy };
  }

// opts.crop overrides the default page-aspect crop; opts.source/resource are provenance URLs.
  loadImageFromFile(file, opts = {}) { return imageLoadFlow.loadImageFromFile(this, file, opts); }

// External launch (`#stencil=<encodeURIComponent(JSON)>`) lives in launchController.js.
  applyExternalLaunch() { return launch.applyExternalLaunch(this); }

  importExternalImage(payload, { mode = 'new' } = {}) { return launch.importExternalImage(this, payload, { mode }); }

  openRemoteProject(meta) { return this.projectTransfer.openRemoteProject(meta); }


// Public: ImageModel, controlsBinder and fullscreenLayer use them too.
  hideSelectionPanels() { selectionPanel.hideSelectionPanels(); }

// `opts.from` is the drop point of a drag-and-drop.
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

  startDrawingMode(opts = {}) { drawMode.startDrawingMode(this, opts); }

  syncDrawToggleUI() { drawToggleUI.syncDrawToggleUI(this); }

  setDrawMode(mode) { drawMode.setDrawMode(this, mode); }

  syncDrawModeUI() { drawToggleUI.syncDrawModeUI(this); }

  stopDrawingMode() { drawMode.stopDrawingMode(this); }

  selectedIndices() { return lineSelection.selectedIndices(this); }

  isLineSelected(i) { return lineSelection.isLineSelected(this, i); }

  updateMultiSelectStatus() { lineSelection.updateMultiSelectStatus(this); }

  selectLineFromList(idx, ctrlShift = false) { return lineSelection.selectLineFromList(this, idx, ctrlShift); }

  applyLinesListHover() { linesList.applyLinesListHover(this); }

  renderLinesList() { linesList.renderLinesList(this); }

// The reverse of applyLinesListHover; -1 / out-of-range clears the glow.
  setListHoverLine(idx) {
    const i = (typeof idx === 'number' && idx >= 0 && idx < this.lines.length) ? idx : -1;
    if (i === this.listHoverLineIdx) return;
    this.listHoverLineIdx = i;
    this.renderer.redraw();
  }

  canvasClick(e) { canvasClickRouter.canvasClick(this, e); }

  tryCloseShapeAt(x, y) { return shapeBuilder.tryCloseShapeAt(this, x, y); }

  insertPointOnSegment(lineIdx, insertIdx, x, y) { shapeBuilder.insertPointOnSegment(this, lineIdx, insertIdx, x, y); }

  createRect(x1, y1, x2, y2, connect = false) { shapeBuilder.createRect(this, x1, y1, x2, y2, connect); }


  canvasMouseMove(e) { hoverController.canvasMouseMove(this, e); }

  canvasDblClick(e) { hoverController.canvasDblClick(this, e); }

// Default thresholds are a screen-px radius divided by the zoom, so hits stay constant on screen.
  findLineAt(x, y, threshold = 8 / (this.scale || 1)) {
    return hitTest.findLineAt(this.lines, x, y, threshold);
  }

  showSelectionPanel(line) { selectionPanel.showSelectionPanel(this, line); }

  applyFill() { selectionPanel.applyFill(this); }

  syncFsSelectionPanel(line) { selectionPanel.syncFsSelectionPanel(this, line); }

// A click on the letterbox OUTSIDE the image drops the selection; canvasClick() is bound
// to the <canvas> and needs image coordinates. Guards mirror it.
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
    if (this.compareReadOnly()) return;
    if (this.selectedLineIdx === -1) return;
    const line = this.lines[this.selectedLineIdx];
// A line still on the inherit fallback ('' pointColor) pins its rendered colour first.
    if (prop === 'color' && !line.pointColor) line.pointColor = line.color;
    line[prop] = value;
    this.saveHistory();
    this.renderer.redraw();
  }

  findNearestPoint(x, y, threshold = 10 / (this.scale || 1)) {
    return hitTest.findNearestPoint(this.lines, this.currentLine, x, y, threshold);
  }

  beginSegmentDrag(nearSeg, x, y) { dragGestures.beginSegmentDrag(this, nearSeg, x, y); }

// Alt+Ctrl/⌘+drag: pull a NEW point out of the line under the cursor; a closed area breaks
// open at that spot (dragGestures.pullOutPoint). Returns whether a drag began.
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

// The ✂ in the selection panel.
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

// Alt+wheel: ±1, clamped 1–20.
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

  rotateSelectedLine(angle) { transformOps.rotateSelectedLine(this, angle); }

  flipSelectedLine(horizontal) { transformOps.flipSelectedLine(this, horizontal); }

  rotateSelectedLineQuarter(dir) { transformOps.rotateSelectedLineQuarter(this, dir); }

  nudgeSelected(dx, dy) { return transformOps.nudgeSelected(this, dx, dy); }

  saveHistory() {
    this.history.push(this.lines);
    this.storage.saveSoon();    // trailing-edge: a stroke persists once, when it stops
    this.remoteSync.scheduleRemoteSync();
  }


  undo() {
    if (this.compareReadOnly()) return;
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
    if (this.compareReadOnly()) return;
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
      this.strokeFx.cancel();
      this.lines = result;
      this.hoverPt = null;
      this.hoverLineIdx = -1;
      this.listHoverLineIdx = -1;
      this.renderer.redraw();
      this.updateButtons();
      if (this.lines.length > 0) this.coordTable.update(this.lines[this.lines.length - 1].points);
    }
  }

  updateButtons() { updateControlState(this); }

  updateProjectTitle(force = false) { projectTitle.updateProjectTitle(this, force); }

  updateInfo() { projectTitle.updateInfo(this); }

// `color` is a status token every call site picks; it selects the toast kind.
  showSaveStatus(msg, color, _iconName = null) {
    const kind = String(color).includes('danger') ? 'fail'
               : String(color).includes('success') ? 'ok' : 'info';
// `key` makes the newer message replace the older toast (a blank-image create saves twice).
    notify(msg, kind, { key: 'save-status' });
  }

  restoreFromLocalStorage() {
    this.storage.restore();
  }

  switchToProject(id) { return this.projectTransfer.switchToProject(id); }

  openProjectInNewTab(id, win = null) { this.projectTransfer.openProjectInNewTab(id, win); }

  openRemoteProjectInNewTab(meta, win = null) { this.projectTransfer.openRemoteProjectInNewTab(meta, win); }

// Strip the "?open=<id>" so a reload doesn't re-trigger; called once after every component is wired.
  applyProjectDeepLink() {
    const id = this.pendingOpenProjectId;
    if (id == null) return false;
    history.replaceState(null, '', location.pathname + location.hash);
    return this.switchToProject(id);
  }

// Falls back to native confirm only without the <stencil-confirm-modal> (pre-wire, non-DOM tests).
// opts: { title, confirmLabel, cancelLabel, danger }.
  confirm(message, opts = {}) {
    const el = document.getElementById('confirm-modal-overlay');
    if (el && typeof el.ask === 'function') return el.ask(message, opts);
    return Promise.resolve(typeof window !== 'undefined' && window.confirm ? window.confirm(message) : true);
  }

// Drives the beforeunload leave-guard in index.js. Kept synchronous: beforeunload can't
// await the confirm() modal, so the browser's native prompt is used.
  hasEditingSession() {
    return !!this.image || (typeof this.history?.canUndo === 'function' && this.history.canUndo());
  }

// Resolves the chosen option value, or null on cancel. opts.options: [{ value, label }].
  choose(message, opts = {}) {
    const el = document.getElementById('confirm-modal-overlay');
    if (el && typeof el.choose === 'function') return el.choose(message, opts);
// No modal (tests / pre-wire): the first option, if any.
    const first = (opts.options || [])[0];
    return Promise.resolve(first ? first.value : null);
  }

// Cancel | alt | confirm → 'confirm', 'alt', or null. opts: { title, confirmLabel, altLabel }.
  askAlt(message, opts = {}) {
    const el = document.getElementById('confirm-modal-overlay');
    if (el && typeof el.askAlt === 'function') return el.askAlt(message, opts);
    return Promise.resolve(null);
  }

// Resolves the trimmed string, or null on cancel. opts: { title, confirmLabel, defaultValue }.
  prompt(message, opts = {}) {
    const el = document.getElementById('confirm-modal-overlay');
    if (el && typeof el.prompt === 'function') return el.prompt(message, opts);
    return Promise.resolve(opts.defaultValue ?? null);
  }

// `keepChat` forwards to newTemporary (the assistant's incognito adoption keeps its turn).
  newEditor({ keepChat = false } = {}) {
    this.remoteLink = null;
    this.pendingRemoteAddress = null;
    this.blankColor = '';
    this.fromFile = false;
    this.stencilSync?.unlink();
    this.storage.newTemporary({ keepChat });
// Dropping the image can move the viewport's top (the toolbar reflows) — re-measure.
    this.zoomPan.syncViewportHeight();
    this.tabs.reportActive(null);
    this.reportIncognitoSession();
  }

// Like openImageHere's incognito branch, but keeps the conversation (the assistant's §10 `openUrl`).
  adoptIncognitoHere() {
    if (!this.storage.incognito) this.storage.save();
    this.newEditor({ keepChat: true });
    this.storage.incognito = true;
    this.updateIncognitoUI();
  }

// `address` creates+links the project there, but incognito wins over a server target
// (publish explicitly via publishIncognitoToServer). `opts.crop` overrides the auto-crop.
  openImageHere(file, incognito = false, address = null, opts = {}) {
    if (!file) return;
    const toServer = !!address && !incognito;
    if (toServer) requireConnection(this.connections, address);
    if (!this.storage.incognito) this.storage.save();
    this.newEditor();
    if (incognito) { this.storage.incognito = true; this.updateIncognitoUI(); }
    this.loadImageFromFile(file, this.#applyOpenOpts(toServer ? { address } : {}, opts));
  }

// An explicit `crop` wins; else `noCrop` imports the whole frame; provenance rides along.
// Returns the mutated target.
  #applyOpenOpts(target, opts) {
    if (opts.crop) target.crop = opts.crop;
    else if (opts.noCrop) target.noCrop = true;
    if (opts.source) target.source = opts.source;
    if (opts.resource) target.resource = opts.resource;
    if (opts.landing) target.landing = true;
    if (opts.from) target.from = opts.from;
    return target;
  }

// Launch `file` in a NEW tab via the #stencil= fragment (applyExternalLaunch honors
// `incognito` and `crop`).
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

// Same id / server link. `crop` is a rect in the NEW image's pixels; the stale pin drops
// in loadImageFromFile.
  replaceProjectImage(file, { rename = false, keepAnnotations = true, crop = null } = {}) {
    if (!file) return;
    this.loadImageFromFile(file, { replaceInPlace: true, rename, keepAnnotations, ...(crop ? { crop } : {}) });
  }

  createBlankImage(opts = {}) { return blankImage.createBlankImage(this, opts); }

  activeIsBlank() { return blankImage.activeIsBlank(this); }

  setBlankColor(color) { return blankImage.setBlankColor(this, color); }

// Arm `address` as the create target for the NEXT image load: the server forbids
// image-less projects, so the upcoming blank()/open creates it WITH real bytes
// (imageSettle's createRemoteForSession). Backs newEditor({ address }).
  async createRemoteBlank(address) {
    const conn = requireConnection(this.connections, address);
    this.pendingRemoteAddress = conn.url;
    return { address: conn.url };
  }

// The #stencil= fragment shape: a server reference for a linked session, else the inline
// image + full layout. `id` hands off a SAVED project instead; null when nothing is stored.
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

// The saved-project half of openInLaunchPayload.
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

// A server reference (the receiver re-fetches — no bytes, no token), else the inline image + full layout.
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

// Shared by the server push and ExportService's download/copy, so it is public.
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

  projectFileState(opts = {}) { return projectFileIO.projectFileState(this, opts); }

  applyProjectFile(project) { return projectFileIO.applyProjectFile(this, project); }

  applyProjectFileInPlace(project, opts = {}) { projectFileIO.applyProjectFileInPlace(this, project, opts); }

  chooseFileConflict(name = '.stencil') { return projectFileIO.chooseFileConflict(this, name); }

  updateStencilSyncUI() { projectFileIO.updateStencilSyncUI(this); }


// Create the project on the server, push layout + result, then LINK the session so it becomes a
// normal server-backed project (incognito off, still no local record). Returns the remote link.
  async publishIncognitoToServer(address) {
    const conn = requireConnection(this.connections, address);
    if (!this.image || !this.imageDataUrl) throw new Error('Open an image first');
// Raw bytes for the codec-free server.
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
// Explicit publish, independent of the sync toggle.
    this.remoteLink = await saveRemoteProject(conn, link, {
      name,
      layout: this.currentLayoutPayload(),
      bytes: await this.remoteSync.renderResultBytes(), ext: 'png', w: this.canvas.width, h: this.canvas.height,
    });
    this.storage.incognito = false;
    this.updateButtons();
    notify(`Published to ${conn.url}`, 'ok');
    return this.remoteLink;
  }

// The local twin of publishIncognitoToServer: incognito only promises the app writes
// nothing BY ITSELF, so an explicit "save this" is honoured. Null with nothing to keep.
  promoteIncognitoToLocal() {
    if (!this.image) return null;
    this.storage.incognito = false;
    this.storage.promoteTemporaryToProject();
    this.storage.save();
    this.tabs.reportActive(this.activeProjectId);
    this.updateButtons();
    notify('Left incognito — saved as a local project', 'ok');
    return this.activeProjectId;
  }

  clearAllProjects() { this.projectTransfer.clearAllProjects(); }


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

  moveProjectToServer(id, address) { return this.projectTransfer.moveProjectToServer(id, address); }

  copyProjectToServer(id, address, opts = {}) { return this.projectTransfer.copyProjectToServer(id, address, opts); }

  moveProjectToLocal(meta) { return this.projectTransfer.moveProjectToLocal(meta); }

  copyServerProjectToLocal(meta, opts = {}) { return this.projectTransfer.copyServerProjectToLocal(meta, opts); }

  copyServerProjectToIncognito(meta, opts = {}) { return this.projectTransfer.copyServerProjectToIncognito(meta, opts); }

// Incognito can be toggled only while the editor is blank, since adding content auto-saves.
  canToggleIncognito() {
    return this.storage.temporary && this.activeProjectId == null &&
      !this.image && this.lines.length === 0;
  }

// Safe to swap content under the user.
  #isIdle() {
    return !this.isDrawing && !this.isPanning && !this.isDraggingPoint &&
      !this.isDraggingSegment && !this.isDraggingLine &&
      !this.isZoomRectDragging && !this.isRectDrawDragging;
  }

// Called from updateButtons(), so it tracks every state change.
  updateIncognitoUI() {
    const btn = document.getElementById('incognito-toggle');
    if (btn) {
      btn.disabled = !this.canToggleIncognito();
      btn.classList.toggle('active', this.storage.incognito);
    }
    document.body.classList.toggle('incognito-mode', this.storage.incognito);
    this.updateInfo();
    this.reportIncognitoSession();
  }

// For the projects modal's "Incognito tabs" filter. Best-effort.
  reportIncognitoSession() {
    const session = this.storage.incognito
      ? { name: this.imageBaseName || 'Incognito (unsaved)', updatedAt: Date.now() }
      : null;
    try { this.tabs.reportIncognito(session); } catch { /* no coordinator */ }
  }

// Another tab changed the project set.
  #onRemoteProjectsChange(detail) {
    const { id, action } = detail;
// Payloads live in a per-tab IndexedDB mirror (projectsBackend.js): pull the changed one in
// now so the sync below reads the peer's bytes, not this tab's stale copy.
    const refreshed = Promise.resolve(id != null ? getProjectsBackend()?.refresh?.(id) : null);
// Full teardown, not storage.newTemporary(): a bare reset leaves remoteLink pointing at a
// project that no longer exists.
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
// A colour change lives in the registry meta, not the payload syncActiveFromStorage reloads.
        this.updateProjectTitle();
      });
    }
  }

// The composited result (filtered image + lines), i.e. what download/copy/share/save emit;
// project thumbnails use it so previews show the EDITED result.
  renderResultCanvas() { return this.export.renderExportCanvas(); }


  async clearAllLines() {
    if (this.compareReadOnly()) return;
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
