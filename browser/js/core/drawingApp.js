import { notify, compareEditedShows } from '../utils.js';
import * as hitTest from './draw/hitTest.js';
import * as dragGestures from './touch/dragGestures.js';
import * as shapeBuilder from './line/shapeBuilder.js';
import * as canvasClickRouter from './pointer/canvasClick.js';
import * as lineSelection from './line/lineSelection.js';
import * as transformOps from './draw/transformOps.js';
import * as drawMode from './draw/drawMode.js';
import * as lineEditOps from './line/lineEditOps.js';
import * as pageMetrics from './parse/pageMetrics.js';
import * as unitDisplay from '../ui/unitDisplay.js';
import * as blankImage from './image/blankImage.js';
import * as projectFileIO from './project/projectFileIO.js';
import * as hoverController from './pointer/hoverController.js';
import * as selectionPanel from '../ui/selectionPanel.js';
import * as linesList from '../ui/linesList.js';
import * as projectTitle from '../ui/projectTitle.js';
import { updateButtons as updateControlState } from '../ui/control/controlState.js';
import * as drawToggleUI from '../ui/drawToggleUI.js';
import * as openFlow from './launch/openFlow.js';
import * as incognito from './launch/incognitoFlow.js';
import { askChoose, askAlt, askPrompt } from './modalAsk.js';
import { wireCollaborators } from './remote/collaborators.js';
import { canvasCoords, nearCompareDivider } from './pointer/canvasCoords.js';
import { openInLaunchPayload } from './launch/launchPayload.js';
import { onRemoteProjectsChange } from './remote/remoteProjectsWatch.js';
import { currentLayoutPayload } from './project/meta/projectMeta.js';
import { CoordTable } from '../ui/coordTable.js';
import { AccentController } from '../ui/accent/accentController.js';
import { wireControls } from '../ui/bindings/index.js';
import { createEditorState } from './editorState.js';
import { DEFAULT_ACCENT, isAccent } from './settings/accents.js';
import { readOpenProjectId, buildExternalLaunchUrl } from './launch/deepLink.js';
import * as launch from './launch/launchController.js';
import * as imageLoadFlow from './image/imageLoadFlow.js';
import { wireExtensionBridge } from './launch/extensionBridge.js';
import { leaveThenRemove } from '../ui/motion.js';
import { wireViewportSync } from './zoom/viewportSync.js';
import { OPEN_IN_DEFAULTS, loadOpenInConfig } from '../config/openInConfig.js';
import { EVENTS } from '../eventBus/appBus.js';

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
// A .stc handed over with it; applyExternalLaunch fills this, index.js runs it once the image lands.
    this.pendingLaunchScript = '';
// Seeded with defaults so the toolbar button gates correctly before the async config load.
    this.openInConfig = { ...OPEN_IN_DEFAULTS };
    loadOpenInConfig().then(cfg => { this.openInConfig = cfg; this.updateButtons(); });
    this.updateButtons();
    this.applyUnitToUI();
  }

// The view-layer pair is injected, so collaborators.js stays inside the core layer.
  #wireCollaborators() {
    wireCollaborators(this, {
      CoordTable, AccentController,
      onProjectsChanged: (detail) => onRemoteProjectsChange(this, detail),
    });
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

  nearCompareDivider(clientX, clientY) { return nearCompareDivider(this, clientX, clientY); }

  canvasCoords(clientX, clientY) { return canvasCoords(this, clientX, clientY); }

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

  setListHoverLine(idx) { lineSelection.setListHoverLine(this, idx); }

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

  deselectEmptyArea(e) { lineSelection.deselectEmptyArea(this, e); }

  deselectLine(redraw = true) {
    this.selectedLineIdx = -1;
    this.selectedLines = [];
    this.updateMultiSelectStatus();
    this.coordLineIdx = -1;
    this.hoveredPtIdx = -1;
    this.focusedPtIdx = -1;
    this.hideSelectionPanels();
    if (redraw) this.renderer.redraw();
  }

  applySelectionChange(prop, value) { lineSelection.applySelectionChange(this, prop, value); }

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

  adjustThicknessAtCursor(e) {
    return lineEditOps.adjustThicknessAtCursor(this, e, () => {
      clearTimeout(this.#thicknessSaveTimer);
      this.#thicknessSaveTimer = setTimeout(() => { this.saveHistory(); this.storage.save(); }, 280);
    });
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

  #modalHost() { return document.getElementById('confirm-modal-overlay'); }

// Falls back to native confirm only without the <stencil-confirm-modal> (pre-wire, non-DOM tests).
// opts: { title, confirmLabel, cancelLabel, danger }.
  confirm(message, opts = {}) {
    const el = this.#modalHost();
    if (el && typeof el.ask === 'function') return el.ask(message, opts);
    return Promise.resolve(typeof window !== 'undefined' && window.confirm ? window.confirm(message) : true);
  }

// Drives the beforeunload leave-guard in index.js. Kept synchronous: beforeunload can't
// await the confirm() modal, so the browser's native prompt is used.
  hasEditingSession() {
    return !!this.image || (typeof this.history?.canUndo === 'function' && this.history.canUndo());
  }

  choose(message, opts = {}) { return askChoose(this.#modalHost(), message, opts); }

  askAlt(message, opts = {}) { return askAlt(this.#modalHost(), message, opts); }

  prompt(message, opts = {}) { return askPrompt(this.#modalHost(), message, opts); }

  newEditor(opts = {}) { openFlow.newEditor(this, opts); }

  adoptIncognitoHere() { incognito.adoptIncognitoHere(this); }

  openImageHere(file, isIncognito = false, address = null, opts = {}) {
    openFlow.openImageHere(this, file, isIncognito, address, opts);
  }

// Launch `file` in a NEW tab via the #stencil= fragment (applyExternalLaunch honors
// `incognito` and `crop`).
  openImageNewTab(file, isIncognito = false, opts = {}) {
    if (!file) return;
    const reader = new FileReader();
    reader.onload = () => {
      const base = location.origin + location.pathname;
      const payload = openFlow.applyOpenOpts(
        { dataUrl: reader.result, name: file.name, incognito: !!isIncognito }, opts);
      const url = buildExternalLaunchUrl(base, payload);
      window.open(url, '_blank');
    };
    reader.onerror = () => notify('Could not read the image', 'fail');
    reader.readAsDataURL(file);
  }

  replaceProjectImage(file, opts = {}) { openFlow.replaceProjectImage(this, file, opts); }

  createBlankImage(opts = {}) { return blankImage.createBlankImage(this, opts); }

  activeIsBlank() { return blankImage.activeIsBlank(this); }

  setBlankColor(color) { return blankImage.setBlankColor(this, color); }

  createRemoteBlank(address) { return openFlow.createRemoteBlank(this, address); }

// The #stencil= fragment shape: a server reference for a linked session, else the inline
// image + full layout. `id` hands off a SAVED project instead; null when nothing is stored.
  openInLaunchPayload(opts = {}) { return openInLaunchPayload(this, opts); }

// Shared by the server push and ExportService's download/copy, so it is public.
  currentLayoutPayload() { return currentLayoutPayload(this); }

  projectFileState(opts = {}) { return projectFileIO.projectFileState(this, opts); }

  applyProjectFile(project) { return projectFileIO.applyProjectFile(this, project); }

  applyProjectFileInPlace(project, opts = {}) { projectFileIO.applyProjectFileInPlace(this, project, opts); }

  chooseFileConflict(name = '.stencil') { return projectFileIO.chooseFileConflict(this, name); }

  updateStencilSyncUI() { projectFileIO.updateStencilSyncUI(this); }

  publishIncognitoToServer(address) { return incognito.publishIncognitoToServer(this, address); }

  promoteIncognitoToLocal() { return incognito.promoteIncognitoToLocal(this); }

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

  canToggleIncognito() { return incognito.canToggleIncognito(this); }

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

  reportIncognitoSession() { incognito.reportIncognitoSession(this); }

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
