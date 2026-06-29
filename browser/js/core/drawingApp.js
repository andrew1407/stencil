import { setVal, setRadioGroup, notify, distToSegment, matchHotkey, isTypingTarget, cmToUnit, unitToCm, unitLabel, defaultUnitFromLocale, wireNameEditor, composeControlTitle, supportsShareFiles } from '../utils.js';
import constants from '../config/constants.json' with { type: 'json' };
import HOTKEY_DEFS from '../config/hotkeysConfig.json' with { type: 'json' };
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
import { core } from './stencilCore.js';
import { hotkeys } from './hotkeys.js';
import { ACCENT_STORAGE_KEY, DEFAULT_ACCENT, isAccent, applyAccentFavicon, applyFaviconHex, normalizeHex } from './accents.js';
import { buildLayoutPayload, validateLayout, resolveInsertIdx, fillState, mergeLines } from './layout.js';
import { cropAspect, centeredCrop, cropChange, isAlbumOrientation, scaleLinePoints, rotateCropRectQuarter, rotateLinePointsQuarter } from './cropGeometry.js';
import { readOpenProjectId, buildOpenProjectUrl, buildExternalLaunchUrl } from './deepLink.js';
import { normalizePageSize } from './units.js';
import { HoldDrawController, holdDrawTarget } from './holdDraw.js';
import { classifyEnd, midpoint, touchDist, TOUCH_DEFAULTS } from './touchGestures.js';
import { icon } from '../ui/icons.js';
import { enhanceSelect } from '../ui/customSelect.js';
import { requireConnection, createRemoteProject, saveRemoteProject, shouldReloadFromEvent } from '../net/remoteSync.js';
import { getSyncToServer } from '../net/connectionStore.js';

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
  // Pan state (Alt+drag)
  #panLastX = 0;
  #panLastY = 0;
  // Point/segment/line drag state
  #draggingPoint = null;
  #dragJustEnded = false;
  #draggingSegment = null;
  #draggingLine = null;
  // Continuation drawing
  #continueLineIdx = -1;
  #continueInsertIdx = -1;
  #rectConnectOnce = false;
  // Hold-to-draw gesture (alternative drawing flow; see ./holdDraw.js)
  #holdDraw = null;
  #holdTickTimer = null;
  // Touchscreen input layer (see ./touchGestures.js + #wireTouch). #touch holds
  // the live single/two-finger gesture state; null between gestures.
  #touch = null;
  #longPressTimer = null;
  #holdAutoEnabled = false;
  // True while a hold stroke extends a line BACKWARD from its first point: new
  // points are prepended (inserted at index 0) so the line grows from its start.
  #holdPrepend = false;
  // Topbar project-name editor controller ({ refresh }), wired in #wireToolbarButtons.
  #nameEditor = null;
  // True while the topbar name is in inline-edit mode (input unlocked, ✓/✗ shown).
  #nameEditing = false;
  // Arrow-key panning
  #arrowsHeld = new Set();
  #arrowPanRaf = null;
  // Smooth zoom animation state
  #smoothZoom = { target: null, focal: null, rafId: null };
  // Debounce timers
  #thicknessSaveTimer = null;
  #saveStatusTimer = null;
  #rotateSaveTimer = null;
  // Live co-edit: debounced server push on edit; timestamp of our last server save (to
  // ignore the server's echo of our own change); guard while a reload is applying.
  #remoteSyncTimer = null;
  #lastRemoteSaveAt = 0;
  #reloadingRemote = false;
  #remoteSyncPending = false;   // a push deferred during a reload, flushed when it settles
  // True when THIS user changed the filter since the last sync — so a save imposes our
  // filter (our intent wins), but a save that's only line edits preserves the shared
  // server filter instead of clobbering a peer's filter change.
  #filterDirty = false;

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
    this.lines = [];
    this.currentLine = null;
    this.isDrawing = false;
    this.scale = 1;

    // Pan state (Alt+drag) — delta-based, with optional Shift speed-up
    this.isPanning = false;
    this.#panLastX = 0;
    this.#panLastY = 0;

    // Point drag state (Alt+hover+drag on point)
    this.#draggingPoint = null; // { lineIdx, ptIdx, point }
    this.isDraggingPoint = false;
    this.#dragJustEnded = false;

    // Segment drag state (Alt+drag on a line segment between two points)
    this.isDraggingSegment = false;
    this.#draggingSegment = null; // { lineIdx, ptIdx1, ptIdx2, startX, startY, origPt1, origPt2 }

    // Whole-line drag state (Alt+Shift+drag on any part of a line)
    this.isDraggingLine = false;
    this.#draggingLine = null; // { lineIdx, startX, startY, origPoints }

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
    this.#continueLineIdx = -1;
    this.#continueInsertIdx = -1;

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
      const next = this.#applyAccent(key);
      try { window.dispatchEvent(new CustomEvent('stencil:accent-changed', { detail: next })); } catch { /* no DOM — best-effort UI nudge */ }
    });
    this.coordTable = new CoordTable(this);
    // The tooltip is a custom element (<stencil-tooltip>) that owns its render
    // logic; give it the app ref and alias it as tooltipMgr for existing callers.
    this.tooltip.app = this;
    this.tooltipMgr = this.tooltip;
    this.zoomPan = new ZoomPan(this);

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
    // Reflect the initial (imageless) state: undo/redo + fullscreen start disabled.
    this.updateButtons();
    this.applyUnitToUI();
  }

  // Slim orchestrator: wire each cohesive control group in source order so
  // document-level listener dispatch order stays identical to before the split.
  initEventListeners() {
    this.#wireStyleControls();
    this.#wireSelectionPanelControls();
    this.#wirePageAndDisplayControls();
    this.#wireFormulaControls();
    this.#wireToolbarButtons();
    this.#wireZoomControls();
    this.#wireScrollPersist();
    this.#wireTheme();
    this.#wireKeyboard();
    this.#wireArrowPan();
    this.#wireDropPaste();
    this.#wireCanvasPointer();
    this.#wireSmoothZoom();
    this.#wirePanDrag();
    this.#wireHoldDraw();
    this.#wireTouch();
  }

  #wireStyleControls() {
    document.getElementById('image-upload').addEventListener('change', e => this.loadImage(e));
    // Compact "Load Image" affordance (shown when no image) just triggers the hidden picker.
    document.getElementById('load-image-btn')?.addEventListener('click',
      () => document.getElementById('image-upload').click());
    // Image actions (shown when an image is loaded). #open-image-btn is wired by the
    // open-image modal component (as its open button); the rest are direct actions.
    document.getElementById('copy-image')?.addEventListener('click', () => this.copyImageToClipboard());
    const shareBtn = document.getElementById('share-image');
    if (shareBtn) {
      if (supportsShareFiles()) shareBtn.style.display = '';
      shareBtn.addEventListener('click', () => this.shareImage());
    }
    document.getElementById('rotate-left').addEventListener('click', () => this.rotateImage(-1));
    document.getElementById('rotate-right').addEventListener('click', () => this.rotateImage(1));
    document.getElementById('line-color').addEventListener('change', e => this.setColor(e.target.value));
    // Live drag (input) previews without persisting; the trailing change commits.
    document.getElementById('line-thickness').addEventListener('input', e => this.setThickness(e.target.value, { persist: false }));
    document.getElementById('line-thickness').addEventListener('change', e => this.setThickness(e.target.value));
    document.getElementById('marker-size').addEventListener('input', e => this.setMarkerSize(e.target.value, { persist: false }));
    document.getElementById('marker-size').addEventListener('change', e => this.setMarkerSize(e.target.value));
    document.getElementById('line-style').addEventListener('change', e => this.setLineStyle(e.target.value));
    document.getElementById('image-filter').addEventListener('change', e => this.setImageFilter(e.target.value));
    let filterColorTimer = null;
    document.getElementById('filter-color').addEventListener('input', e => {
      // Reflect the model + mirror immediately; debounce the redraw/persist commit.
      this.filterColor = e.target.value;
      const ctxTint = document.getElementById('ctx-tint-color');
      if (ctxTint) ctxTint.value = e.target.value;
      clearTimeout(filterColorTimer);
      filterColorTimer = setTimeout(() => this.setFilterColor(e.target.value), 80);
    });
  }

  // Selection panel listeners
  #wireSelectionPanelControls() {
    document.getElementById('sel-color').addEventListener('input', e => this.applySelectionChange('color', e.target.value));
    document.getElementById('sel-thickness').addEventListener('change', e => this.applySelectionChange('thickness', parseInt(e.target.value)));
    document.getElementById('sel-marker-size').addEventListener('change', e => this.applySelectionChange('marker-size', parseInt(e.target.value)));
    document.getElementById('sel-style').addEventListener('change', e => this.applySelectionChange('style', e.target.value));
    document.getElementById('sel-fill-enabled').addEventListener('change', () => this.applyFill());
    document.getElementById('sel-fill').addEventListener('input', () => {
      document.getElementById('sel-fill-enabled').checked = true;
      this.applyFill();
    });
    document.getElementById('sel-fill-clear').addEventListener('click', () => {
      document.getElementById('sel-fill-enabled').checked = false;
      this.applyFill();
      notify('Fill cleared (transparent)', 'ok');
    });
    document.getElementById('sel-deselect').addEventListener('click', () => this.deselectLine());
  }

  #wirePageAndDisplayControls() {
    document.getElementById('page-size').addEventListener('change', e => this.setPageSize(e.target.value));
    document.getElementById('custom-page-width').addEventListener('change', e => {
      // Inputs are typed in the active unit; the setter stores cm.
      const v = parseFloat(e.target.value);
      this.setCustomPageWidth(Number.isNaN(v) ? 21 : unitToCm(v, this.unit));
    });
    document.getElementById('custom-page-height').addEventListener('change', e => {
      const v = parseFloat(e.target.value);
      this.setCustomPageHeight(Number.isNaN(v) ? 29.7 : unitToCm(v, this.unit));
    });
    const unitSel = document.getElementById('unit-select');
    if (unitSel) unitSel.addEventListener('change', e => this.setUnit(e.target.value));
    // Swap the native popups (whose position macOS controls) for custom dropdowns
    // anchored below the control. The native <select>s stay as the state source, so
    // the change listeners above and every setVal('page-size'|'unit-select') keep working.
    enhanceSelect(document.getElementById('page-size'));
    enhanceSelect(unitSel);
    document.getElementById('show-points').addEventListener('change', e => this.setShowPoints(e.target.checked));
    document.getElementById('show-lines').addEventListener('change', e => this.setShowLines(e.target.checked));
  }

  // ── Formula controls (top bar) ──────────────────────────────
  // #syncFormulaUI / #showFormulaError / #refreshFormulaCoords + setAllowFormulas live
  // with the other setters. This validates BOTH inputs together (no half-typed pair) and
  // shows the inline error; the console's setFormula() throws instead.
  #wireFormulaControls() {
    const validateAndApplyFormulas = () => {
      const fxVal = document.getElementById('formula-x').value.trim();
      const fyVal = document.getElementById('formula-y').value.trim();
      const okX = this.formula.validate(fxVal, 'x');
      const okY = this.formula.validate(fyVal, 'y');
      this.#showFormulaError(!okX || !okY);
      if (okX && okY) {
        this.formulaX = fxVal;
        this.formulaY = fyVal;
        setVal('ctx-formula-x', fxVal);
        setVal('ctx-formula-y', fyVal);
        this.#refreshFormulaCoords();
        this.storage.save();
        this.scheduleRemoteSync();   // push the formula change to peers/server
      }
    };
    document.getElementById('allow-formulas').addEventListener('change', e => this.setAllowFormulas(e.target.checked));
    document.getElementById('formula-x').addEventListener('input', validateAndApplyFormulas);
    document.getElementById('formula-y').addEventListener('input', validateAndApplyFormulas);
  }

  #wireToolbarButtons() {
    // Topbar project-name field: read-only title, renames inline on demand. Double-click
    // (or hover ✎) to edit; ✓/✗ show ONLY while editing. ✓ enabled for a changed, valid
    // (non-empty, unique) name; ✓/Enter commit, ✗/Escape/click-away revert. Incognito / no
    // project never expose ✎ or ✓/✗.
    const nameInput = document.getElementById('project-name-input');
    const nameEdit = document.getElementById('project-name-edit');
    const nameAccept = document.getElementById('project-name-accept');
    const nameCancel = document.getElementById('project-name-cancel');
    if (nameInput && nameAccept && nameCancel) {
      const currentName = () => (this.activeProjectId != null ? (this.storage.store.getMeta(this.activeProjectId)?.name || '') : '');
      // Only a saved (non-incognito) project can be renamed.
      const canRename = () => this.activeProjectId != null && !this.storage.incognito;
      const endEdit = () => {
        this.#nameEditing = false;
        nameInput.readOnly = true;
        nameAccept.style.display = 'none';
        nameCancel.style.display = 'none';
        this.updateProjectTitle(true);   // restore value + ✎ visibility
      };
      const beginEdit = () => {
        if (!canRename() || this.#nameEditing) return;
        this.#nameEditing = true;
        nameInput.readOnly = false;
        if (nameEdit) nameEdit.style.display = 'none';
        nameAccept.style.display = '';
        nameCancel.style.display = '';
        this.#nameEditor?.refresh();     // set ✓ enabled/disabled for the starting value
        nameInput.focus();
        nameInput.select();
      };
      this.#nameEditor = wireNameEditor(nameInput, nameAccept, nameCancel, {
        alwaysShow: true,                // edit-mode controls ✓/✗ visibility, not change-detection
        current: currentName,
        validate: (v) => this.storage.store.validateName(v, this.activeProjectId),
        commit: (v) => {
          if (this.activeProjectId != null) this.renameProject(this.activeProjectId, v);   // syncs imageBaseName itself
          endEdit();
        },
        cancel: () => endEdit(),
      });
      nameInput.addEventListener('dblclick', () => beginEdit());
      if (nameEdit) nameEdit.addEventListener('click', () => beginEdit());
      // A real click-away (the ✓/✗ buttons prevent their own mousedown, so they don't
      // blur) discards the in-progress rename.
      nameInput.addEventListener('blur', () => { if (this.#nameEditing) endEdit(); });
    }
    document.getElementById('start-drawing').addEventListener('click', () => this.startDrawingMode());
    document.getElementById('stop-drawing').addEventListener('click', () => this.stopDrawingMode());
    document.getElementById('draw-mode-toggle').addEventListener('click', () => {
      this.setDrawMode(this.drawMode === 'rect' ? 'line' : 'rect');
      this.storage.save();
    });
    document.getElementById('undo').addEventListener('click', () => this.undo());
    document.getElementById('redo').addEventListener('click', () => this.redo());
    document.getElementById('download-json').addEventListener('click', () => this.downloadJSON());
    document.getElementById('copy-json-btn').addEventListener('click', () => this.copyLayoutToClipboard());
    document.getElementById('save-image').addEventListener('click', () => this.saveImage());
    document.getElementById('upload-json-btn').addEventListener('click', () => document.getElementById('upload-json').click());
    document.getElementById('upload-json').addEventListener('change', e => this.uploadJSON(e));
    document.getElementById('clear-storage').addEventListener('click', async () => {
      if (this.storage.temporary || this.activeProjectId == null) {
        // Temporary editor → just clear the editor back to blank.
        if (await this.confirm('Clear this editor (image + lines)?', { title: 'Clear editor', danger: true })) {
          this.storage.newTemporary();
          this.tabs.reportActive(null);
          this.showSaveStatus('Cleared', 'var(--danger)', 'trash');
        }
        return;
      }
      // A server-linked project only clears its LOCAL copy — the server keeps it.
      // Say so up front, and confirm the user really wants to drop the open project.
      const server = this.remoteLink?.address;
      const msg = server
        ? `Remove the local copy of this project? It is stored on the server ${server} and will stay there.`
        : 'Clear this project (image + lines) from storage?';
      if (await this.confirm(msg, { title: server ? 'Remove local copy' : 'Clear project', danger: true })) {
        const id = this.activeProjectId;
        this.storage.store.remove(id);
        this.storage.newTemporary();
        this.remoteLink = null;   // dropped the local session → no server link to save back to
        this.tabs.reportActive(null);
        this.tabs.projectsChanged({ id, action: PROJECT_ACTION.REMOVED });
        if (server) notify(`Local copy removed — still on the server ${server}`, 'info');
        this.showSaveStatus('Cleared', 'var(--danger)', 'trash');
      }
    });
    const incognitoBtn = document.getElementById('incognito-toggle');
    if (incognitoBtn) incognitoBtn.addEventListener('click', () => {
      if (!this.#canToggleIncognito()) return;
      this.storage.incognito = !this.storage.incognito;
      this.updateIncognitoUI();
      notify(this.storage.incognito
        ? 'Incognito mode — this editor won\'t be saved'
        : 'Incognito off', 'info');
    });
    document.getElementById('clear-all-lines').addEventListener('click', () => this.clearAllLines());
    // Zoom buttons: single click = small step, double-click = large step,
    // hold = continuous zoom (kicks in after a short delay)
    this.zoomPan.setupHoldZoom(document.getElementById('zoom-in'), +1);
    this.zoomPan.setupHoldZoom(document.getElementById('zoom-out'), -1);
    document.getElementById('zoom-fit').addEventListener('click', () => this.zoomPan.fitToWindow());
  }

  #wireZoomControls() {
    // Manual zoom input
    const zoomInput = document.getElementById('zoom-input');
    const applyZoomInput = () => {
      const val = parseFloat(zoomInput.value);
      if (!isNaN(val) && val >= 5 && val <= 500) {
        this.zoomPan.zoomAroundCenter(val / 100);
      } else {
        zoomInput.value = Math.round(this.scale * 100);
      }
    };
    zoomInput.addEventListener('change', applyZoomInput);
    zoomInput.addEventListener('keydown', e => {
      if (e.key === 'Enter') { e.preventDefault(); applyZoomInput(); zoomInput.blur(); }
      if (e.key === 'Escape') { zoomInput.value = Math.round(this.scale * 100); zoomInput.blur(); }
    });
    // Prevent zoom input scroll from zooming the canvas
    zoomInput.addEventListener('wheel', e => e.stopPropagation());
  }

  #wireScrollPersist() {
    // Save scroll position (debounced) so it's restored on reopen
    {
      const scrollVp = document.getElementById('canvas-viewport');
      if (scrollVp) {
        let scrollSaveTimer = null;
        scrollVp.addEventListener('scroll', () => {
          clearTimeout(scrollSaveTimer);
          scrollSaveTimer = setTimeout(() => this.storage.save(), 400);
        });
      }
    }
  }

  // Active UI theme ('dark' | 'light').
  get theme() { return document.documentElement.getAttribute('data-theme') === 'dark' ? 'dark' : 'light'; }
  // Set (and persist as the manual override) the UI theme; refreshes the toggle icon.
  setTheme(theme) {
    const next = String(theme).toLowerCase() === 'dark' ? 'dark' : 'light';
    document.documentElement.setAttribute('data-theme', next);
    localStorage.setItem('drawingApp_theme', next);
    this.#updateThemeIcon();
  }
  #updateThemeIcon() {
    const btn = document.getElementById('theme-toggle');
    if (btn) btn.innerHTML = this.theme === 'dark' ? icon('sun') : icon('moon');
  }

  // Active accent preset key (see js/core/accents.js); falls back to violet.
  get accent() {
    const a = document.documentElement.getAttribute('data-accent');
    return isAccent(a) ? a : DEFAULT_ACCENT;
  }
  // Set (and persist) the accent preset; --accent-2 and the glows derive from it.
  // Broadcasts to peer tabs so they repaint live (see #applyAccent for the local apply).
  setAccent(key) {
    const next = this.#applyAccent(key);
    this.tabs.broadcastAccent(next);
  }
  // Paint + persist the accent in THIS tab. Returns the resolved key. Used by
  // setAccent (local change) and the cross-tab listener (remote change, no re-broadcast).
  #applyAccent(key) {
    const next = isAccent(key) ? key : DEFAULT_ACCENT;
    document.documentElement.style.removeProperty('--accent'); // drop any custom (temp) override
    document.documentElement.setAttribute('data-accent', next);
    try { localStorage.setItem(ACCENT_STORAGE_KEY, next); } catch { /* storage blocked — accent still applies this session, just won't persist */ }
    applyAccentFavicon(next);
    return next;
  }

  // A custom (non-preset) accent applied to THIS page only — the inline --accent override
  // string, or null when a named preset is active. Set via setCustomAccent.
  get customAccent() {
    return document.documentElement.style.getPropertyValue('--accent').trim() || null;
  }
  // Apply an arbitrary hex colour as the accent for this page only: NO persistence and NO
  // cross-tab broadcast (unlike setAccent), so it stays local and vanishes on reload. The
  // inline --accent overrides the data-accent preset rule; everything else derives from it.
  // Returns the normalized '#rrggbb', or null when `hex` isn't a valid colour.
  setCustomAccent(hex) {
    const norm = normalizeHex(hex);
    if (!norm) return null;
    document.documentElement.style.setProperty('--accent', norm);
    applyFaviconHex(norm);
    return norm;
  }

  #wireTheme() {
    this.#updateThemeIcon();
    // Tint the tab favicon + status bar to the saved accent on load.
    applyAccentFavicon(this.accent);
    document.getElementById('theme-toggle').addEventListener('click', () => {
      this.setTheme(this.theme === 'dark' ? 'light' : 'dark');
    });
    // Follow system changes only if user hasn't manually overridden
    window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', e => {
      if (!localStorage.getItem('drawingApp_theme')) {
        document.documentElement.setAttribute('data-theme', e.matches ? 'dark' : 'light');
        this.#updateThemeIcon();
      }
    });
  }

  #wireKeyboard() {
    // Keyboard shortcuts — dispatched via the hotkeys registry
    const HK_HANDLERS = {
      undo: () => { if (!document.getElementById('undo').disabled) this.undo(); },
      redo: () => { if (!document.getElementById('redo').disabled) this.redo(); },
      startDraw: () => { if (this.image && !this.isDrawing) this.startDrawingMode(); },
      stopDraw: () => { if (this.isDrawing) this.stopDrawingMode(); },
      togglePoints: () => {
        const cb = document.getElementById('show-points');
        cb.checked = !cb.checked;
        this.showPoints = cb.checked;
        this.renderer.redraw();
      },
      toggleLines: () => {
        const cb = document.getElementById('show-lines');
        cb.checked = !cb.checked;
        this.showLines = cb.checked;
        this.renderer.redraw();
      },
      cycleFilter: () => {
        const opts = ['none', 'bw', 'sepia', 'custom'];
        const cur = opts.indexOf(this.imageFilter);
        // Route through setImageFilter so the cycle marks the filter dirty + syncs to
        // the server (it used to set the value inline and never push).
        this.setImageFilter(opts[(cur + 1) % opts.length]);
      },
      resetZoom: () => this.zoomPan.fitToWindow(),
      toggleControls: () => { const b = document.getElementById('toggle-controls');   if (b) b.click(); },
      togglePointsList: () => { const b = document.getElementById('toggle-coord-panel'); if (b) b.click(); },
      fullscreen: () => this.toggleFullscreen?.(),
      zoomIn: () => this.zoomPan.zoomAroundCenter(this.scale + 0.25),
      zoomOut: () => this.zoomPan.zoomAroundCenter(this.scale - 0.25),
      zoomInBig: () => this.zoomPan.zoomAroundCenter(this.scale + 1.0),
      zoomOutBig: () => this.zoomPan.zoomAroundCenter(this.scale - 1.0),
      rotateImageLeft: () => { if (this.image) this.rotateImage(-1); },
      rotateImageRight: () => { if (this.image) this.rotateImage(1); },
      copyImage: () => this.copyImageToClipboard(),
      copyLayout: () => this.copyLayoutToClipboard(),
      // paste is handled by the native 'paste' event listener below — entry here is for hotkey display only
      paste: () => { /* handled by paste event */ },
      clearAllLines: () => this.clearAllLines(),
      // Delete the selected line; on Mac the default reads as ⌥⌫ (Delete→Backspace).
      deleteLine: () => { if (!this.isDrawing && this.selectedLineIdx >= 0) this.removeLine(this.selectedLineIdx); },
      // Delete the focused point of the selected line (the point, not the whole line).
      deletePoint: () => {
        if (this.isDrawing) return;
        if (this.coordLineIdx >= 0 && this.focusedPtIdx >= 0) this.removePoint(this.coordLineIdx, this.focusedPtIdx);
      }
    };
    document.addEventListener('keydown', e => {
      if (isTypingTarget(e.target)) return;
      for (const def of HOTKEY_DEFS) {
        const combo = hotkeys.get(def.id);
        if (!combo) continue;
        if (!matchHotkey(e, combo)) continue;
        // Skip 'paste' here — let the browser fire its native paste event
        if (def.id === 'paste') return;
        e.preventDefault();
        const fn = HK_HANDLERS[def.id];
        if (fn) fn();
        return;
      }

      // Alt + (=/+/−) ergonomic zoom shortcuts (not in the registry so they can't be remapped away)
      // Shift held → large step (1.0), otherwise small step (0.25)
      if (e.altKey && !e.ctrlKey && !e.metaKey) {
        const inc = e.code === 'Equal' || e.code === 'NumpadAdd';
        const dec = e.code === 'Minus' || e.code === 'NumpadSubtract';
        if (inc || dec) {
          e.preventDefault();
          const step = e.shiftKey ? 1.0 : 0.25;
          this.zoomPan.zoomAroundCenter(this.scale + (inc ? step : -step));
        }
      }
    });

    // Refresh tooltip & cursor the instant a modifier key is pressed/released
    // while hovering the canvas — so Shift (full points list) and Ctrl (live
    // cursor coordinates) tooltips update at once without re-hovering.
    const onModifierChange = e => {
      if (e.key !== 'Shift' && e.key !== 'Control' && e.key !== 'Alt' && e.key !== 'Meta') return;
      const mods = { ctrlKey: e.ctrlKey, shiftKey: e.shiftKey, altKey: e.altKey, metaKey: e.metaKey };
      this.tooltipMgr.refresh(mods);
      // Live-switch an active segment/line drag the instant Shift is pressed or
      // released, even if the mouse is held still.
      if ((this.isDraggingSegment && this.#draggingSegment) ||
          (this.isDraggingLine && this.#draggingLine)) {
        this.#dragMove(this.lastMouseClientX, this.lastMouseClientY, mods.shiftKey);
      }
      if (this.mouseOverCanvas && !this.isZoomRectDragging && !this.isPanning &&
        !this.isDraggingPoint && !this.isDraggingSegment && !this.isDraggingLine) {
        if (mods.altKey)                         this.canvas.style.cursor = 'grab';
        else if (mods.ctrlKey && !mods.shiftKey) this.canvas.style.cursor = 'copy';
        else if (mods.shiftKey)                  this.canvas.style.cursor = 'zoom-in';
        else                                     this.canvas.style.cursor = 'crosshair';
      }
    };
    document.addEventListener('keydown', onModifierChange);
    document.addEventListener('keyup', onModifierChange);
  }

  #wireArrowPan() {
    // ── Arrow-key panning ──────────────────────────────────────
    // Plain arrows pan the viewport; multiple arrows pan diagonally, opposing pairs
    // cancel; Shift accelerates. Alt/Ctrl/Meta are reserved (e.g. Alt+ArrowUp = zoom).
    this.#arrowsHeld = new Set();
    this.#arrowPanRaf = null;
    const ARROW_KEYS = ['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'];
    const arrowPanTick = () => {
      const vp = document.getElementById('canvas-viewport');
      if (!vp || this.#arrowsHeld.size === 0) { this.#arrowPanRaf = null; return; }
      const speed = this.#arrowsHeld.has('Shift') ? 22 : 7;
      let dx = 0;
      let dy = 0;
      if (this.#arrowsHeld.has('ArrowLeft'))  dx -= 1;
      if (this.#arrowsHeld.has('ArrowRight')) dx += 1;
      if (this.#arrowsHeld.has('ArrowUp'))    dy -= 1;
      if (this.#arrowsHeld.has('ArrowDown'))  dy += 1;
      if (dx) vp.scrollLeft += dx * speed;
      if (dy) vp.scrollTop  += dy * speed;
      this.#arrowPanRaf = requestAnimationFrame(arrowPanTick);
    };
    document.addEventListener('keydown', e => {
      if (isTypingTarget(e.target)) return;
      if (e.key === 'Shift') { this.#arrowsHeld.add('Shift'); return; }
      if (!ARROW_KEYS.includes(e.key)) return;
      // Don't steal Alt/Ctrl/Meta+Arrow combos used by other shortcuts
      if (e.ctrlKey || e.altKey || e.metaKey) return;
      e.preventDefault();
      this.#arrowsHeld.add(e.key);
      if (!this.#arrowPanRaf) this.#arrowPanRaf = requestAnimationFrame(arrowPanTick);
    });
    document.addEventListener('keyup', e => {
      if (e.key === 'Shift') this.#arrowsHeld.delete('Shift');
      if (ARROW_KEYS.includes(e.key)) this.#arrowsHeld.delete(e.key);
    });
    // Clear held keys if the window loses focus while arrows are held
    window.addEventListener('blur', () => { this.#arrowsHeld.clear(); });
  }

  #wireDropPaste() {
    // Document-wide drag-and-drop overlay
    const dropZone = document.getElementById('global-drop-overlay');

    document.addEventListener('dragenter', e => {
      e.preventDefault();
      if (e.dataTransfer.types.includes('Files')) dropZone.style.display = 'flex';
    });
    document.addEventListener('dragover', e => {
      e.preventDefault();
      e.dataTransfer.dropEffect = 'copy';
    });
    document.addEventListener('dragleave', e => {
      // Only hide when leaving the entire window
      if (e.relatedTarget === null) dropZone.style.display = 'none';
    });
    document.addEventListener('drop', e => {
      e.preventDefault();
      dropZone.style.display = 'none';
      const file = e.dataTransfer.files[0];
      if (!file) return;
      if (file.type.startsWith('image/')) {
        this.loadImageFromFile(file);
      } else if (file.name.endsWith('.json') || file.type === 'application/json') {
        this.loadJSONFromFile(file);
      } else {
        notify('Please drop an image or a .json file', 'fail');
      }
    });

    // Clipboard paste (Ctrl+V) — handles images and JSON layout text
    document.addEventListener('paste', async e => {
      if (isTypingTarget(e.target)) return; // let native paste work in inputs
      const cd = e.clipboardData;
      if (!cd) return;

      // 1) Image takes priority
      for (const item of cd.items) {
        if (item.type && item.type.startsWith('image/')) {
          e.preventDefault();
          // Read the file synchronously — clipboardData is invalid after an await.
          const file = item.getAsFile();
          if (this.image && !(await this.confirm('Replace current image with pasted image?', { title: 'Replace image' }))) {
            notify('Image paste canceled', 'fail');
            return;
          }
          if (file) {
            this.loadImageFromFile(file);
            notify('Image pasted from clipboard', 'ok');
          } else {
            notify('Could not read pasted image', 'fail');
          }
          return;
        }
      }

      // 2) Text — try to parse as layout JSON
      const text = cd.getData('text/plain');
      if (text) {
        let data = null;
        try {
          data = JSON.parse(text);
        } catch {
          /* not layout JSON — left as null; the guard below simply ignores the paste */
        }
        if (data && Array.isArray(data.lines)) {
          e.preventDefault();
          this.applyPastedLayout(data);
        }
      }
    });
  }

  #wireCanvasPointer() {
    this.canvas.addEventListener('click', e => this.canvasClick(e));
    this.canvas.addEventListener('dblclick', e => this.canvasDblClick(e));
    this.canvas.addEventListener('mousemove', e => this.canvasMouseMove(e));
    this.canvas.addEventListener('mouseleave', () => {
      this.mouseOverCanvas = false;
      this.tooltipMgr.hide();
      this.updateCoordStatus();
      if (this.hoverPt) { this.hoverPt = null; this.renderer.redraw(); }
    });
  }

  #wireSmoothZoom() {
    // ── Smooth zoom via rAF ──
    // Rapid wheel events accumulate into one rAF loop. IMPORTANT: add `zoom-no-transition`
    // while the rAF runs so the CSS width/height transition doesn't fight it (causes flicker).
    this.#smoothZoom = { target: null, focal: null, rafId: null };

    const viewport = document.getElementById('canvas-viewport');

    const runSmoothZoom = () => {
      const sz = this.#smoothZoom;
      const oldScale = this.scale;
      const diff = sz.target - oldScale;

      if (Math.abs(diff) < 0.0018) {
        // Snap to final target, re-enable CSS transition, persist
        this.canvas.classList.remove('zoom-no-transition');
        this.zoomPan.setZoom(sz.target, true);
        // Apply final focal-point scroll after snap
        if (sz.focal && viewport) {
          const { imgX, imgY, clientX, clientY } = sz.focal;
          viewport.scrollLeft = imgX * sz.target - clientX;
          viewport.scrollTop = imgY * sz.target - clientY;
        }
        sz.rafId = null;
        sz.focal = null;
        sz.target = null;
        return;
      }

      // Ease towards target (~0.25 per frame — smooth but responsive)
      const next = oldScale + diff * 0.25;
      // Set scale directly (CSS transition is OFF — no conflict)
      this.scale = next;
      this.canvas.style.width = (this.canvas.width  * next) + 'px';
      this.canvas.style.height = (this.canvas.height * next) + 'px';
      this.zoomPan.setZoomInputValue(Math.round(next * 100));

      // Maintain focal point: keep the image pixel under the cursor fixed.
      // scrollLeft = imgX * newScale - clientX_in_viewport
      if (sz.focal && viewport) {
        const { imgX, imgY, clientX, clientY } = sz.focal;
        viewport.scrollLeft = imgX * next - clientX;
        viewport.scrollTop = imgY * next - clientY;
      }

      sz.rafId = requestAnimationFrame(runSmoothZoom);
    };

    // Ctrl+wheel (also fired by touchpad pinch-to-zoom) OR Alt+wheel → zoom toward cursor.
    // The +/− buttons use zoomAroundCenter() instead, so center-zoom is still reachable.
    document.addEventListener('wheel', e => {
      if (!e.ctrlKey && !e.altKey && !e.metaKey) return;
      // No image → nothing to zoom/thicken/rotate; let plain scroll pass through.
      if (!this.image) return;

      // Alt+wheel → adjust thickness of the line under the cursor
      // (point's line if hovering a point, else the hovered line).
      if (e.altKey && !e.ctrlKey && !e.metaKey) {
        e.preventDefault();
        this.#adjustThicknessAtCursor(e);
        return;
      }

      // Ctrl+Shift+wheel with a selected line → rotate it (around its center,
      // or around the focused point if one is selected).
      if ((e.ctrlKey || e.metaKey) && e.shiftKey && this.selectedLineIdx >= 0
          && this.lines[this.selectedLineIdx]) {
        e.preventDefault();
        const dir = e.deltaY > 0 ? 1 : -1;
        this.#rotateSelectedLine(dir * (Math.PI / 60)); // 3° per tick
        return;
      }

      e.preventDefault();

      // Per-event zoom increment scaled by the wheel delta (not a fixed step):
      // a mouse "notch" sends a large delta and steps a sensible amount, while a
      // touchpad pinch sends many tiny deltas that now sum gently instead of each
      // leaping a full step (the "too rapid" touchpad zoom). deltaMode normalizes
      // line/page units to pixels; the cap keeps a big mouse notch from overshooting.
      const px = e.deltaY * (e.deltaMode === 1 ? 16 : e.deltaMode === 2 ? 400 : 1);
      const factor = e.shiftKey ? 0.0095 : 0.004;
      const cap = e.shiftKey ? 0.65 : 0.32;
      const delta = Math.max(-cap, Math.min(cap, -px * factor));
      const sz = this.#smoothZoom;

      // Accumulate target scale
      const base = sz.target !== null ? sz.target : this.scale;
      sz.target = Math.max(0.05, Math.min(5, base + delta));

      // Focal point in image-space (unscaled pixels).
      // Works in both fullscreen (viewport fixed at 0,0) and normal mode.
      const vpRect = viewport.getBoundingClientRect();
      const contentX = e.clientX - vpRect.left + viewport.scrollLeft;
      const contentY = e.clientY - vpRect.top  + viewport.scrollTop;
      sz.focal = {
        imgX: contentX / this.scale,
        imgY: contentY / this.scale,
        // cursor position relative to viewport left/top edge (viewport-local)
        clientX: e.clientX - vpRect.left,
        clientY: e.clientY - vpRect.top,
      };

      // Start animation loop; disable CSS transition first to prevent conflict
      if (!sz.rafId) {
        this.canvas.classList.add('zoom-no-transition');
        sz.rafId = requestAnimationFrame(runSmoothZoom);
      }
    }, { passive: false });
  }

  #wirePanDrag() {
    const viewport = document.getElementById('canvas-viewport');

    // Pan: Alt+left-drag OR middle-mouse-button drag (works in both drawing/non-drawing modes)
    const startPan = e => {
      // Rect-draw mode: plain left-drag sweeps out a rectangle area
      if (this.isDrawing && this.drawMode === 'rect' && e.button === 0 &&
        !e.altKey && !e.shiftKey && !e.ctrlKey && !e.metaKey && this.image) {
        const { cssX, cssY, x: imgX, y: imgY } = this.canvasCoords(e.clientX, e.clientY);
        this.isRectDrawDragging = true;
        this.rectDrawStart = { imgX, imgY, cssX, cssY };
        this.rectDrawEnd = { ...this.rectDrawStart };
        e.preventDefault();
        e.stopPropagation();
        return;
      }

      // Alt+Shift+left → drag whole line (takes priority over zoom-rect)
      if (e.button === 0 && e.altKey && e.shiftKey && this.image) {
        const { x, y } = this.canvasCoords(e.clientX, e.clientY);
        const lineIdx = this.findLineAt(x, y);
        if (lineIdx !== -1) {
          e.preventDefault();
          e.stopPropagation();
          const line = this.lines[lineIdx];
          // Record the grabbed segment too, so releasing Shift mid-drag can
          // drop down to moving just that segment (live modifier switching).
          const seg = this.#findNearestSegmentWithIdx(x, y);
          this.isDraggingLine = true;
          this.#draggingLine = {
            lineIdx,
            ptIdx1: seg && seg.lineIdx === lineIdx ? seg.ptIdx1 : null,
            ptIdx2: seg && seg.lineIdx === lineIdx ? seg.ptIdx2 : null,
            startX: x,
            startY: y,
            origPoints: line.points.map(p => ({ x: p.x, y: p.y }))
          };
          this.canvas.style.cursor = 'move';
          return;
        }
      }

      // Shift+left (no Alt) → start zoom rect selection
      if (e.button === 0 && e.shiftKey && !e.altKey && this.image) {
        const { cssX, cssY, x: imgX, y: imgY } = this.canvasCoords(e.clientX, e.clientY);
        this.isZoomRectDragging = true;
        this.zoomRectStart = { imgX, imgY, cssX, cssY };
        this.zoomRectEnd = { imgX, imgY, cssX, cssY };
        e.preventDefault();
        e.stopPropagation();
        return;
      }

      const isMiddle = e.button === 1;
      // Alt+left always pans (with or without Shift). If Alt+Shift was on a line,
      // the line-drag block above already returned; here means empty area → fast pan.
      const isAltLeft = e.button === 0 && e.altKey;
      if (!isMiddle && !isAltLeft) return;

      // Alt+left: check if clicking on a point → drag the point instead of panning
      if (isAltLeft) {
        const { x, y } = this.canvasCoords(e.clientX, e.clientY);
        // Priority 1: near a point → drag the point
        const nearPt = this.#findNearestPointWithIdx(x, y);
        if (nearPt) {
          e.preventDefault();
          this.isDraggingPoint = true;
          this.#draggingPoint = nearPt;
          this.canvas.style.cursor = 'move';
          return;
        }
        // Priority 2: near a segment → drag that segment
        const nearSeg = this.#findNearestSegmentWithIdx(x, y);
        if (nearSeg) {
          e.preventDefault();
          this.#beginSegmentDrag(nearSeg, x, y);
          this.canvas.style.cursor = 'move';
          return;
        }
      }

      e.preventDefault();
      this.isPanning = true;
      this.#panLastX = e.clientX;
      this.#panLastY = e.clientY;
      this.canvas.style.cursor = 'grabbing';
    };

    // Listen on both canvas and viewport so middle-click anywhere inside works
    this.canvas.addEventListener('mousedown', startPan);
    viewport.addEventListener('mousedown', startPan);

    // Prevent the browser's default middle-click auto-scroll mode
    viewport.addEventListener('mousedown', e => { if (e.button === 1) e.preventDefault(); });

    document.addEventListener('mousemove', e => {
      // Handle rect-draw drag (rect drawing mode, plain left-drag)
      if (this.isRectDrawDragging) {
        const { cssX, cssY, x: imgX, y: imgY } = this.canvasCoords(e.clientX, e.clientY);
        this.rectDrawEnd = { imgX, imgY, cssX, cssY };
        this.zoomPan.updateRectDrawOverlay();
        return;
      }

      // Handle zoom rect drag (Shift+left-drag)
      if (this.isZoomRectDragging) {
        const { cssX, cssY, x: imgX, y: imgY } = this.canvasCoords(e.clientX, e.clientY);
        this.zoomRectEnd = { imgX, imgY, cssX, cssY };
        this.zoomPan.updateZoomRectOverlay();
        return;
      }

      // Handle point drag
      if (this.isDraggingPoint && this.#draggingPoint) {
        const { x, y } = this.canvasCoords(e.clientX, e.clientY);
        this.#movePointTo(this.#draggingPoint, x, y);
        return;
      }

      // Segment / whole-line drag. Shift is read per-event so pressing or
      // releasing it mid-drag switches live between moving just the grabbed
      // segment and translating the whole line shape — no need to restart.
      if ((this.isDraggingSegment && this.#draggingSegment) ||
          (this.isDraggingLine && this.#draggingLine)) {
        this.#dragMove(e.clientX, e.clientY, e.shiftKey);
        return;
      }
      if (!this.isPanning) return;
      // Delta-based pan with Shift = faster (2.5×). Reading shiftKey per
      // event means user can speed up / slow down mid-drag without jumps.
      const speed = e.shiftKey ? 2.5 : 1;
      viewport.scrollLeft -= (e.clientX - this.#panLastX) * speed;
      viewport.scrollTop  -= (e.clientY - this.#panLastY) * speed;
      this.#panLastX = e.clientX;
      this.#panLastY = e.clientY;
    });
    document.addEventListener('mouseup', e => {
      // Finish rect-draw (rect drawing mode)
      if (this.isRectDrawDragging) {
        this.isRectDrawDragging = false;
        this.zoomPan.hideZoomRectOverlay();
        const s = this.rectDrawStart;
        const en = this.rectDrawEnd;
        this.rectDrawStart = null; this.rectDrawEnd = null;
        if (s && en) {
          const w = Math.abs(en.imgX - s.imgX);
          const h = Math.abs(en.imgY - s.imgY);
          if (w > 3 && h > 3) {
            // createRect auto-connects when continuation mode is active
            this.createRect(s.imgX, s.imgY, en.imgX, en.imgY, false);
          }
        }
        // Stay in rect-drawing mode so multiple rects can be drawn;
        // suppress the trailing click so it isn't treated as a point.
        this.#dragJustEnded = true;
        setTimeout(() => { this.#dragJustEnded = false; }, 50);
        return;
      }

      // Finish zoom rect (Shift+left-drag)
      if (this.isZoomRectDragging) {
        this.isZoomRectDragging = false;
        this.zoomPan.hideZoomRectOverlay();
        const s = this.zoomRectStart;
        const en = this.zoomRectEnd;
        if (s && en) {
          const x1 = Math.min(s.imgX, en.imgX);
          const y1 = Math.min(s.imgY, en.imgY);
          const x2 = Math.max(s.imgX, en.imgX);
          const y2 = Math.max(s.imgY, en.imgY);
          const rectW = x2 - x1;
          const rectH = y2 - y1;
          if (rectW > 4 && rectH > 4) {
            const vp = document.getElementById('canvas-viewport');
            const availW = vp ? vp.clientWidth  : window.innerWidth;
            const availH = vp ? vp.clientHeight : window.innerHeight;
            const newScale = Math.min(availW / rectW, availH / rectH, 5);

            // Disable CSS transition so width/height are applied instantly,
            // allowing scrollLeft/scrollTop to reflect the final canvas size.
            this.canvas.classList.add('zoom-no-transition');
            this.zoomPan.setZoom(newScale, false);
            // Force a synchronous layout — this makes scrollWidth reflect
            // the new canvas size before we assign scrollLeft/scrollTop.
            void this.canvas.getBoundingClientRect();
            if (vp) {
              vp.scrollLeft = Math.max(0, x1 * newScale - (availW - rectW * newScale) / 2);
              vp.scrollTop = Math.max(0, y1 * newScale - (availH - rectH * newScale) / 2);
            }
            // Re-enable transition and persist after layout settles
            requestAnimationFrame(() => {
              this.canvas.classList.remove('zoom-no-transition');
              if (this.image) this.storage.save();
            });
          }
        }
        this.zoomRectStart = null;
        this.zoomRectEnd = null;
        this.canvas.style.cursor = 'crosshair';
        return;
      }

      // Finish point drag
      if (this.isDraggingPoint) {
        this.#endPointDrag(this.#draggingPoint, e.altKey);
        return;
      }

      // Finish segment drag
      if (this.isDraggingSegment) {
        this.#endSegmentDrag(e.altKey);
        return;
      }

      // Finish whole-line drag
      if (this.isDraggingLine) {
        this.isDraggingLine = false;
        this.#draggingLine = null;
        this.saveHistory();
        this.#finishDragGesture(e.altKey);
        return;
      }
      if (!this.isPanning) return;
      this.isPanning = false;
      // Restore cursor
      if (this.isDrawing) {
        this.canvas.style.cursor = 'crosshair';
      } else {
        const { x, y } = this.canvasCoords(e.clientX, e.clientY);
        const overLine = this.findLineAt(x, y) !== -1;
        this.canvas.style.cursor = overLine ? 'pointer' : 'crosshair';
      }
    });

    // Double-click middle mouse button OR Alt+double-left-click → reset zoom (fit to window)
    const resetZoom = e => {
      const isMiddleDouble = e.button === 1;
      const isAltLeftDouble = e.button === 0 && e.altKey;
      if (!isMiddleDouble && !isAltLeftDouble) return;
      e.preventDefault();
      this.zoomPan.fitToWindow();
    };
    this.canvas.addEventListener('dblclick', resetZoom);
    viewport.addEventListener('dblclick', resetZoom);
  }

  // ── Hold-to-draw: an alternative drawing flow ──────────────────
  // A near-stationary plain-left press-and-hold auto-enters drawing and drops the
  // first point; dwelling drops more; releasing commits and exits drawing again.
  // The pure HoldDrawController (./holdDraw.js) decides timing/transitions; this
  // wiring owns the DOM timers, coordinate conversion and rendering. Engaged only
  // when NOT already drawing, so the existing click-to-draw flow is untouched.
  #wireHoldDraw() {
    const ctrl = this.#holdDraw = new HoldDrawController({ holdDelay: this.holdDrawDelay });

    const onDown = e => {
      if (e.button !== 0 || e.altKey || e.shiftKey || e.ctrlKey || e.metaKey) return;
      this.#holdTryDown(e.clientX, e.clientY);
    };
    this.canvas.addEventListener('mousedown', onDown);

    document.addEventListener('mousemove', e => {
      if (!ctrl.engaged) return;
      const r = ctrl.pointerMove(e.clientX, e.clientY, this.#now());
      if (!r) return;
      if (r.type === 'abort') { this.#stopHoldTicks(); ctrl.cancel(); return; }
      if (r.type === 'preview') this.#holdSetPreview(e.clientX, e.clientY);
    });

    document.addEventListener('mouseup', () => {
      if (ctrl.state === 'idle') return;
      const r = ctrl.pointerUp(this.#now());
      this.#stopHoldTicks();
      if (r && r.type === 'commit') this.#holdCommit();
      else this.#holdClearPreview();
    });

    // Drop the gesture if focus leaves the window mid-hold.
    window.addEventListener('blur', () => { this.#stopHoldTicks(); ctrl.cancel(); this.#holdClearPreview(); });
  }

  // Monotonic clock for gesture timing (falls back to Date in old environments).
  #now() { return (typeof performance !== 'undefined' ? performance.now() : Date.now()); }
  #stopHoldTicks() { if (this.#holdTickTimer) { clearInterval(this.#holdTickTimer); this.#holdTickTimer = null; } }
  #startHoldTicks() { if (!this.#holdTickTimer) this.#holdTickTimer = setInterval(() => this.#holdTick(this.#now()), 40); }

  // Arm hold-to-draw at a press point, if eligible. Shared by mouse (#wireHoldDraw)
  // and touch (#wireTouch); the caller has already filtered out modified presses.
  // Returns true if the hold gesture was armed.
  #holdTryDown(clientX, clientY) {
    if (!this.image) return false;
    // Only the auto-mode: manual line/rect drawing keeps its click behavior.
    if (this.isDrawing) return false;
    // Never start over another active gesture (pan / drag / zoom / rect).
    if (this.isPanning || this.isDraggingPoint || this.isDraggingSegment ||
        this.isDraggingLine || this.isZoomRectDragging || this.isRectDrawDragging) return false;
    this.#holdDraw.setHoldDelay(this.holdDrawDelay);
    this.#holdDraw.pointerDown(clientX, clientY, this.#now());
    this.#startHoldTicks();
    return true;
  }

  // ── Touchscreen input (direct manipulation + two-finger) ───────
  // Touch-only layer; preventDefault() suppresses the synthetic mouse/click so it
  // can't collide with the mouse handlers above. Gesture map:
  //   1 finger on a point   → drag that point      (no Alt needed)
  //   1 finger on a segment → drag that segment
  //   1 finger held still on geometry → context menu (mirrors right-click there)
  //   1 finger on empty: tap → place a point; press-and-hold → hold-to-draw
  //   2 fingers → pan + pinch-zoom (focal = the midpoint between the fingers)
  #wireTouch() {
    const viewport = document.getElementById('canvas-viewport');
    const moveTol = TOUCH_DEFAULTS.moveTol;

    const findTouch = (touches, id) => {
      for (let i = 0; i < touches.length; i++) if (touches[i].identifier === id) return touches[i];
      return null;
    };
    // Pinch DOM writes are coalesced into one rAF (like #wireSmoothZoom) so a flood of
    // touchmoves doesn't thrash layout: onMove just stashes the latest scale+midpoint.
    const applyPinch = () => {
      const st = this.#touch;
      if (!st || st.mode !== 'pinch' || !st.pending) { if (st) st.raf = null; return; }
      const { scale, midX, midY } = st.pending;
      st.pending = null;
      st.raf = null;
      this.scale = scale;
      this.canvas.style.width = (this.canvas.width * scale) + 'px';
      this.canvas.style.height = (this.canvas.height * scale) + 'px';
      this.zoomPan.setZoomInputValue(Math.round(scale * 100));
      // Keep the pinched-down image point pinned under the (moving) midpoint — pan + zoom
      // together. vpLeft/vpTop are cached at pinch start (the viewport can't move mid-gesture).
      viewport.scrollLeft = st.imgX * scale - (midX - st.vpLeft);
      viewport.scrollTop = st.imgY * scale - (midY - st.vpTop);
    };

    const clearLongPress = () => {
      if (this.#longPressTimer) { clearTimeout(this.#longPressTimer); this.#longPressTimer = null; }
    };
    // Abandon every single-finger gesture (used when a 2nd finger lands or on cancel).
    const dropSingle = () => {
      clearLongPress();
      this.#stopHoldTicks();
      if (this.#holdDraw.engaged) { this.#holdDraw.cancel(); this.#holdClearPreview(); }
      this.isDraggingPoint = false; this.#draggingPoint = null;
      this.isDraggingSegment = false; this.#draggingSegment = null;
    };
    // A stationary tap behaves exactly like a left mouse click: drops a point in
    // empty space, or selects the line/point under the finger and opens its style
    // panel (canvasClick handles both). Synthesise a modifier-free MouseEvent.
    const tapClick = (e, st) => {
      const ct = (e.changedTouches && e.changedTouches[0]) || st;
      this.canvasClick({
        clientX: ct.clientX ?? st.startX, clientY: ct.clientY ?? st.startY,
        altKey: false, shiftKey: false, ctrlKey: false, metaKey: false,
      });
    };

    // Long-press on grabbed geometry that never moved → open the context menu
    // instead of leaving a no-op drag (empty-space holds belong to hold-to-draw).
    const armGeometryLongPress = (t) => {
      this.#longPressTimer = setTimeout(() => {
        this.#longPressTimer = null;
        if (!this.#touch || this.#touch.id !== t.identifier) return;
        dropSingle();
        this.#touch = { mode: 'done', id: t.identifier };
        this.canvas.dispatchEvent(new MouseEvent('contextmenu',
          { clientX: t.clientX, clientY: t.clientY, bubbles: true, cancelable: true }));
      }, TOUCH_DEFAULTS.longPressMs);
    };

    const onStart = e => {
      if (!this.image) return;

      // Two fingers → pan + pinch. Abandon any in-flight single-finger gesture.
      if (e.touches.length >= 2) {
        e.preventDefault();
        dropSingle();
        const [a, b] = [e.touches[0], e.touches[1]];
        const mid = midpoint(a, b);
        const vpRect = viewport.getBoundingClientRect();
        const contentX = mid.x - vpRect.left + viewport.scrollLeft;
        const contentY = mid.y - vpRect.top + viewport.scrollTop;
        this.canvas.classList.add('zoom-no-transition');
        this.#touch = {
          mode: 'pinch',
          startDist: touchDist(a, b) || 1,
          startScale: this.scale,
          imgX: contentX / this.scale,
          imgY: contentY / this.scale,
          vpLeft: vpRect.left, vpTop: vpRect.top,   // viewport screen pos, fixed for the gesture
          pending: null, raf: null,                 // latest frame awaiting applyPinch
        };
        return;
      }
      if (e.touches.length !== 1) return;
      e.preventDefault();
      const t = e.touches[0];
      const { x, y } = this.canvasCoords(t.clientX, t.clientY);

      // Direct manipulation: a finger landing on a point/segment grabs it.
      const nearPt = this.#findNearestPointWithIdx(x, y);
      if (nearPt) {
        this.isDraggingPoint = true;
        this.#draggingPoint = nearPt;
        this.#touch = { mode: 'point', id: t.identifier, startX: t.clientX, startY: t.clientY };
        armGeometryLongPress(t);
        return;
      }
      const nearSeg = this.#findNearestSegmentWithIdx(x, y);
      if (nearSeg) {
        this.#beginSegmentDrag(nearSeg, x, y);
        this.#touch = { mode: 'segment', id: t.identifier, startX: t.clientX, startY: t.clientY };
        armGeometryLongPress(t);
        return;
      }

      // Empty space: tap places a point, press-and-hold draws (hold-to-draw).
      this.#touch = { mode: 'tap', id: t.identifier, startX: t.clientX, startY: t.clientY, startT: this.#now() };
      this.#holdTryDown(t.clientX, t.clientY);
    };

    const onMove = e => {
      const st = this.#touch;
      if (!st) return;

      if (st.mode === 'pinch') {
        if (e.touches.length < 2) return;
        e.preventDefault();
        const [a, b] = [e.touches[0], e.touches[1]];
        const mid = midpoint(a, b);
        const factor = touchDist(a, b) / st.startDist;
        const newScale = Math.max(0.05, Math.min(5, st.startScale * factor));
        st.pending = { scale: newScale, midX: mid.x, midY: mid.y };
        if (!st.raf) st.raf = requestAnimationFrame(applyPinch);
        return;
      }

      const t = findTouch(e.touches, st.id);
      if (!t) return;
      e.preventDefault();
      const moved = Math.hypot(t.clientX - st.startX, t.clientY - st.startY);

      if (st.mode === 'point') {
        if (moved <= moveTol) return; // below threshold → still a tap; leave room for long-press
        clearLongPress();
        st.dragged = true;
        const { x, y } = this.canvasCoords(t.clientX, t.clientY);
        this.#movePointTo(this.#draggingPoint, x, y);
        return;
      }
      if (st.mode === 'segment') {
        if (moved <= moveTol) return;
        clearLongPress();
        st.dragged = true;
        this.#dragMove(t.clientX, t.clientY, false);
        return;
      }
      if (st.mode === 'tap') {
        st.moved = Math.max(st.moved || 0, moved);
        if (!this.#holdDraw.engaged) return;
        const r = this.#holdDraw.pointerMove(t.clientX, t.clientY, this.#now());
        if (!r) return;
        if (r.type === 'abort') { this.#stopHoldTicks(); this.#holdDraw.cancel(); }
        else if (r.type === 'preview') this.#holdSetPreview(t.clientX, t.clientY);
      }
    };

    const onEnd = e => {
      const st = this.#touch;
      if (!st) return;

      if (st.mode === 'pinch') {
        // A finger lifted: settle the zoom; ignore the lone remaining finger until
        // all fingers are up, so lifting one doesn't kick off a stray drag.
        if (st.raf) { cancelAnimationFrame(st.raf); st.raf = null; }
        if (st.pending) applyPinch();   // flush the last frame so we settle at the real pinch end
        this.canvas.classList.remove('zoom-no-transition');
        this.zoomPan.setZoom(this.scale, true);
        this.#touch = e.touches.length === 0 ? null : { mode: 'done', id: -1 };
        return;
      }

      if (e.touches.length > 0) return; // wait until the last finger lifts
      clearLongPress();

      if (st.mode === 'point') {
        if (st.dragged) {
          this.#endPointDrag(this.#draggingPoint, false);
        } else {
          // Tap (no drag) on a point → select its line + focus the point + open
          // the style panel, exactly as a mouse click does.
          this.isDraggingPoint = false;
          this.#draggingPoint = null;
          tapClick(e, st);
        }
      } else if (st.mode === 'segment') {
        if (st.dragged) {
          this.#endSegmentDrag(false);
        } else {
          // Tap (no drag) on a line → select it + open the style panel.
          this.isDraggingSegment = false;
          this.#draggingSegment = null;
          tapClick(e, st);
        }
      } else if (st.mode === 'tap') {
        const r = this.#holdDraw.pointerUp(this.#now());
        this.#stopHoldTicks();
        if (r && r.type === 'commit') {
          this.#holdCommit();
        } else {
          this.#holdClearPreview();
          // Not a hold stroke → a plain tap drops a point (like a left click).
          const kind = classifyEnd({ moved: st.moved || 0, elapsed: this.#now() - st.startT });
          if (kind === 'tap') tapClick(e, st);
        }
      }
      this.#touch = null;
    };

    const onCancel = () => {
      if (this.#touch && this.#touch.raf) cancelAnimationFrame(this.#touch.raf);
      dropSingle();
      this.#touch = null;
    };

    this.canvas.addEventListener('touchstart', onStart, { passive: false });
    document.addEventListener('touchmove', onMove, { passive: false });
    document.addEventListener('touchend', onEnd);
    document.addEventListener('touchcancel', onCancel);
  }

  #holdTick(t) {
    const r = this.#holdDraw.tick(t);
    if (!r) return;
    if (r.type === 'start') this.#holdStart(r.x, r.y);
    else if (r.type === 'drop') this.#holdDrop(r.x, r.y);
  }

  // Hold completed → auto-enable drawing and seed the stroke. The target under
  // the press point decides: existing point → continue that line from it;
  // line body → insert a point on it then continue; empty → fresh line.
  #holdStart(clientX, clientY) {
    const { x, y } = this.canvasCoords(clientX, clientY);
    this.#holdAutoEnabled = true;
    const target = holdDrawTarget(this.lines, x, y);
    this.#holdPrepend = false;
    if (target.kind === 'point') {
      this.selectedLineIdx = target.lineIdx;
      this.coordLineIdx = target.lineIdx;
      this.focusedPtIdx = target.ptIdx;
      this.startDrawingMode({ connect: true });
      // Holding the FIRST point extends the line backward: prepend new points
      // before it (index 0) instead of inserting after it as the second point.
      if (target.ptIdx === 0) { this.#holdPrepend = true; this.#continueInsertIdx = 0; }
    } else if (target.kind === 'segment') {
      // Insert the seed point on the existing line, then extend from it.
      this.#insertPointOnSegment(target.lineIdx, target.ptIdx2, x, y);
      this.startDrawingMode({ connect: true });
    } else {
      this.startDrawingMode({ connect: false });
      if (this.currentLine) this.currentLine.points.push({ x, y });
    }
    this.#holdSetPreviewImg(x, y);
    this.updateButtons();
  }

  // Dwell completed → drop a point (extends the in-progress / continued line).
  #holdDrop(clientX, clientY) {
    const { x, y } = this.canvasCoords(clientX, clientY);
    if (this.#continueLineIdx >= 0 && this.lines[this.#continueLineIdx]) {
      const line = this.lines[this.#continueLineIdx];
      line.points.splice(this.#continueInsertIdx, 0, { x, y });
      this.focusedPtIdx = this.#continueInsertIdx;
      // Prepend mode keeps inserting at index 0 (each new point becomes the new
      // head); forward mode advances the insert point so points keep appending.
      if (!this.#holdPrepend) this.#continueInsertIdx++;
      this.coordTable.update(line.points, this.#continueLineIdx);
    } else if (this.currentLine) {
      this.currentLine.points.push({ x, y });
    }
    this.#holdSetPreviewImg(x, y);
    this.updateButtons();
  }

  // Release after a hold stroke → commit the line and disable drawing mode,
  // then suppress the trailing synthetic click so it isn't read as a point/select.
  #holdCommit() {
    this.#holdClearPreview();
    if (this.isDrawing) this.stopDrawingMode();
    this.#holdAutoEnabled = false;
    this.#holdPrepend = false;
    this.#dragJustEnded = true;
    setTimeout(() => { this.#dragJustEnded = false; }, 50);
  }

  #holdSetPreview(clientX, clientY) {
    const { x, y } = this.canvasCoords(clientX, clientY);
    this.#holdSetPreviewImg(x, y);
  }
  #holdSetPreviewImg(x, y) {
    this.holdPreview = { x, y };
    this.renderer.redraw();
  }
  #holdClearPreview() {
    if (this.holdPreview) { this.holdPreview = null; this.renderer.redraw(); }
  }

  // The point a hold-draw preview line should emanate from: the last point of the
  // in-progress line, or the current tail of the line being extended. null = none.
  holdAnchorPoint() {
    if (this.currentLine && this.currentLine.points.length)
      return this.currentLine.points[this.currentLine.points.length - 1];
    if (this.#continueLineIdx >= 0 && this.lines[this.#continueLineIdx]) {
      const pts = this.lines[this.#continueLineIdx].points;
      // Prepend: the next point connects to the current head (index 0); forward:
      // it connects to the point just before the insertion tail.
      if (this.#holdPrepend) return pts[this.#continueInsertIdx] ?? pts[0] ?? null;
      return pts[this.#continueInsertIdx - 1] ?? pts[pts.length - 1] ?? null;
    }
    return null;
  }

  // Set the hold-to-draw dwell/hold delay (ms). Clamped to a sane range; persisted.
  setHoldDrawDelay(ms, { persist = true } = {}) {
    const n = Number(ms);
    if (!Number.isFinite(n)) return this;
    this.holdDrawDelay = Math.max(100, Math.min(3000, Math.round(n)));
    if (this.#holdDraw) this.#holdDraw.setHoldDelay(this.holdDrawDelay);
    if (persist) this.storage.save();
    return this;
  }

  // Convert a viewport client point to canvas CSS offset and image-space coords.
  canvasCoords(clientX, clientY) {
    const rect = this.canvas.getBoundingClientRect();
    const cssX = clientX - rect.left;
    const cssY = clientY - rect.top;
    return { cssX, cssY, x: cssX / this.scale, y: cssY / this.scale };
  }

  loadImage(e) {
    const file = e.target.files[0];
    if (!file) return;
    this.loadImageFromFile(file);
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
    // Incognito never creates on a server — drop a create-on-server address (central guard
    // covering openImageHere / createBlankImage / the console API).
    const remoteCreateAddress = (opts.address && !opts.remoteId && !this.storage.incognito) ? opts.address : null;
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
        if ((opts.remoteId || opts.adoptLayout) && remoteLayout) this.#adoptServerPageFormat(remoteLayout);
        if (remoteLayout && remoteLayout.cropRect) {
          this.cropRect = this.#roundRect(remoteLayout.cropRect);
        } else if (opts.crop) {
          this.cropRect = this.#roundRect(opts.crop);
        } else if (opts.noCrop) {
          const { w: iw, h: ih } = this.#rotatedOriginalDims();
          this.cropRect = this.#roundRect({ x: 0, y: 0, width: iw, height: ih }, iw, ih);
        } else {
          this.cropRect = this.defaultCropRect(opts.album);
        }
        this.rebuildCroppedImage();

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
            this.#adoptServerFilter(remoteLayout);
            this.#adoptServerFormulas(remoteLayout);
          } else {
            this.#adoptServerFilter({});     // no saved filter — reset to 'none'
            this.#adoptServerFormulas({});   // no saved formulas — reset to off
          }
        }

        this.currentLine = null;
        this.history.reset(this.lines);
        this.zoomPan.fitToWindow();
        this.updateInfo();
        this.coordTable.update(this.lines.length > 0 ? this.lines[this.lines.length - 1].points : null);
        this.renderer.redraw();
        this.updateButtons();
        this.updateCoordStatus();
        this.storage.save();

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

  // ── External launch (browser extension) ──────────────────────────
  // Extension hands off an image via URL fragment `#stencil=<encodeURIComponent(JSON)>`,
  // shape { dataUrl, name?, crop?, page?, source?, resource?, open?, incognito? }. Fragment
  // (not query) keeps the payload off server/logs; consumed once, stripped, routed through the
  // normal upload. `open:'resume'` switches to an existing same-source project (cross-origin, so
  // the extension can't dedup itself); else import a new project, auto-numbered "name (N)".
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
    if (!payload || typeof payload.dataUrl !== 'string') return;

    // Page size must be applied BEFORE loading so the crop aspect and pixel↔page
    // conversion match the size the image was cropped for in the extension.
    if (payload.page && typeof payload.page === 'object') this.#setExternalPage(payload.page);

    if (payload.incognito) {
      this.storage.incognito = true;
      this.updateIncognitoUI();
    }

    const name = typeof payload.name === 'string' && payload.name ? payload.name : 'image.png';
    const crop = payload.crop && typeof payload.crop === 'object' ? payload.crop : null;
    const source = typeof payload.source === 'string' && payload.source ? payload.source : null;
    const resource = typeof payload.resource === 'string' && payload.resource ? payload.resource : null;

    // Resume: if we hold project(s) for this source, switch instead of re-importing.
    // Several matches → open the projects list to pick. No match (stale ledger / expired
    // project) falls through to a fresh import.
    if (payload.open === 'resume' && !this.storage.incognito && (source || name)) {
      const baseName = this.#stripExt(name);
      const matches = this.storage.store.findByImage(source, baseName);
      if (matches.length && this.switchToProject(matches[0].id)) {
        if (matches.length > 1) {
          notify(`Resumed "${matches[0].name}" — ${matches.length} projects share this image`, 'ok');
          document.getElementById('projects-btn')?.click();
        }
        return;
      }
    }

    // Fresh import. Auto-number the name against existing same-source projects so repeats
    // become "name (1)", "name (2)", … (skipped for incognito, which never persists).
    // `open:'copy'` takes the same path.
    const opts = crop ? { crop } : {};
    opts.source = source;
    opts.resource = resource;
    if (!this.storage.incognito && source) opts.name = this.storage.store.copyName(this.#stripExt(name), source);

    fetch(payload.dataUrl)
      .then(r => r.blob())
      .then(blob => this.loadImageFromFile(new File([blob], name, { type: blob.type || 'image/png' }), opts))
      .catch(() => notify('Stencil: failed to load the shared image', 'fail'));
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
    const size = page.size === 'A4' ? 'A4' : page.size === 'custom' ? 'custom' : 'A3';
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

  // Page natural dimensions (cm) as selected — NOT orientation-swapped (only the
  // proportions matter). Mirrors blankImageModal.pageDims.
  #pageCmDims() {
    return this.pageSize === 'custom'
      ? { width: this.customPageWidth, height: this.customPageHeight }
      : (PAGE_SIZES[this.pageSize] || PAGE_SIZES.A4);
  }

  // The default centered crop for the loaded original: page aspect in the
  // orientation matching the image (album when wider than tall). Public so the
  // storage layer can default-crop legacy projects saved before cropping existed.
  // `albumOverride` (optional) forces album (true) / portrait (false) orientation;
  // omitted, orientation auto-matches the image (wider-than-tall ⇒ album).
  defaultCropRect(albumOverride) {
    const { w: iw, h: ih } = this.#rotatedOriginalDims();
    const isAlbum = (albumOverride == null) ? isAlbumOrientation(iw, ih) : !!albumOverride;
    const aspect = cropAspect(this.#pageCmDims().width, this.#pageCmDims().height, isAlbum);
    return this.#roundRect(centeredCrop(iw, ih, aspect), iw, ih);
  }

  // Dimensions of the original image after the current rotation is applied (the
  // pixel space `cropRect` lives in). Odd quarter-turns swap width and height.
  #rotatedOriginalDims() {
    const w = this.originalImage.width, h = this.originalImage.height;
    return (this.rotationQuarters % 2) ? { w: h, h: w } : { w, h };
  }

  // The original image rotated by the current quarter-turn count (clockwise). For
  // no rotation the untouched bitmap is returned; otherwise a freshly-rotated
  // canvas. Used by rebuildCroppedImage and the crop modal's preview.
  #rotatedOriginalCanvas() {
    const img = this.originalImage;
    const q = ((this.rotationQuarters % 4) + 4) % 4;
    if (q === 0) return img;
    const swap = q % 2 === 1;
    const c = document.createElement('canvas');
    c.width = swap ? img.height : img.width;
    c.height = swap ? img.width : img.height;
    const ctx = c.getContext('2d');
    ctx.translate(c.width / 2, c.height / 2);
    ctx.rotate(q * Math.PI / 2);
    ctx.drawImage(img, -img.width / 2, -img.height / 2);
    return c;
  }

  // The rotated original as a data URL + dimensions, for the crop modal (which
  // previews the full original). Returns the stored data URL untouched when the
  // image is not rotated, avoiding a needless re-encode.
  effectiveOriginalDims() { return this.#rotatedOriginalDims(); }
  effectiveOriginalDataUrl() {
    if (!this.rotationQuarters) return this.imageDataUrl;
    return this.#rotatedOriginalCanvas().toDataURL();
  }

  // Snap a crop rect to integer pixels, clamped inside the rotated original image.
  #roundRect(r, iw = this.#rotatedOriginalDims().w, ih = this.#rotatedOriginalDims().h) {
    let w = Math.max(1, Math.min(Math.round(r.width), iw));
    let h = Math.max(1, Math.min(Math.round(r.height), ih));
    const x = Math.max(0, Math.min(Math.round(r.x), iw - w));
    const y = Math.max(0, Math.min(Math.round(r.y), ih - h));
    return { x, y, width: w, height: h };
  }

  // Rebuild the working `image` canvas from the rotated `originalImage` + `cropRect`,
  // sizing the main canvas to the crop. Original never modified. Public so storage can
  // rebuild the view after restoring original + rotation + cropRect.
  rebuildCroppedImage() {
    const src = this.#rotatedOriginalCanvas();
    const r = this.cropRect;
    const c = document.createElement('canvas');
    c.width = r.width;
    c.height = r.height;
    c.getContext('2d').drawImage(src, r.x, r.y, r.width, r.height, 0, 0, r.width, r.height);
    this.image = c;
    this.canvas.width = r.width;
    this.canvas.height = r.height;
  }

  /**
   * Hide the selection panel and its fullscreen mirror.
   * @returns {void}
   */
  #hideSelectionPanels() {
    const selPanel = document.getElementById('selection-panel');
    if (selPanel) selPanel.style.display = 'none';
    const fsPanel = document.getElementById('fs-selection-panel');
    if (fsPanel) fsPanel.style.display = 'none';
  }

  /**
   * Reset selection/drawing state and refresh every view after the image
   * geometry changes (rotate or crop): clears the active selection, resets
   * history to the current lines, refits the viewport, and persists.
   * @returns {void}
   */
  #afterImageGeometryChange() {
    this.currentLine = null;
    this.selectedLineIdx = -1;
    this.coordLineIdx = -1;
    this.focusedPtIdx = -1;
    this.#hideSelectionPanels();
    this.history.reset(this.lines);
    this.zoomPan.fitToWindow();
    this.updateInfo();
    this.renderer.redraw();
    this.updateButtons();
    this.updateCoordStatus();
    this.coordTable.update(this.lines.length > 0 ? this.lines[this.lines.length - 1].points : null);
    this.storage.save();
    this.scheduleRemoteSync(); // crop/rotate change the layout's geometry — push it to peers too
  }

  // Rotate the whole image a quarter turn — dir < 0 rotates left (CCW), dir > 0
  // rotates right (CW). The crop window and every line follow the picture so the
  // framing and the drawing stay put relative to the image content.
  rotateImage(dir) {
    if (!this.originalImage) {
      notify('Open an image first', 'fail');
      return;
    }
    const clockwise = dir > 0;
    const dims = this.#rotatedOriginalDims();  // space the crop currently lives in
    // Points first — they rotate inside the OLD crop box (width x height).
    rotateLinePointsQuarter(this.lines, this.cropRect.width, this.cropRect.height, clockwise);
    const rotated = rotateCropRectQuarter(this.cropRect, dims.w, dims.h, clockwise);
    this.rotationQuarters = (((this.rotationQuarters + (clockwise ? 1 : -1)) % 4) + 4) % 4;
    this.cropRect = this.#roundRect(rotated);
    this.rebuildCroppedImage();
    this.#afterImageGeometryChange();
  }

  // Apply a new crop rectangle (image-space). With opts.recalc, existing lines
  // are cleared on an orientation flip or rescaled to the new size (the page
  // relation is preserved). Does NOT replace the stored original image.
  applyCrop(rect, opts = {}) {
    if (!this.originalImage) return;
    const newRect = this.#roundRect(rect);
    if (opts.recalc && this.cropRect) {
      const change = cropChange(this.cropRect, newRect);
      if (change.orientationChanged) this.lines = [];
      else if (change.scale !== 1) scaleLinePoints(this.lines, change.scale);
    }
    this.cropRect = newRect;
    this.rebuildCroppedImage();
    this.#afterImageGeometryChange();
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
        this.applyPastedLayout(data);
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
      this.#continueLineIdx = this.selectedLineIdx;
      const line = this.lines[this.#continueLineIdx];
      this.#continueInsertIdx = resolveInsertIdx(line, {
        coordLineIdx: this.coordLineIdx,
        selectedLineIdx: this.selectedLineIdx,
        focusedPtIdx: this.focusedPtIdx
      });
      this.currentLine = null;
      this.undonePoints = [];
      document.getElementById('start-drawing').classList.add('active');
      document.getElementById('stop-drawing').disabled = false;
      this.coordLineIdx = this.#continueLineIdx;
      this.coordTable.update(line.points, this.#continueLineIdx);
      this.updateButtons();
      this.renderer.redraw();
      notify('Continuing selected line — new points connect to it', 'info');
      return;
    }

    this.#continueLineIdx = -1;
    this.#continueInsertIdx = -1;
    this.currentLine = {
      points: [],
      color: this.color,
      thickness: this.thickness,
      markerSize: this.markerSize,
      style: this.style
    };
    if (!opts.keepSelection) {
      this.selectedLineIdx = -1;
      this.#hideSelectionPanels();
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
    if (this.#continueLineIdx >= 0) {
      const li = this.#continueLineIdx;
      this.#continueLineIdx = -1;
      this.#continueInsertIdx = -1;
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

  canvasClick(e) {
    // No image → the canvas is an empty void; don't let clicks drop points.
    if (!this.image) return;
    // Ignore click that ended a pan gesture or point drag
    if (e.altKey) return;
    if (e.shiftKey) return; // Shift+drag is for zoom-area rect
    if (this.#dragJustEnded) return;
    const { x, y } = this.canvasCoords(e.clientX, e.clientY);

    if (this.isDrawing) {
      if (this.drawMode === 'rect') return; // rect areas are created by dragging

      // Ctrl/Cmd+click on an existing committed segment → insert a point BETWEEN
      // that segment's two endpoints (same as Ctrl+click outside drawing mode),
      // instead of appending it at the line's tail with a connecting segment.
      if (e.ctrlKey || e.metaKey) {
        const nearSeg = this.#findNearestSegmentWithIdx(x, y);
        if (nearSeg) {
          this.#insertPointOnSegment(nearSeg.lineIdx, nearSeg.ptIdx2, x, y);
          // Inserting shifts later indices right by one; keep the continuation
          // tail anchored to the same logical spot on the line we're extending.
          if (nearSeg.lineIdx === this.#continueLineIdx && nearSeg.ptIdx2 <= this.#continueInsertIdx)
            this.#continueInsertIdx++;
          return;
        }
        // No segment under the cursor → fall through to normal drawing behavior.
      }

      // Continuation drawing: extend the selected line at the insert point
      if (this.#continueLineIdx >= 0 && this.lines[this.#continueLineIdx]) {
        const line = this.lines[this.#continueLineIdx];
        // Click near the first point closes it into a locked area
        if (this.#shouldCloseShape(line.points, x, y, line.markerSize ?? this.markerSize)) {
          this.#closeContinuedShape();
          return;
        }
        line.points.splice(this.#continueInsertIdx, 0, { x, y });
        this.focusedPtIdx = this.#continueInsertIdx;
        this.#continueInsertIdx++;
        this.coordTable.update(line.points, this.#continueLineIdx);
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
      const nearSeg = this.#findNearestSegmentWithIdx(x, y);
      if (nearSeg) {
        // Hovering a line → insert a point between its two connecting points
        this.#insertPointOnSegment(nearSeg.lineIdx, nearSeg.ptIdx2, x, y);
      } else {
        // Empty space → add a new point (connected to selection if any)
        this.#addConnectedPoint(x, y);
      }
      return;
    }

    // Non-drawing mode: priority 1 — click on a point of any committed line
    // → select that line, focus the clicked point in the coord table
    const nearPt = this.#findNearestPointWithIdx(x, y);
    if (nearPt && nearPt.lineIdx !== -1) {
      this.selectedLineIdx = nearPt.lineIdx;
      this.showSelectionPanel(this.lines[nearPt.lineIdx]);
      this.coordLineIdx = nearPt.lineIdx;
      this.focusedPtIdx = nearPt.ptIdx;
      this.coordTable.update(this.lines[nearPt.lineIdx].points, nearPt.lineIdx);
      this.renderer.redraw();
      // Bring the focused row into view inside the coord table
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
    this.#closeShape({ line: this.lines[this.#continueLineIdx], idx: this.#continueLineIdx, isContinuation: true });
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
      this.#continueLineIdx = -1;
      this.#continueInsertIdx = -1;
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
  #insertPointOnSegment(lineIdx, insertIdx, x, y) {
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
    if (this.#continueLineIdx >= 0 && this.lines[this.#continueLineIdx]) {
      const line = this.lines[this.#continueLineIdx];
      const insertIdx = this.#continueInsertIdx;
      line.points.splice(insertIdx, 0, ...corners);
      this.#continueInsertIdx = insertIdx + corners.length;
      this.coordLineIdx = this.#continueLineIdx;
      this.focusedPtIdx = this.#continueInsertIdx - 1;
      this.coordTable.update(line.points, this.#continueLineIdx);
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
    const nearPtIdx = this.#findNearestPointWithIdx(x, y);
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
        const nearSeg = nearPtIdx ? null : this.#findNearestSegmentWithIdx(x, y);
        this.canvas.style.cursor = (nearPtIdx || nearSeg) ? 'move' : 'grab';
      }
      this.tooltipMgr.hide();
      return;
    }

    // Cursor
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

      // Check points
      for (const p of pts)
        if (Math.hypot(p.x - x, p.y - y) <= threshold + 4) return i;

      // Check segments
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
    // Wire events
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
    this.coordLineIdx = -1;
    this.hoveredPtIdx = -1;
    this.focusedPtIdx = -1;
    this.#hideSelectionPanels();
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
  #beginSegmentDrag(nearSeg, x, y) {
    const line = this.lines[nearSeg.lineIdx];
    this.isDraggingSegment = true;
    this.#draggingSegment = {
      lineIdx: nearSeg.lineIdx, ptIdx1: nearSeg.ptIdx1, ptIdx2: nearSeg.ptIdx2,
      startX: x, startY: y,
      origPt1: { x: line.points[nearSeg.ptIdx1].x, y: line.points[nearSeg.ptIdx1].y },
      origPt2: { x: line.points[nearSeg.ptIdx2].x, y: line.points[nearSeg.ptIdx2].y },
      origPoints: line.points.map(p => ({ x: p.x, y: p.y })),
    };
  }

  // Move the currently-dragged point (dp = #draggingPoint) to canvas coords (x, y) and
  // refresh its coordinate row. Shared by the mouse and touch point-drag paths.
  #movePointTo(dp, x, y) {
    const line = dp.lineIdx === -1 ? this.currentLine : this.lines[dp.lineIdx];
    if (!line) return;
    line.points[dp.ptIdx].x = x;
    line.points[dp.ptIdx].y = y;
    this.renderer.redraw();
    this.coordTable.refreshCoordRow(dp.ptIdx);
  }

  // Finish a point drag: clear state, save history (only for a placed line), commit.
  #endPointDrag(dp, altKey) {
    this.isDraggingPoint = false;
    this.#draggingPoint = null;
    if (dp && dp.lineIdx !== -1) this.saveHistory();
    this.#finishDragGesture(altKey);
  }

  // Finish a segment drag: clear state, save history, commit.
  #endSegmentDrag(altKey) {
    this.isDraggingSegment = false;
    this.#draggingSegment = null;
    this.saveHistory();
    this.#finishDragGesture(altKey);
  }

  // Apply the active segment/whole-line drag at the cursor. `shiftKey` decides the mode
  // live: held → translate the entire line shape; released → move only the grabbed segment's
  // two endpoints. Both derive from the original snapshot, so toggling Shift never accumulates.
  #dragMove(clientX, clientY, shiftKey) {
    const { x, y } = this.canvasCoords(clientX, clientY);

    if (this.isDraggingSegment && this.#draggingSegment) {
      const ds = this.#draggingSegment;
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

    if (this.isDraggingLine && this.#draggingLine) {
      const dl = this.#draggingLine;
      const line = this.lines[dl.lineIdx];
      if (!line) return;
      const dx = x - dl.startX;
      const dy = y - dl.startY;
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
  #finishDragGesture(altKey) {
    this.#dragJustEnded = true;
    setTimeout(() => { this.#dragJustEnded = false; }, 50);
    this.canvas.style.cursor = altKey ? 'grab' : 'crosshair';
  }

  // Find nearest point and return { lineIdx, ptIdx, point }
  #findNearestPointWithIdx(x, y, threshold = 12) {
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
  #findNearestSegmentWithIdx(x, y, threshold = 12) {
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
  #adjustThicknessAtCursor(e) {
    const { x, y } = this.canvasCoords(e.clientX, e.clientY);
    const delta = e.deltaY > 0 ? -1 : 1;
    const nearPt = this.#findNearestPointWithIdx(x, y);
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

  // Ctrl+Shift+wheel: rotate the selected line about its center (or the focused point).
  #rotateSelectedLine(angle) {
    const line = this.lines[this.selectedLineIdx];
    if (!line || line.points.length < 2) return;
    let cx;
    let cy;
    if (this.coordLineIdx === this.selectedLineIdx && this.focusedPtIdx >= 0
        && line.points[this.focusedPtIdx]) {
      cx = line.points[this.focusedPtIdx].x;
      cy = line.points[this.focusedPtIdx].y;
    } else {
      const bboxCenter = core.op('boundingBoxCenter');
      if (bboxCenter) {
        // Pivot = bbox center via the shared C++ core (wasm).
        ({ x: cx, y: cy } = bboxCenter(line.points));
      } else {
        let minX = Infinity;
        let minY = Infinity;
        let maxX = -Infinity;
        let maxY = -Infinity;
        for (const p of line.points) {
          if (p.x < minX) minX = p.x;
          if (p.x > maxX) maxX = p.x;
          if (p.y < minY) minY = p.y;
          if (p.y > maxY) maxY = p.y;
        }
        cx = (minX + maxX) / 2;
        cy = (minY + maxY) / 2;
      }
    }
    const rotate = core.op('rotatePoints');
    if (rotate) {
      // Rotate every point about the pivot via the shared C++ core (wasm).
      rotate(line.points, cx, cy, angle);
    } else {
      const cos = Math.cos(angle);
      const sin = Math.sin(angle);
      line.points.forEach(p => {
        const dx = p.x - cx;
        const dy = p.y - cy;
        p.x = cx + dx * cos - dy * sin;
        p.y = cy + dx * sin + dy * cos;
      });
    }
    this.renderer.redraw();
    if (this.coordLineIdx === this.selectedLineIdx) this.coordTable.update(line.points, this.selectedLineIdx);
    clearTimeout(this.#rotateSaveTimer);
    this.#rotateSaveTimer = setTimeout(() => { this.saveHistory(); this.storage.save(); }, 280);
  }

  saveHistory() {
    this.history.push(this.lines);
    this.storage.save();
    this.scheduleRemoteSync();
  }

  // Live co-edit (send): debounce a save-back to the server after an edit so peers on
  // the same project get a `project-event` and reload. No-op for local-only projects,
  // and when the "Sync changes to server" setting is off (edit-in-memory only).
  scheduleRemoteSync() {
    if (!this.remoteLink || !getSyncToServer()) return;
    // Mid-reload: defer (don't drop) the push; reloadRemoteActive flushes it when it settles.
    if (this.#reloadingRemote) { this.#remoteSyncPending = true; return; }
    clearTimeout(this.#remoteSyncTimer);
    this.#remoteSyncTimer = setTimeout(() => {
      if (!this.remoteLink || !getSyncToServer()) return;
      if (this.#reloadingRemote) { this.#remoteSyncPending = true; return; }
      this.saveToServer();
    }, 700);
  }

  // Live co-edit (receive): a server project-event for the project we're editing —
  // reload from the server when it's a genuine peer change. Wired from the connection
  // event feed (stencilApi onChange).
  onServerProjectEvent(msg, conn) {
    if (!getSyncToServer()) return; // sync off — don't auto-pull peer changes
    if (!shouldReloadFromEvent(msg, this.remoteLink, {
      lastLocalSaveAt: this.#lastRemoteSaveAt,
      isDrawing: this.isDrawing,
      connUrl: conn ? conn.url : null,
    })) return;
    this.reloadRemoteActive();
  }

  // Re-fetch the active server project (image + layout) and apply it, so a peer's saved
  // change shows live. Uses original/source as the base + re-applies the stored layout
  // (lines + filter) — never the baked `result` (would double-draw). Guarded so the
  // reload's own redraws don't trigger a push back.
  async reloadRemoteActive() {
    const link = this.remoteLink;
    if (!link || this.#reloadingRemote) return;
    const conn = this.connections && this.connections.get(link.address);
    if (!conn) return;
    this.#reloadingRemote = true;
    try {
      const full = await conn.getProject(link.remoteId);
      const src = full.project?.source || '';
      const blob = await this.#fetchRemoteOriginal(conn, link.remoteId, src);
      if (!blob) return;
      const ext = (blob.type && blob.type.split('/')[1]) || 'png';
      const file = new File([blob], `${this.imageBaseName || 'image'}.${ext}`, { type: blob.type || 'image/png' });
      this.loadImageFromFile(file, {
        source: src,
        resource: full.project?.resource || '',
        address: link.address,
        remoteId: link.remoteId,
        version: full.project?.version || link.version,
        layout: full.layout,
      });
      notify('Updated from server', 'ok');
    } catch { notify("Couldn't refresh from server — showing the last loaded version", 'info'); }
    finally {
      setTimeout(() => {
        this.#reloadingRemote = false;
        // Flush an edit that landed during the reload, so client and server reconverge.
        if (this.#remoteSyncPending) {
          this.#remoteSyncPending = false;
          this.scheduleRemoteSync();
        }
      }, 250);
    }
  }

  // Fetch a server project's original bytes, falling back to its http(s) source URL
  // (CORS) when the server stores none. Returns a Blob or null.
  async #fetchRemoteOriginal(conn, remoteId, src) {
    let blob = null;
    try { blob = await conn.fetchFile(remoteId, 'original'); } catch {}
    if (!blob && /^https?:/i.test(src || '')) {
      const resp = await fetch(src, { mode: 'cors' });
      if (resp.ok) blob = await resp.blob();
    }
    return blob;
  }

  // Adopt a server layout's filter/tint into the editor + filter UI, and clear the
  // dirty flag (the server's filter is now ours). Used by conflict-merge so a line-only
  // edit preserves a peer's filter change instead of clobbering it.
  #adoptServerFilter(layout) {
    if (!layout) return;
    this.imageFilter = layout.imageFilter || (layout.blackAndWhite ? 'bw' : 'none');
    if (layout.filterColor) this.filterColor = layout.filterColor;
    setVal('image-filter', this.imageFilter);
    setRadioGroup('ctxFilter', this.imageFilter);
    const fp = document.getElementById('filter-color');
    if (fp) {
      fp.value = this.filterColor;
      fp.style.display = this.imageFilter === 'custom' ? 'inline-block' : 'none';
    }
    this.#filterDirty = false;
  }

  // Restore a server layout's page format (A3/A4/custom + cm dims) into state + the page UI.
  #adoptServerPageFormat(layout) {
    if (!layout) return;
    const n = normalizePageSize(layout.pageSize);
    if (n) {
      this.pageSize = n;
      setVal('page-size', n);
      const cg = document.getElementById('custom-size-group');
      if (cg) cg.style.display = n === 'custom' ? 'inline-flex' : 'none';
    }
    if (Number.isFinite(layout.customPageWidth)) {
      this.customPageWidth = layout.customPageWidth;
      setVal('custom-page-width', cmToUnit(layout.customPageWidth, this.unit));
    }
    if (Number.isFinite(layout.customPageHeight)) {
      this.customPageHeight = layout.customPageHeight;
      setVal('custom-page-height', cmToUnit(layout.customPageHeight, this.unit));
    }
  }

  // Restore a server layout's x/y formulas into state + the formula UI. The expressions are
  // kept regardless of the toggle (allow only gates visibility + whether they're applied).
  #adoptServerFormulas(layout) {
    const allow = !!(layout && layout.allowFormulas);
    this.allowFormulas = allow;
    const cb = document.getElementById('allow-formulas');
    if (cb) cb.checked = allow;
    this.#syncFormulaUI(allow);
    const fx = layout && typeof layout.formulaX === 'string' ? layout.formulaX : '';
    const fy = layout && typeof layout.formulaY === 'string' ? layout.formulaY : '';
    this.formulaX = fx;
    this.formulaY = fy;
    setVal('formula-x', this.formulaX);
    setVal('formula-y', this.formulaY);
    setVal('ctx-formula-x', this.formulaX);
    setVal('ctx-formula-y', this.formulaY);
    this.#showFormulaError(false);
  }

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
    // Recompose tooltips so the reason line appears/clears with the disabled state
    // (and hotkey buttons keep their combo). Covers every control carrying either
    // a hotkey id or a disabled-reason.
    document.querySelectorAll('[data-disabled-reason], [data-hk-title]').forEach(el => {
      el.title = composeControlTitle(el, hotkeys.isMac, id => hotkeys.get(id));
    });

    this.updateIncognitoUI();
    this.updateProjectTitle();
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
      if (!this.#nameEditing) input.readOnly = true;
      input.placeholder = editable ? 'Untitled' : (this.storage.incognito ? 'Incognito (unsaved)' : 'No project');
      if (editBtn && !this.#nameEditing) editBtn.style.display = editable ? '' : 'none';
      this.#nameEditor?.refresh();                      // set ✓ enabled/disabled state
    }
    // Outside edit mode (no project, incognito, post-commit, click-away) the ✓/✗
    // rename controls must never linger — they belong to edit mode only.
    if (!this.#nameEditing) {
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
    if (this.image) {
      info.textContent = `Image Size: ${this.canvas.width} × ${this.canvas.height} px  |  Zoom: Ctrl+Scroll · Alt+± · +/− btn  (+Shift = larger)  |  Alt+Scroll: thickness  |  Ctrl+Shift+Scroll: rotate selected  |  Ctrl+Click: add point  |  ℹ for full help`;
      if (sizeDisplay) {
        sizeDisplay.innerHTML = `${icon('ruler', { size: 13 })} ${this.canvas.width} × ${this.canvas.height} px`;
        sizeDisplay.style.display = 'inline-flex';
      }
    } else {
      info.textContent = 'No image loaded. Upload an image to start.';
      if (sizeDisplay) sizeDisplay.style.display = 'none';
    }
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
      this.tabs.reportActive(id);
      this.updateProjectTitle();   // reflect (or clear) the remote badge + outline now
      // Server-linked: pull the latest so a reopen shows peers' newest state, not stale cache.
      if (this.remoteLink && getSyncToServer()) this.reloadRemoteActive();
      return true;
    }
    return false;
  }

  // Open a saved project in a NEW browser tab, leaving this tab untouched. The
  // new tab boots with a "?open=<id>" deep link that applyProjectDeepLink()
  // consumes. Default open-in-current-tab behavior stays on switchToProject().
  openProjectInNewTab(id) {
    if (id == null) return;
    const base = location.origin + location.pathname;
    window.open(buildOpenProjectUrl(base, id), '_blank');
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
  openImageHere(file, incognito = false, address = null) {
    if (!file) return;
    const toServer = !!address && !incognito;
    if (toServer) requireConnection(this.connections, address);   // validate up front
    if (!this.storage.incognito) this.storage.save();
    this.newEditor();
    if (incognito) { this.storage.incognito = true; this.updateIncognitoUI(); }
    this.loadImageFromFile(file, toServer ? { address } : {});
  }

  // Open-image dialog action: launch `file` in a NEW browser tab via the #stencil=
  // fragment hand-off (consumed by applyExternalLaunch, which honors `incognito`).
  openImageNewTab(file, incognito = false) {
    if (!file) return;
    const reader = new FileReader();
    reader.onload = () => {
      const base = location.origin + location.pathname;
      const url = buildExternalLaunchUrl(base, { dataUrl: reader.result, name: file.name, incognito: !!incognito });
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
    const cnv = document.createElement('canvas');
    cnv.width = w; cnv.height = h;
    const ctx = cnv.getContext('2d');
    ctx.fillStyle = color || '#ffffff';
    ctx.fillRect(0, 0, w, h);
    return new Promise((resolve, reject) => {
      cnv.toBlob(blob => {
        if (!blob) { reject(new Error('Could not create the image')); return; }
        this.loadImageFromFile(new File([blob], `blank-${w}x${h}.png`, { type: 'image/png' }), { address: address || undefined });
        resolve({ width: w, height: h });
      }, 'image/png');
    });
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

  // Create an EMPTY project on `address` (no image yet) and link the session, so a
  // later saveToServer() uploads the result. Backs stencil.newEditor({ address }).
  async createRemoteBlank(address) {
    const conn = requireConnection(this.connections, address);
    this.remoteLink = await createRemoteProject(conn, { name: this.imageBaseName || 'Untitled' });
    return this.remoteLink;
  }

  // Save the current annotated result + layout back to the linked server project.
  // On a 409 version conflict, pull latest, union-merge the peer's lines with ours, and
  // retry until convergence (handles the result-upload's extra bump). No-op when local-only.
  async saveToServer() {
    if (!this.remoteLink) return null;
    if (!getSyncToServer()) return null; // sync off — fetched project stays edit-in-memory only
    let conn;
    try {
      conn = requireConnection(this.connections, this.remoteLink.address);
    } catch (err) {
      notify(err.message, 'fail');
      return null;
    }
    const name = this.activeProjectId != null
      ? (this.storage.store.getMeta(this.activeProjectId)?.name || this.imageBaseName || 'Untitled')
      : (this.imageBaseName || 'Untitled');
    const MAX_TRIES = 6;
    for (let attempt = 0; attempt < MAX_TRIES; attempt++) {
      const layout = this.#currentLayoutPayload();
      const bytes = await this.#renderResultBytes();
      this.#lastRemoteSaveAt = Date.now();   // open the echo-suppression window
      try {
        this.remoteLink = await saveRemoteProject(conn, this.remoteLink, {
          name, layout, bytes, ext: 'png', w: this.canvas.width, h: this.canvas.height,
        });
        this.#lastRemoteSaveAt = Date.now();
        this.#filterDirty = false;   // our filter (if any) is now the server's
        notify(attempt === 0 ? 'Saved to server' : 'Merged changes from another editor', 'ok');
        return this.remoteLink;
      } catch (err) {
        if (!err || !err.conflict) {
          notify(`Server save failed — ${err.message}`, 'fail');
          return null;
        }
        // Conflict: a peer saved first. Merge their latest lines into ours, adopt the
        // server version, and loop to retry — re-merging each time so repeated bumps
        // (the unguarded result upload) can't drop our edit.
        try {
          const full = await conn.getProject(this.remoteLink.remoteId);
          this.remoteLink = { ...this.remoteLink, version: full.project?.version ?? this.remoteLink.version };
          const sl = full.layout || {};
          const serverLines = Array.isArray(sl.lines) ? sl.lines : [];
          this.lines = mergeLines(serverLines, this.lines);
          // Adopt the peer's filter UNLESS this user just changed their own — so a
          // line-only edit doesn't clobber a peer's filter change (the scalar can't merge).
          if (!this.#filterDirty) this.#adoptServerFilter(sl);
          this.history.push(this.lines);
          this.renderer.redraw();
        } catch { /* fetch failed; loop retries with current state */ }
      }
    }
    // Couldn't win the race after several merges — reload so the user sees a consistent
    // state (our pending lines were already merged into the server's by an earlier pass).
    notify('Sync conflict — reloaded latest from the server', 'info');
    this.reloadRemoteActive();
    return null;
  }

  // The current editor state as a server layout payload (lines + filter + geometry + page +
  // formulas). Shared by saveToServer / publishIncognitoToServer / replaceProjectImage.
  #currentLayoutPayload() {
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
      await this.saveToServer();   // push the new layout + rendered result
    } catch (err) { notify(`Could not update the server image — ${err.message}`, 'fail'); }
  }

  // Render the annotated result to PNG bytes (the server's `result` blob), or null if no image.
  async #renderResultBytes() {
    if (!this.image) return null;
    const off = this.#renderExportCanvas();
    const blob = await new Promise(res => off.toBlob(res, 'image/png'));
    return blob ? new Uint8Array(await blob.arrayBuffer()) : null;
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
      layout: this.#currentLayoutPayload(),
      bytes: await this.#renderResultBytes(), ext: 'png', w: this.canvas.width, h: this.canvas.height,
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
  // Single source of truth for top-menu settings: toolbar handlers AND the console API
  // (window.stencil) both call these, staying in sync. Each updates model, mirrors UI,
  // redraws/persists as needed, returns `this` for chaining. `persist:false` is used by
  // live-drag (input) events that commit on the trailing change (no write per slider tick).

  setColor(v, { persist = true } = {}) {
    this.color = String(v);
    setVal('line-color', this.color);
    if (persist) this.storage.save();
    return this;
  }

  setThickness(n, { persist = true } = {}) {
    const v = parseInt(n, 10);
    if (Number.isNaN(v)) return this;
    this.thickness = v;
    setVal('line-thickness', v);
    const ctx = document.getElementById('ctx-thickness');
    if (ctx && document.activeElement !== ctx) ctx.value = v;
    this.renderer.redraw();
    if (persist) this.storage.save();
    return this;
  }

  setMarkerSize(n, { persist = true } = {}) {
    const v = parseInt(n, 10);
    if (Number.isNaN(v)) return this;
    this.markerSize = v;
    setVal('marker-size', v);
    const ctx = document.getElementById('ctx-marker-size');
    if (ctx && document.activeElement !== ctx) ctx.value = v;
    this.renderer.redraw();
    if (persist) this.storage.save();
    return this;
  }

  setLineStyle(s) {
    this.style = String(s);
    setVal('line-style', this.style);
    setRadioGroup('ctxLineStyle', this.style);
    this.storage.save();
    return this;
  }

  setShowPoints(b) {
    this.showPoints = !!b;
    const cb = document.getElementById('show-points');
    if (cb) cb.checked = this.showPoints;
    const chk = document.getElementById('ctx-chk-points');
    if (chk) chk.innerHTML = this.showPoints ? icon('check', { size: 14 }) : '';
    this.renderer.redraw();
    this.storage.save();
    return this;
  }

  setShowLines(b) {
    this.showLines = !!b;
    const cb = document.getElementById('show-lines');
    if (cb) cb.checked = this.showLines;
    const chk = document.getElementById('ctx-chk-lines');
    if (chk) chk.innerHTML = this.showLines ? icon('check', { size: 14 }) : '';
    this.renderer.redraw();
    this.storage.save();
    return this;
  }

  setImageFilter(f) {
    this.imageFilter = String(f);
    setVal('image-filter', this.imageFilter);
    const picker = document.getElementById('filter-color');
    if (picker) picker.style.display = this.imageFilter === 'custom' ? 'inline-block' : 'none';
    setRadioGroup('ctxFilter', this.imageFilter);
    const tintRow = document.getElementById('ctx-tint-row');
    if (tintRow) tintRow.classList.toggle('ctx-tint-visible', this.imageFilter === 'custom');
    this.renderer.redraw();
    this.#filterDirty = true;   // user changed the filter → our filter wins on save
    this.storage.save();
    this.scheduleRemoteSync();
    return this;
  }

  setFilterColor(v, { persist = true } = {}) {
    this.filterColor = String(v);
    setVal('filter-color', this.filterColor);
    const ctxTint = document.getElementById('ctx-tint-color');
    if (ctxTint) ctxTint.value = this.filterColor;
    this.renderer.redraw();
    if (persist) { this.#filterDirty = true; this.storage.save(); this.scheduleRemoteSync(); }
    return this;
  }

  setPageSize(size) {
    const n = normalizePageSize(size);
    if (!n) throw new Error(`Unknown page size: ${size} (use 'A3', 'A4', or 'custom')`);
    this.pageSize = n;
    setVal('page-size', n);
    const cg = document.getElementById('custom-size-group');
    if (cg) cg.style.display = n === 'custom' ? 'inline-flex' : 'none';
    this.coordTable.update();
    this.renderer.redraw();
    this.storage.save();
    this.scheduleRemoteSync();   // page format rides the layout — push it to peers/server too
    return this;
  }

  // Width/height are stored in cm (the model unit); the input is shown in the active
  // display unit. Pass cm from the UI handler (it converts the typed value first).
  setCustomPageWidth(cm) {
    const v = parseFloat(cm);
    if (Number.isNaN(v)) return this;
    this.customPageWidth = v;
    setVal('custom-page-width', cmToUnit(v, this.unit));
    this.coordTable.update();
    this.renderer.redraw();
    this.storage.save();
    this.scheduleRemoteSync();
    return this;
  }

  setCustomPageHeight(cm) {
    const v = parseFloat(cm);
    if (Number.isNaN(v)) return this;
    this.customPageHeight = v;
    setVal('custom-page-height', cmToUnit(v, this.unit));
    this.coordTable.update();
    this.renderer.redraw();
    this.storage.save();
    this.scheduleRemoteSync();
    return this;
  }

  setUnit(u) {
    this.unit = u === 'in' ? 'in' : 'cm';
    setVal('unit-select', this.unit);
    this.applyUnitToUI();
    this.coordTable.update();
    this.updateCoordStatus();
    this.renderer.redraw();
    this.storage.save();
    return this;
  }

  // ── Formula controls (shared) ──
  #syncFormulaUI(checked) {
    const fi = document.getElementById('formula-inputs');
    if (fi) fi.style.display = checked ? 'inline-flex' : 'none';
    const ctxFi = document.getElementById('ctx-formula-inputs');
    if (ctxFi) ctxFi.style.display = checked ? 'block' : 'none';
    const ctxCb = document.getElementById('ctx-allow-formulas');
    if (ctxCb) ctxCb.checked = checked;
  }

  #showFormulaError(hasError) {
    const el = document.getElementById('formula-error');
    const ctxEl = document.getElementById('ctx-formula-error');
    if (el) el.style.display = hasError ? 'inline' : 'none';
    if (ctxEl) ctxEl.style.display = hasError ? 'block' : 'none';
  }

  #refreshFormulaCoords() {
    const li = this.coordLineIdx;
    const pts = li === -1
      ? (this.currentLine ? this.currentLine.points : null)
      : (this.lines[li] ? this.lines[li].points : null);
    this.coordTable.update(pts, li);
  }

  setAllowFormulas(b) {
    this.allowFormulas = !!b;
    const cb = document.getElementById('allow-formulas');
    if (cb) cb.checked = this.allowFormulas;
    // Toggling only shows/hides the inputs and gates whether formulas are applied to the
    // coordinate conversion (pixelToPageCoords passes allowFormulas) — the expressions are
    // KEPT so re-enabling restores them.
    this.#syncFormulaUI(this.allowFormulas);
    if (!this.allowFormulas) this.#showFormulaError(false);
    this.#refreshFormulaCoords();
    this.storage.save();
    this.scheduleRemoteSync();   // formulas ride the layout — push them to peers/server too
    return this;
  }

  // Set the x or y coordinate transform. Throws on an invalid expression so the
  // console surfaces it; the UI handler catches and shows the inline error instead.
  setFormula(axis, expr) {
    const a = axis === 'y' ? 'y' : 'x';
    const v = String(expr ?? '').trim();
    if (v && !this.formula.validate(v, a)) throw new Error(`Invalid ${a} formula: ${expr}`);
    if (a === 'x') this.formulaX = v; else this.formulaY = v;
    setVal(`formula-${a}`, v);
    setVal(`ctx-formula-${a}`, v);
    this.#showFormulaError(false);
    this.#refreshFormulaCoords();
    this.storage.save();
    this.scheduleRemoteSync();
    return this;
  }

  // Toggle one tooltip section: key ∈ 'enabled' | 'page' | 'screen' | 'coords'.
  setTooltipOption(key, on) {
    const propMap = { enabled: 'tooltipEnabled', page: 'tooltipShowPage', screen: 'tooltipShowScreen', coords: 'tooltipShowCoords' };
    const idMap = { enabled: 'ctx-tt-enabled', page: 'ctx-tt-page', screen: 'ctx-tt-screen', coords: 'ctx-tt-coords' };
    const prop = propMap[key];
    if (!prop) throw new Error(`Unknown tooltip option: ${key}`);
    this[prop] = !!on;
    const el = document.getElementById(idMap[key]);
    if (el) el.checked = !!on;
    this.storage.save();
    try { this.tooltipMgr?.refresh?.(); } catch { /* tooltip not mounted */ }
    return this;
  }

  // Set one "visual default" colour (shared by the visuals modal + console settings).
  // key ∈ 'fill' | 'selGlow' | 'hoverRing' | 'focusRing'.
  setVisualColor(key, value) {
    const propMap = { fill: 'defaultFillColor', selGlow: 'selGlowColor', hoverRing: 'hoverRingColor', focusRing: 'focusRingColor' };
    const idMap = { fill: 'vs-fill', selGlow: 'vs-sel-glow', hoverRing: 'vs-hover-ring', focusRing: 'vs-focus-ring' };
    const prop = propMap[key];
    if (!prop) throw new Error(`Unknown visual colour: ${key}`);
    this[prop] = String(value);
    setVal(idMap[key], this[prop]);
    this.renderer.redraw();
    this.storage.save();
    return this;
  }

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
    }
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
      bytes, ext, w, h,
    });
    // Push the annotated layout (lines + filter) so the server holds the full project.
    await saveRemoteProject(conn, link, {
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
    return { link, proj, meta };
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
    const blob = await this.#fetchRemoteOriginal(conn, meta.id, src);
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
    const blob = await this.#fetchRemoteOriginal(conn, meta.id, src);
    const dataUrl = blob ? await this.#blobToDataUrl(blob) : null;
    const sl = full.layout || {};
    const newId = this.storage.store.createId();
    const base = full.project?.name || meta.name || 'Untitled';
    const projName = (name && name.trim()) || (copy ? `${base}-copy` : base);
    const localMeta = {
      id: newId,
      name: projName,
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
  #canToggleIncognito() {
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
      btn.disabled = !this.#canToggleIncognito();
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
    if (action === PROJECT_ACTION.UPDATED && id === this.activeProjectId && this.#isIdle())
      this.storage.syncActiveFromStorage();
  }

  // Render the image (with its current filter) plus all visible lines/points onto a
  // fresh full-resolution offscreen canvas. Shared by saveImage / copyImageToClipboard
  // / shareImage so every image action produces the same annotated result.
  #renderExportCanvas() {
    const offscreen = document.createElement('canvas');
    offscreen.width = this.canvas.width;
    offscreen.height = this.canvas.height;
    const ctx = offscreen.getContext('2d');
    // The renderer's draw helpers write to this.ctx; point them at the offscreen
    // ctx for the duration of the export, then restore.
    const savedCtx = this.ctx;
    this.ctx = ctx;
    this.renderer.drawImageWithFilter(ctx);
    if (this.showLines) {
      this.lines.forEach(line => this.renderer.drawLine(line, false));
    } else if (this.showPoints) {
      this.lines.forEach(line => {
        line.points.forEach(p => this.renderer.drawPoint(p, line.color, line.markerSize ?? this.markerSize, false));
      });
    }
    this.ctx = savedCtx;
    return offscreen;
  }

  // Public: the composited "result" canvas (filtered image + drawn lines/points),
  // i.e. what download/copy/share/save-to-server emit. Used for project thumbnails
  // so previews show the EDITED result, not the untouched original.
  renderResultCanvas() { return this.#renderExportCanvas(); }

  saveImage() {
    if (!this.image) {
      notify('No image loaded', 'fail');
      return;
    }
    const offscreen = this.#renderExportCanvas();

    // Download — use original image name if available
    const baseName = this.imageBaseName || 'drawing';
    const ext = this.imageExt      || 'png';
    const mimeMap = { jpg: 'image/jpeg', jpeg: 'image/jpeg', webp: 'image/webp', png: 'image/png' };
    const mime = mimeMap[ext] || 'image/png';
    const outExt = (ext === 'jpg' || ext === 'jpeg') ? 'jpg' : (mimeMap[ext] ? ext : 'png');
    const link = document.createElement('a');
    link.download = `${baseName}-drawing.${outExt}`;
    link.href = offscreen.toDataURL(mime);
    link.click();

    // A server-linked session also writes the annotated result + layout back.
    if (this.remoteLink) this.saveToServer();
  }

  // Share the annotated image via the Web Share API (mobile/PWA). The Share entry
  // points are only shown when supportsShareFiles() is true (see toolbar/contextMenu
  // wiring), so this is reached only where file sharing works; we still guard defensively.
  shareImage() {
    if (!this.image) { notify('No image loaded', 'fail'); return; }
    const off = this.#renderExportCanvas();
    const baseName = this.imageBaseName || 'drawing';
    off.toBlob(blob => {
      if (!blob) { notify('Image encode failed', 'fail'); return; }
      const file = new File([blob], `${baseName}-drawing.png`, { type: 'image/png' });
      if (!(navigator.canShare && navigator.canShare({ files: [file] }))) {
        notify('Sharing not supported on this browser', 'fail');
        return;
      }
      navigator.share({ files: [file], title: `${baseName} — Stencil` })
        .catch(err => { if (err && err.name !== 'AbortError') notify('Share failed', 'fail'); });
    }, 'image/png');
  }

  downloadJSON() {
    if (this.lines.length === 0) {
      notify('No lines to export', 'fail');
      return;
    }

    const data = buildLayoutPayload({
      imageWidth: this.canvas.width,
      imageHeight: this.canvas.height,
      lines: this.lines
    });

    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `${this.imageBaseName || 'drawing'}-layout.json`;
    a.click();
    URL.revokeObjectURL(url);
  }

  uploadJSON(e) {
    const file = e.target.files[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = async event => {
      try {
        const data = JSON.parse(event.target.result);
        await this.#applyValidatedLayout(data, {
          source: 'uploaded JSON',
          cancelMsg: 'Upload canceled',
          successMsg: 'JSON loaded successfully'
        });
      } catch (err) {
        notify('Error loading JSON: ' + err.message, 'fail');
      }
    };
    reader.readAsText(file);
    e.target.value = '';
  }

  // ── Clipboard: copy current image (with active filter) ──
  copyImageToClipboard() {
    if (!this.image) { notify('No image to copy', 'fail'); return; }
    try {
      const off = this.#renderExportCanvas();
      off.toBlob(async blob => {
        if (!blob) { notify('Image encode failed', 'fail'); return; }
        try {
          await navigator.clipboard.write([new ClipboardItem({ 'image/png': blob })]);
          notify('Image copied to clipboard', 'ok');
        } catch (err) {
          notify('Copy failed: ' + (err.message || err), 'fail');
        }
      }, 'image/png');
    } catch (e) {
      notify('Copy failed: ' + e.message, 'fail');
    }
  }

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
    this.#hideSelectionPanels();
    this.saveHistory();
    this.coordTable.update();
    this.renderer.redraw();
    this.updateButtons();
    notify('All lines cleared', 'ok');
  }

  // ── Clipboard: copy layout JSON text ──
  copyLayoutToClipboard() {
    if (!this.lines || this.lines.length === 0) {
      notify('No layout to copy', 'fail');
      return;
    }
    const data = buildLayoutPayload({
      imageWidth: this.canvas.width,
      imageHeight: this.canvas.height,
      lines: this.lines
    });
    const txt = JSON.stringify(data, null, 2);
    navigator.clipboard.writeText(txt).then(
      () => notify('Layout JSON copied', 'ok'),
      err => notify('Copy failed: ' + (err.message || err), 'fail')
    );
  }

  // ── Apply a layout object pasted from the clipboard ──
  async applyPastedLayout(data) {
    await this.#applyValidatedLayout(data, {
      source: 'pasted JSON',
      cancelMsg: 'Layout paste canceled',
      successMsg: 'Layout pasted from clipboard'
    });
  }

  /**
   * Validate a layout payload and, after any needed confirmations, install it
   * as the current lines. Shared by JSON file upload and clipboard paste.
   * @param {object} data - Parsed layout payload (expects a `lines` array).
   * @param {{source: string, cancelMsg: string, successMsg: string}} opts -
   *   `source` names the layout's origin in the replace prompt; `cancelMsg` and
   *   `successMsg` are the toasts shown on cancel and success.
   * @returns {Promise<void>}
   */
  async #applyValidatedLayout(data, { source, cancelMsg, successMsg }) {
    const verdict = validateLayout(data, {
      hasImage: !!this.image,
      imgW: this.canvas.width,
      imgH: this.canvas.height,
      hasExistingLines: !!(this.lines && this.lines.length > 0)
    });
    if (!verdict.ok) {
      notify('Load an image first', 'fail');
      return;
    }
    if (verdict.needsReplaceConfirm && !(await this.confirm(`Replace current layout with ${source}?`, { title: 'Replace layout' }))) {
      notify(cancelMsg, 'fail');
      return;
    }
    if (verdict.needsDimMismatchConfirm && !(await this.confirm('Image dimensions do not match. Continue anyway?', { title: 'Dimension mismatch' }))) {
      notify(cancelMsg, 'fail');
      return;
    }
    this.lines = verdict.lines;
    this.saveHistory();
    this.renderer.redraw();
    this.updateButtons();
    if (this.lines.length > 0) this.coordTable.update(this.lines[this.lines.length - 1].points);
    notify(successMsg, 'ok');
  }
}
