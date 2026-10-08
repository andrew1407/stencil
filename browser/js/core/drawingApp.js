import { notify } from '../utils.js';
import * as lineEditOps from './line/editOps.js';
import * as unitDisplay from '../ui/panel/unitDisplay.js';
import * as selectionPanel from '../ui/panel/selectionPanel.js';
import * as linesList from '../ui/panel/linesList.js';
import * as projectTitle from '../ui/projects/window/projectTitle.js';
import { updateButtons as updateControlState } from '../ui/control/state.js';
import * as drawToggleUI from '../ui/panel/drawToggleUI.js';
import * as openFlow from './launch/openFlow.js';
import { askChoose, askAlt, askPrompt } from './modalAsk.js';
import { wireCollaborators } from './remote/collaborators.js';
import { onRemoteProjectsChange } from './remote/projectsWatch.js';
import { CoordTable } from '../ui/panel/coordTable.js';
import { AccentController } from '../ui/accent/controller.js';
import { wireControls } from '../ui/bindings/index.js';
import { createEditorState } from './editorState.js';
import { DEFAULT_ACCENT, isAccent } from './settings/accents.js';
import { readOpenProjectId, buildExternalLaunchUrl } from './launch/deepLink.js';
import * as launch from './launch/controller.js';
import { wireExtensionBridge } from './launch/extensionBridge.js';
import { leaveThenRemove } from '../ui/motion.js';
import { wireViewportSync } from './zoom/viewportSync.js';
import { OPEN_IN_DEFAULTS, loadOpenInConfig } from '../config/openInConfig.js';
import { EVENTS } from '../eventBus/appBus.js';
import { installGestureFlags } from './pointer/gesture.js';
import { installDelegates, installMethods } from './app/delegates.js';
import { EditingMethods } from './app/editing.js';
import { canToggleIncognito, reportIncognitoSession } from './launch/incognitoFlow.js';
import { CHANGE, changed } from './app/changes.js';
import constants from '../../../common/config/constants.json' with { type: 'json' };

const { DEBOUNCE } = constants;

// The view seam: the ui/ functions the core, llm and console layers reach through the app, since
// only this file may import ui/. A ui/ caller imports them itself.
const VIEW_SEAM = Object.freeze({
  hideSelectionPanels: () => selectionPanel.hideSelectionPanels(),
  applyLinesListHover: linesList.applyLinesListHover,
  showSelectionPanel: selectionPanel.showSelectionPanel,
  syncDrawModeUI: drawToggleUI.syncDrawModeUI,
  updateCoordStatus: unitDisplay.updateCoordStatus,
  applyUnitToUI: unitDisplay.applyUnitToUI,
  updateButtons: updateControlState,
  updateProjectTitle: projectTitle.updateProjectTitle,
  updateInfo: projectTitle.updateInfo,
});

// DrawingApp: the mediator owning state + DOM wiring. What it does without the DOM is
// app/editing.js; its view seam is VIEW_SEAM above; the rest is here.
export class DrawingApp {
  draggingPoint = null;
  dragJustEnded = false;
  draggingSegment = null;
  draggingLine = null;
  continueLineIdx = -1;
  continueInsertIdx = -1;
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

    this.#wireCollaborators();
    this.initEventListeners();
    wireViewportSync(this);
// Boot synchronously into a blank temporary editor; the projects component decides
// whether to offer a chooser after readiness.
    this.storage.restore();
    this.storage.newTemporary();
// "?open=<id>" is read before any component wires, so the chooser stays closed;
// applyProjectDeepLink() loads it once everything is wired.
    this.pendingOpenProjectId = readOpenProjectId(location.search);
// Likewise a `#stencil=` hand-off keeps the chooser closed; read before applyExternalLaunch() strips it.
    this.hasExternalLaunch = (location.hash || '').startsWith('#stencil=');
// A .stc handed over with it; applyExternalLaunch fills this, index.js opens it in the Script
// window once the image lands — a link never runs a script.
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

// The DOM wiring lives in js/ui/bindings/, in one fixed order.
  initEventListeners() {
    wireControls(this);
    this.pointer.wirePanDrag();
    this.input.wireHoldDraw();
    this.input.wireTouch();
    this.#wireExternalResume();
// The extension's "editor mode" (js/core/launch/extensionBridge.js).
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

  get accent() {
    const a = document.documentElement.getAttribute('data-accent');
    return isAccent(a) ? a : DEFAULT_ACCENT;
  }

// The inline --accent override string, or null when a named preset is active.
  get customAccent() {
    return document.documentElement.style.getPropertyValue('--accent').trim() || null;
  }

  adjustThicknessAtCursor(e) {
    return lineEditOps.adjustThicknessAtCursor(this, e, () => {
      clearTimeout(this.#thicknessSaveTimer);
      this.#thicknessSaveTimer = setTimeout(() => this.saveHistory(), DEBOUNCE.editCommitMs);
    });
  }

// Strip the "?open=<id>" so a reload doesn't re-trigger; called once after every component is wired.
  applyProjectDeepLink() {
    const id = this.pendingOpenProjectId;
    if (id == null) return false;
    history.replaceState(null, '', location.pathname + location.hash);
    return this.projectTransfer.switchToProject(id);
  }

  #modalHost() { return document.getElementById('confirm-modal-overlay'); }

// Falls back to native confirm only without the <stencil-confirm-modal> (pre-wire, non-DOM tests).
// opts: { title, confirmLabel, cancelLabel, danger }.
  confirm(message, opts = {}) {
    const el = this.#modalHost();
    if (el && typeof el.ask === 'function') return el.ask(message, opts);
    return Promise.resolve(typeof window !== 'undefined' && window.confirm ? window.confirm(message) : true);
  }

  choose(message, opts = {}) { return askChoose(this.#modalHost(), message, opts); }
  askAlt(message, opts = {}) { return askAlt(this.#modalHost(), message, opts); }
  prompt(message, opts = {}) { return askPrompt(this.#modalHost(), message, opts); }

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

// The incognito toggle, the body's incognito class and the info line's tag.
  updateIncognitoUI() {
    const btn = document.getElementById('incognito-toggle');
    if (btn) {
      btn.disabled = !canToggleIncognito(this);
      btn.classList.toggle('active', this.storage.incognito);
    }
    document.body.classList.toggle('incognito-mode', this.storage.incognito);
    this.updateInfo();
    reportIncognitoSession(this);
  }

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
    changed(this, CHANGE.lines, CHANGE.selection);
    notify('All lines cleared', 'ok');
  }
}
installDelegates(DrawingApp.prototype, VIEW_SEAM);
installMethods(DrawingApp.prototype, EditingMethods);
installGestureFlags(DrawingApp.prototype);
