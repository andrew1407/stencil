import { setVal, setRadioGroup, notify, distToSegment, matchHotkey, isTypingTarget } from '../utils.js';
import constants from '../config/constants.json' with { type: 'json' };
import HOTKEY_DEFS from '../config/hotkeysConfig.json' with { type: 'json' };
const { PAGE_SIZES } = constants;
import { HistoryStack } from './historyStack.js';
import { FormulaEngine } from './formulaEngine.js';
import { Renderer } from './renderer.js';
import { Storage } from './storage.js';
import { TabsCoordinator } from './tabsCoordinator.js';
import { PROJECT_ACTION } from '../worker/messages.js';
import { CoordTable } from './coordTable.js';
import { ZoomPan } from './zoomPan.js';
// ── DrawingApp: orchestrator owning state + DOM wiring ──────────
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
  // Arrow-key panning
  #arrowsHeld = new Set();
  #arrowPanRaf = null;
  // Smooth zoom animation state
  #smoothZoom = { target: null, focal: null, rafId: null };
  // Debounce timers
  #thicknessSaveTimer = null;
  #saveStatusTimer = null;
  #rotateSaveTimer = null;

  constructor() {
    this.canvas = document.getElementById('canvas');
    this.ctx = this.canvas.getContext('2d');
    this.tooltip = document.getElementById('tooltip');
    this.coordinatesBody = document.getElementById('coordinatesBody');

    this.image = null;
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

    // ── Drawing mode: 'line' (click points) or 'rect' (drag rectangle) ──
    this.drawMode = 'line';
    this.isRectDrawDragging = false;
    this.rectDrawStart = null; // { imgX, imgY, cssX, cssY }
    this.rectDrawEnd = null;
    this.#rectConnectOnce = false; // one-shot: connect next rect to selection
    // Continuation drawing: when Start is pressed with a line selected, new
    // points/rects extend that line (connecting to its last/focused point)
    // and inherit its style. -1 = drawing a fresh line.
    this.#continueLineIdx = -1;
    this.#continueInsertIdx = -1;

    // ── Hover tracking (for hover ring on any point, Ctrl/Shift tooltip refresh) ──
    this.hoverPt = null;          // { lineIdx, ptIdx } currently hovered on canvas
    this.mouseOverCanvas = false;
    this.lastMouseClientX = 0;
    this.lastMouseClientY = 0;

    // ── Configurable visuals (persisted) ──
    this.selGlowColor = '#ffc800'; // selection highlight glow (lines + points)
    this.hoverRingColor = '#007bff'; // hover ring around points
    this.focusRingColor = '#007bff'; // focused/clicked point ring
    this.defaultFillColor = '#3399ff'; // default fill applied to new locked areas

    // ── Multi-project state ──
    // The active project id mirrors storage.activeId; null = temporary editor.
    this.activeProjectId = null;

    // ── Components ──
    this.history = new HistoryStack();
    this.formula = new FormulaEngine();
    this.renderer = new Renderer(this);
    this.storage = new Storage(this);
    this.tabs = new TabsCoordinator();
    // Another tab changed the project set: sync the editor if it's our project.
    this.tabs.onProjectsChanged(detail => this.#onRemoteProjectsChange(detail || {}));
    this.coordTable = new CoordTable(this);
    // The tooltip is a custom element (<stencil-tooltip>) that owns its render
    // logic; give it the app ref and alias it as tooltipMgr for existing callers.
    this.tooltip.app = this;
    this.tooltipMgr = this.tooltip;
    this.zoomPan = new ZoomPan(this);

    this.initEventListeners();
    // Set a sensible initial viewport height
    const vp = document.getElementById('canvasViewport');
    if (vp) vp.style.maxHeight = Math.max(300, window.innerHeight - 220) + 'px';
    // Boot synchronously into a blank temporary editor (migrate + sweep only).
    // The projects component decides whether to offer a chooser after readiness.
    this.restoreFromLocalStorage();
    this.storage.newTemporary();
  }

  initEventListeners() {
    document.getElementById('imageUpload').addEventListener('change', e => this.loadImage(e));
    document.getElementById('lineColor').addEventListener('change', e => { this.color = e.target.value; this.storage.save(); });
    document.getElementById('lineThickness').addEventListener('input', e => {
      const v = parseInt(e.target.value);
      if (isNaN(v)) return;
      this.thickness = v;
      const ctxEl = document.getElementById('ctx-thickness');
      if (ctxEl && document.activeElement !== ctxEl) ctxEl.value = v;
      this.renderer.redraw();
    });
    document.getElementById('lineThickness').addEventListener('change', e => { this.thickness = parseInt(e.target.value); this.storage.save(); });
    document.getElementById('markerSize').addEventListener('input', e => {
      const v = parseInt(e.target.value);
      if (isNaN(v)) return;
      this.markerSize = v;
      const ctxEl = document.getElementById('ctx-marker-size');
      if (ctxEl && document.activeElement !== ctxEl) ctxEl.value = v;
      this.renderer.redraw();
    });
    document.getElementById('markerSize').addEventListener('change', e => { this.markerSize = parseInt(e.target.value); this.storage.save(); });
    document.getElementById('lineStyle').addEventListener('change', e => {
      this.style = e.target.value;
      setRadioGroup('ctxLineStyle', e.target.value);
      this.storage.save();
    });

    // Selection panel listeners
    document.getElementById('selColor').addEventListener('input', e => this.applySelectionChange('color', e.target.value));
    document.getElementById('selThickness').addEventListener('change', e => this.applySelectionChange('thickness', parseInt(e.target.value)));
    document.getElementById('selMarkerSize').addEventListener('change', e => this.applySelectionChange('markerSize', parseInt(e.target.value)));
    document.getElementById('selStyle').addEventListener('change', e => this.applySelectionChange('style', e.target.value));
    document.getElementById('selFillEnabled').addEventListener('change', () => this.applyFill());
    document.getElementById('selFill').addEventListener('input', () => {
      document.getElementById('selFillEnabled').checked = true;
      this.applyFill();
    });
    document.getElementById('selFillClear').addEventListener('click', () => {
      document.getElementById('selFillEnabled').checked = false;
      this.applyFill();
      notify('Fill cleared (transparent)', 'ok');
    });
    document.getElementById('selDeselect').addEventListener('click', () => this.deselectLine());
    document.getElementById('pageSize').addEventListener('change', e => {
      this.pageSize = e.target.value;
      const cg = document.getElementById('customSizeGroup');
      if (cg) cg.style.display = e.target.value === 'custom' ? 'inline-flex' : 'none';
      this.coordTable.update();
      this.renderer.redraw();
      this.storage.save();
    });
    document.getElementById('customPageWidth').addEventListener('change', e => {
      this.customPageWidth = parseFloat(e.target.value) || 21;
      this.coordTable.update();
      this.storage.save();
    });
    document.getElementById('customPageHeight').addEventListener('change', e => {
      this.customPageHeight = parseFloat(e.target.value) || 29.7;
      this.coordTable.update();
      this.storage.save();
    });
    document.getElementById('showPoints').addEventListener('change', e => {
      this.showPoints = e.target.checked;
      const chk = document.getElementById('ctx-chk-points');
      if (chk) chk.textContent = e.target.checked ? '✓' : '';
      this.renderer.redraw();
      this.storage.save();
    });
    document.getElementById('showLines').addEventListener('change', e => {
      this.showLines = e.target.checked;
      const chk = document.getElementById('ctx-chk-lines');
      if (chk) chk.textContent = e.target.checked ? '✓' : '';
      this.renderer.redraw();
      this.storage.save();
    });
    document.getElementById('imageFilter').addEventListener('change', e => {
      this.imageFilter = e.target.value;
      const filterColorPicker = document.getElementById('filterColor');
      if (filterColorPicker) filterColorPicker.style.display = (e.target.value === 'custom') ? 'inline-block' : 'none';
      // Mirror to ctx filter radios + tint visibility
      setRadioGroup('ctxFilter', e.target.value);
      const tintRow = document.getElementById('ctxTintRow');
      if (tintRow) tintRow.classList.toggle('ctx-tint-visible', e.target.value === 'custom');
      this.renderer.redraw();
      this.storage.save();
    });
    let filterColorTimer = null;
    document.getElementById('filterColor').addEventListener('input', e => {
      this.filterColor = e.target.value;
      const ctxTint = document.getElementById('ctx-tint-color');
      if (ctxTint) ctxTint.value = e.target.value;
      clearTimeout(filterColorTimer);
      filterColorTimer = setTimeout(() => {
        this.renderer.redraw();
        this.storage.save();
      }, 80);
    });

    // ── Formula controls (top bar) ──────────────────────────────
    const syncFormulaUI = checked => {
      document.getElementById('formulaInputs').style.display = checked ? 'inline-flex' : 'none';
      const ctxFi = document.getElementById('ctx-formula-inputs');
      if (ctxFi) ctxFi.style.display = checked ? 'block' : 'none';
      const ctxCb = document.getElementById('ctx-allow-formulas');
      if (ctxCb) ctxCb.checked = checked;
    };
    const showFormulaError = hasError => {
      const el = document.getElementById('formulaError');
      const ctxEl = document.getElementById('ctx-formula-error');
      if (el) el.style.display = hasError ? 'inline' : 'none';
      if (ctxEl) ctxEl.style.display = hasError ? 'block' : 'none';
    };
    const refreshCoordsAfterFormula = () => {
      const li = this.coordLineIdx;
      const pts = li === -1 ? (this.currentLine ? this.currentLine.points : null) : (this.lines[li] ? this.lines[li].points : null);
      this.coordTable.update(pts, li);
    };
    const validateAndApplyFormulas = () => {
      const fxVal = document.getElementById('formulaX').value.trim();
      const fyVal = document.getElementById('formulaY').value.trim();
      const okX = this.formula.validate(fxVal, 'x');
      const okY = this.formula.validate(fyVal, 'y');
      showFormulaError(!okX || !okY);
      if (okX && okY) {
        this.formulaX = fxVal;
        this.formulaY = fyVal;
        const cx = document.getElementById('ctx-formula-x');
        const cy = document.getElementById('ctx-formula-y');
        if (cx) cx.value = fxVal;
        if (cy) cy.value = fyVal;
        refreshCoordsAfterFormula();
        this.storage.save();
      }
    };
    document.getElementById('allowFormulas').addEventListener('change', e => {
      this.allowFormulas = e.target.checked;
      syncFormulaUI(e.target.checked);
      if (!e.target.checked) { this.formulaX = ''; this.formulaY = ''; showFormulaError(false); }
      refreshCoordsAfterFormula();
      this.storage.save();
    });
    document.getElementById('formulaX').addEventListener('input', validateAndApplyFormulas);
    document.getElementById('formulaY').addEventListener('input', validateAndApplyFormulas);
    document.getElementById('startDrawing').addEventListener('click', () => this.startDrawingMode());
    document.getElementById('stopDrawing').addEventListener('click', () => this.stopDrawingMode());
    document.getElementById('drawModeToggle').addEventListener('click', () => {
      this.setDrawMode(this.drawMode === 'rect' ? 'line' : 'rect');
      this.storage.save();
    });
    document.getElementById('undo').addEventListener('click', () => this.undo());
    document.getElementById('redo').addEventListener('click', () => this.redo());
    document.getElementById('downloadJSON').addEventListener('click', () => this.downloadJSON());
    document.getElementById('copyJSONBtn').addEventListener('click', () => this.copyLayoutToClipboard());
    document.getElementById('saveImage').addEventListener('click', () => this.saveImage());
    document.getElementById('uploadJSONBtn').addEventListener('click', () => document.getElementById('uploadJSON').click());
    document.getElementById('uploadJSON').addEventListener('change', e => this.uploadJSON(e));
    document.getElementById('clearStorage').addEventListener('click', () => {
      if (this.storage.temporary || this.activeProjectId == null) {
        // Temporary editor → just clear the editor back to blank.
        if (confirm('Clear this editor (image + lines)?')) {
          this.storage.newTemporary();
          this.tabs.reportActive(null);
          this.showSaveStatus('🗑 Cleared', '#dc3545');
        }
        return;
      }
      if (confirm('Clear this project (image + lines) from storage?')) {
        const id = this.activeProjectId;
        this.storage.store.remove(id);
        this.storage.newTemporary();
        this.tabs.reportActive(null);
        this.tabs.projectsChanged({ id, action: PROJECT_ACTION.REMOVED });
        this.showSaveStatus('🗑 Cleared', '#dc3545');
      }
    });
    const incognitoBtn = document.getElementById('incognitoToggle');
    if (incognitoBtn) incognitoBtn.addEventListener('click', () => {
      if (!this.#canToggleIncognito()) return;
      this.storage.incognito = !this.storage.incognito;
      this.updateIncognitoUI();
      notify(this.storage.incognito
        ? 'Incognito mode — this editor won\'t be saved'
        : 'Incognito off', 'info');
    });
    document.getElementById('clearAllLines').addEventListener('click', () => this.clearAllLines());
    // Zoom buttons: single click = small step, double-click = large step,
    // hold = continuous zoom (kicks in after a short delay)
    this.zoomPan.setupHoldZoom(document.getElementById('zoomIn'), +1);
    this.zoomPan.setupHoldZoom(document.getElementById('zoomOut'), -1);
    document.getElementById('zoomFit').addEventListener('click', () => this.zoomPan.fitToWindow());

    // Manual zoom input
    const zoomInput = document.getElementById('zoomInput');
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

    // Save scroll position (debounced) so it's restored on reopen
    {
      const scrollVp = document.getElementById('canvasViewport');
      if (scrollVp) {
        let scrollSaveTimer = null;
        scrollVp.addEventListener('scroll', () => {
          clearTimeout(scrollSaveTimer);
          scrollSaveTimer = setTimeout(() => this.storage.save(), 400);
        });
      }
    }

    // Theme toggle
    const updateThemeIcon = () => {
      const isDark = document.documentElement.getAttribute('data-theme') === 'dark';
      document.getElementById('themeToggle').textContent = isDark ? '☀️' : '🌙';
    };
    updateThemeIcon();
    document.getElementById('themeToggle').addEventListener('click', () => {
      const isDark = document.documentElement.getAttribute('data-theme') === 'dark';
      const next = isDark ? 'light' : 'dark';
      document.documentElement.setAttribute('data-theme', next);
      localStorage.setItem('drawingApp_theme', next);
      updateThemeIcon();
    });
    // Follow system changes only if user hasn't manually overridden
    window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', e => {
      if (!localStorage.getItem('drawingApp_theme')) {
        const theme = e.matches ? 'dark' : 'light';
        document.documentElement.setAttribute('data-theme', theme);
        updateThemeIcon();
      }
    });

    // Keyboard shortcuts — dispatched via HOTKEYS registry
    const HK_HANDLERS = {
      undo: () => { if (!document.getElementById('undo').disabled) this.undo(); },
      redo: () => { if (!document.getElementById('redo').disabled) this.redo(); },
      startDraw: () => { if (this.image && !this.isDrawing) this.startDrawingMode(); },
      stopDraw: () => { if (this.isDrawing) this.stopDrawingMode(); },
      togglePoints: () => {
        const cb = document.getElementById('showPoints');
        cb.checked = !cb.checked;
        this.showPoints = cb.checked;
        this.renderer.redraw();
      },
      toggleLines: () => {
        const cb = document.getElementById('showLines');
        cb.checked = !cb.checked;
        this.showLines = cb.checked;
        this.renderer.redraw();
      },
      cycleFilter: () => {
        const sel = document.getElementById('imageFilter');
        const opts = ['none', 'bw', 'sepia', 'custom'];
        const cur = opts.indexOf(sel.value);
        sel.value = opts[(cur + 1) % opts.length];
        this.imageFilter = sel.value;
        const filterColorPicker = document.getElementById('filterColor');
        if (filterColorPicker) filterColorPicker.style.display = (this.imageFilter === 'custom') ? 'inline-block' : 'none';
        this.renderer.redraw();
      },
      resetZoom: () => this.zoomPan.fitToWindow(),
      toggleControls: () => { const b = document.getElementById('toggleControls');   if (b) b.click(); },
      togglePointsList: () => { const b = document.getElementById('toggleCoordPanel'); if (b) b.click(); },
      fullscreen: () => toggleFullscreen(),
      zoomIn: () => this.zoomPan.zoomAroundCenter(this.scale + 0.25),
      zoomOut: () => this.zoomPan.zoomAroundCenter(this.scale - 0.25),
      zoomInBig: () => this.zoomPan.zoomAroundCenter(this.scale + 1.0),
      zoomOutBig: () => this.zoomPan.zoomAroundCenter(this.scale - 1.0),
      copyImage: () => this.copyImageToClipboard(),
      copyLayout: () => this.copyLayoutToClipboard(),
      // paste is handled by the native 'paste' event listener below — entry here is for hotkey display only
      paste: () => { /* handled by paste event */ },
      clearAllLines: () => this.clearAllLines()
    };
    document.addEventListener('keydown', e => {
      if (isTypingTarget(e.target)) return;
      for (const def of HOTKEY_DEFS) {
        const combo = HOTKEYS[def.id];
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

    // ── Arrow-key panning ──────────────────────────────────────
    // Plain arrows pan the viewport; holding multiple arrows
    // (e.g. Down+Left) pans diagonally; opposing pairs cancel.
    // Shift accelerates pan. Alt/Ctrl/Meta are reserved for other
    // shortcuts (e.g. Alt+ArrowUp = zoom), so we ignore them here.
    this.#arrowsHeld = new Set();
    this.#arrowPanRaf = null;
    const ARROW_KEYS = ['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'];
    const arrowPanTick = () => {
      const vp = document.getElementById('canvasViewport');
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

    // Document-wide drag-and-drop overlay
    const dropZone = document.getElementById('globalDropOverlay');

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
        alert('Please drop an image or a .json file.');
      }
    });

    // Clipboard paste (Ctrl+V) — handles images and JSON layout text
    document.addEventListener('paste', e => {
      if (isTypingTarget(e.target)) return; // let native paste work in inputs
      const cd = e.clipboardData;
      if (!cd) return;

      // 1) Image takes priority
      for (const item of cd.items) {
        if (item.type && item.type.startsWith('image/')) {
          e.preventDefault();
          if (this.image && !confirm('Replace current image with pasted image?')) {
            notify('Image paste canceled', 'fail');
            return;
          }
          const file = item.getAsFile();
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
        try { data = JSON.parse(text); } catch {}
        if (data && Array.isArray(data.lines)) {
          e.preventDefault();
          this.applyPastedLayout(data);
        }
      }
    });

    this.canvas.addEventListener('click', e => this.canvasClick(e));
    this.canvas.addEventListener('dblclick', e => this.canvasDblClick(e));
    this.canvas.addEventListener('mousemove', e => this.canvasMouseMove(e));
    this.canvas.addEventListener('mouseleave', () => {
      this.mouseOverCanvas = false;
      this.tooltipMgr.hide();
      if (this.hoverPt) { this.hoverPt = null; this.renderer.redraw(); }
    });

    // ── Smooth zoom via rAF ──
    // #smoothZoom holds animation state. Rapid wheel events accumulate
    // into a single rAF loop instead of causing abrupt per-event jumps.
    // IMPORTANT: we add `zoom-no-transition` to the canvas while the rAF
    // runs so the CSS width/height transition doesn't fight the rAF updates
    // (that conflict causes the flicker).
    this.#smoothZoom = { target: null, focal: null, rafId: null };

    const viewport = document.getElementById('canvasViewport');

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

      // Ease towards target (~0.22 per frame ≈ finishes in 12 frames at 60fps)
      const next = oldScale + diff * 0.22;
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

      // Shift held → 3× larger zoom step
      const step = e.shiftKey ? 0.3 : 0.1;
      const delta = e.deltaY > 0 ? -step : step;
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
          const line = this.lines[nearSeg.lineIdx];
          this.isDraggingSegment = true;
          this.#draggingSegment = {
            lineIdx: nearSeg.lineIdx,
            ptIdx1: nearSeg.ptIdx1,
            ptIdx2: nearSeg.ptIdx2,
            startX: x,
            startY: y,
            origPt1: { x: line.points[nearSeg.ptIdx1].x, y: line.points[nearSeg.ptIdx1].y },
            origPt2: { x: line.points[nearSeg.ptIdx2].x, y: line.points[nearSeg.ptIdx2].y },
            // Full snapshot so Shift mid-drag can translate the whole line shape.
            origPoints: line.points.map(p => ({ x: p.x, y: p.y }))
          };
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
        const dp = this.#draggingPoint;
        const line = dp.lineIdx === -1 ? this.currentLine : this.lines[dp.lineIdx];
        if (line) {
          line.points[dp.ptIdx].x = x;
          line.points[dp.ptIdx].y = y;
          this.renderer.redraw();
          this.coordTable.refreshCoordRow(dp.ptIdx);
        }
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
            const vp = document.getElementById('canvasViewport');
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
        this.isDraggingPoint = false;
        if (this.#draggingPoint && this.#draggingPoint.lineIdx !== -1) this.saveHistory();
        this.#draggingPoint = null;
        this.#finishDragGesture(e.altKey);
        return;
      }

      // Finish segment drag
      if (this.isDraggingSegment) {
        this.isDraggingSegment = false;
        this.#draggingSegment = null;
        this.saveHistory();
        this.#finishDragGesture(e.altKey);
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

  loadImageFromFile(file) {
    // A temporary editor receiving its first image becomes a real project, so
    // the final storage.save() below persists it (and subsequent tabs see it).
    // Incognito editors are the exception: they deliberately stay unsaved, so
    // we do NOT promote — the image/lines live in memory only.
    if ((this.storage.temporary || this.activeProjectId == null) && !this.storage.incognito) {
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

    const reader = new FileReader();
    reader.onload = event => {
      this.image = new Image();
      this.image.onload = () => {
        this.canvas.width = this.image.width;
        this.canvas.height = this.image.height;

        // If pending lines exist from a previous session where image couldn't be stored,
        // apply them automatically when user re-uploads an image of matching size
        if (this.pendingLines && this.pendingLines.length > 0) {
          const ps = this.pendingImageSize;
          if (!ps || (ps.w === this.image.width && ps.h === this.image.height)) {
            this.lines = this.pendingLines;
            this.pendingLines = null;
            this.pendingImageSize = null;
            this.storage.showImageMissingBanner(false);
            this.showSaveStatus('✓ Drawing restored!', '#28a745');
          } else {
            if (confirm(`Saved drawing was for a ${ps.w}×${ps.h} image but this image is ${this.image.width}×${this.image.height}. Apply saved lines anyway?`))
              this.lines = this.pendingLines;
            this.pendingLines = null;
            this.pendingImageSize = null;
            this.storage.showImageMissingBanner(false);
          }
        } else {
          this.lines = [];
        }

        this.currentLine = null;
        this.history.reset(this.lines);
        this.zoomPan.fitToWindow();
        this.updateInfo();
        this.coordTable.update(this.lines.length > 0 ? this.lines[this.lines.length - 1].points : null);
        this.renderer.redraw();
        this.updateButtons();
        this.storage.save();
      };
      this.image.src = event.target.result;
      // Store base64 for persistence
      this.imageDataUrl = event.target.result;
    };
    reader.readAsDataURL(file);
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
      alert('Please upload an image first!');
      return;
    }
    this.isDrawing = true;

    // Continuation: a line is selected → connect the new drawing to it and
    // adopt its style (new points/rects become part of the selected line).
    if (opts.connect !== false && this.selectedLineIdx >= 0 && this.lines[this.selectedLineIdx]) {
      this.#continueLineIdx = this.selectedLineIdx;
      const line = this.lines[this.#continueLineIdx];
      this.#continueInsertIdx =
        (this.coordLineIdx === this.selectedLineIdx && this.focusedPtIdx >= 0)
          ? this.focusedPtIdx + 1 : line.points.length;
      this.currentLine = null;
      this.undonePoints = [];
      document.getElementById('startDrawing').classList.add('active');
      document.getElementById('stopDrawing').disabled = false;
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
      document.getElementById('selectionPanel').style.display = 'none';
      const fsPanel = document.getElementById('fs-selection-panel');
      if (fsPanel) fsPanel.style.display = 'none';
    }
    this.undonePoints = []; // stack for redo while drawing
    document.getElementById('startDrawing').classList.add('active');
    document.getElementById('stopDrawing').disabled = false;
    this.updateButtons();
    this.renderer.redraw();
  }

  // Switch between polyline ('line') and rectangle ('rect') drawing.
  setDrawMode(mode) {
    this.drawMode = (mode === 'rect') ? 'rect' : 'line';
    this.syncDrawModeUI();
  }

  syncDrawModeUI() {
    const btn = document.getElementById('drawModeToggle');
    if (btn) {
      btn.textContent = this.drawMode === 'rect' ? '▭ Rect' : '╱ Line';
      btn.title = this.drawMode === 'rect'
        ? 'Drawing mode: Rectangle (click to switch to Line)'
        : 'Drawing mode: Line (click to switch to Rectangle)';
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
      document.getElementById('startDrawing').classList.remove('active');
      document.getElementById('stopDrawing').disabled = true;
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
    document.getElementById('startDrawing').classList.remove('active');
    document.getElementById('stopDrawing').disabled = true;
    this.renderer.redraw();
    this.updateButtons();
  }

  canvasClick(e) {
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
        if (line.points.length >= 3) {
          const p0 = line.points[0];
          const closeThresh = (line.markerSize ?? this.markerSize) + 8;
          if (Math.hypot(p0.x - x, p0.y - y) <= closeThresh) {
            this.#closeContinuedShape();
            return;
          }
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
      if (pts.length >= 3) {
        const p0 = pts[0];
        const closeThresh = (this.currentLine.markerSize ?? this.markerSize) + 8;
        if (Math.hypot(p0.x - x, p0.y - y) <= closeThresh) {
          this.#closeCurrentShape();
          return;
        }
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
    const line = this.currentLine;
    if (!line || line.points.length < 3) return;
    // Append a closing point coincident with the first, then lock it
    line.points.push({ x: line.points[0].x, y: line.points[0].y });
    line.locked = true;
    if (line.fillColor === undefined) line.fillColor = 'transparent';
    this.lines.push(line);
    const idx = this.lines.length - 1;
    this.currentLine = null;
    this.isDrawing = false;
    document.getElementById('startDrawing').classList.remove('active');
    document.getElementById('stopDrawing').disabled = true;
    // Select the new area so its fill control appears
    this.selectedLineIdx = idx;
    this.coordLineIdx = idx;
    this.focusedPtIdx = -1;
    this.showSelectionPanel(this.lines[idx]);
    this.coordTable.update(this.lines[idx].points, idx);
    this.saveHistory();
    this.renderer.redraw();
    this.updateButtons();
    notify('Shape closed — locked area created', 'ok');
  }

  // Close a line that is being extended (continuation drawing) into a locked area.
  #closeContinuedShape() {
    const li = this.#continueLineIdx;
    const line = this.lines[li];
    if (!line || line.points.length < 3) return;
    line.points.push({ x: line.points[0].x, y: line.points[0].y });
    line.locked = true;
    if (line.fillColor === undefined) line.fillColor = 'transparent';
    this.#continueLineIdx = -1;
    this.#continueInsertIdx = -1;
    this.currentLine = null;
    this.isDrawing = false;
    document.getElementById('startDrawing').classList.remove('active');
    document.getElementById('stopDrawing').disabled = true;
    this.selectedLineIdx = li;
    this.coordLineIdx = li;
    this.focusedPtIdx = -1;
    this.showSelectionPanel(line);
    this.coordTable.update(line.points, li);
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
      let insertIdx;
      if (this.coordLineIdx === this.selectedLineIdx && this.focusedPtIdx >= 0) {
        insertIdx = this.focusedPtIdx + 1; // connect to the selected point
      } else {
        insertIdx = line.points.length;     // connect to the last point
      }
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
      let insertIdx = (this.coordLineIdx === this.selectedLineIdx && this.focusedPtIdx >= 0)
        ? this.focusedPtIdx + 1 : line.points.length;
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

    // While panning or dragging point, don't update tooltip or cursor here
    if (this.isPanning || this.isDraggingPoint) return;

    const { x, y } = this.canvasCoords(e.clientX, e.clientY);

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
    document.getElementById('selColor').value = line.color;
    document.getElementById('selThickness').value = line.thickness;
    document.getElementById('selMarkerSize').value = line.markerSize ?? this.markerSize;
    document.getElementById('selStyle').value = line.style;
    // Fill control appears only for locked areas
    const fillGroup = document.getElementById('selFillGroup');
    if (fillGroup) {
      if (line.locked) {
        fillGroup.style.display = 'flex';
        const hasFill = line.fillColor && line.fillColor !== 'transparent';
        document.getElementById('selFillEnabled').checked = !!hasFill;
        document.getElementById('selFill').value = hasFill ? line.fillColor : (this.defaultFillColor || '#3399ff');
      } else {
        fillGroup.style.display = 'none';
      }
    }
    document.getElementById('selectionPanel').style.display = 'block';
    // Sync fullscreen overlay panel
    this.syncFsSelectionPanel(line);
  }

  // Apply the locked-area fill from the selection panel controls.
  applyFill() {
    if (this.selectedLineIdx === -1) return;
    const line = this.lines[this.selectedLineIdx];
    if (!line) return;
    const enabled = document.getElementById('selFillEnabled').checked;
    const color = document.getElementById('selFill').value;
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
    fsPanel.innerHTML = `<div class="selection-panel-inner">
            <span class="selection-label">✏️ Selected Line:</span>
            <div class="control-group"><label>Color:</label>
                <input type="color" id="fsSel-color" value="${line.color}" style="width:60px;height:34px;cursor:pointer;border:1px solid var(--border-main);border-radius:4px;"></div>
            <div class="control-group"><label>Thickness:</label>
                <input type="number" id="fsSel-thickness" value="${line.thickness}" min="1" max="20" style="width:70px;background:var(--input-bg);color:var(--input-text);border:1px solid var(--border-main);border-radius:4px;padding:6px 8px;font-size:14px;"></div>
            <div class="control-group"><label>Marker Size:</label>
                <input type="number" id="fsSel-markerSize" value="${line.markerSize ?? this.markerSize}" min="1" max="30" style="width:70px;background:var(--input-bg);color:var(--input-text);border:1px solid var(--border-main);border-radius:4px;padding:6px 8px;font-size:14px;"></div>
            <div class="control-group"><label>Style:</label>
                <select id="fsSel-style" style="background:var(--input-bg);color:var(--input-text);border:1px solid var(--border-main);border-radius:4px;padding:6px 8px;font-size:14px;">
                    <option value="solid"${line.style==='solid'?' selected':''}>Solid</option>
                    <option value="dashed"${line.style==='dashed'?' selected':''}>Dashed</option>
                    <option value="dotted"${line.style==='dotted'?' selected':''}>Dotted</option>
                </select></div>
            ${line.locked ? `<div class="control-group"><label>Fill:</label>
                <input type="checkbox" id="fsSel-fillEnabled"${(line.fillColor && line.fillColor!=='transparent')?' checked':''} style="vertical-align:middle;">
                <input type="color" id="fsSel-fill" value="${(line.fillColor && line.fillColor!=='transparent') ? line.fillColor : (this.defaultFillColor||'#3399ff')}" style="width:60px;height:34px;cursor:pointer;border:1px solid var(--border-main);border-radius:4px;">
                <button id="fsSel-fillClear" type="button" title="Clear fill (make transparent)" style="background:#e67e22;color:#fff;border:none;padding:6px 10px;border-radius:4px;cursor:pointer;font-size:13px;">✕</button></div>` : ''}
            <button id="fsSel-deselect" style="background:#e67e22;color:#fff;border:none;padding:6px 12px;border-radius:4px;cursor:pointer;font-size:13px;">✕ Deselect</button>
        </div>`;
    // Wire events
    fsPanel.querySelector('#fsSel-color').addEventListener('input', e => {
      this.applySelectionChange('color', e.target.value);
      document.getElementById('selColor').value = e.target.value;
    });
    fsPanel.querySelector('#fsSel-thickness').addEventListener('change', e => {
      this.applySelectionChange('thickness', parseInt(e.target.value));
      document.getElementById('selThickness').value = e.target.value;
    });
    fsPanel.querySelector('#fsSel-markerSize').addEventListener('change', e => {
      this.applySelectionChange('markerSize', parseInt(e.target.value));
      document.getElementById('selMarkerSize').value = e.target.value;
    });
    fsPanel.querySelector('#fsSel-style').addEventListener('change', e => {
      this.applySelectionChange('style', e.target.value);
      document.getElementById('selStyle').value = e.target.value;
    });
    const fsFillEnabled = fsPanel.querySelector('#fsSel-fillEnabled');
    const fsFill = fsPanel.querySelector('#fsSel-fill');
    if (fsFillEnabled && fsFill) {
      const applyFsFill = () => {
        if (this.selectedLineIdx === -1) return;
        const ln = this.lines[this.selectedLineIdx];
        if (!ln) return;
        ln.fillColor = fsFillEnabled.checked ? fsFill.value : 'transparent';
        const mainEnabled = document.getElementById('selFillEnabled');
        const mainFill = document.getElementById('selFill');
        if (mainEnabled) mainEnabled.checked = fsFillEnabled.checked;
        if (mainFill) mainFill.value = fsFill.value;
        this.saveHistory(); this.renderer.redraw(); this.storage.save();
      };
      fsFillEnabled.addEventListener('change', applyFsFill);
      fsFill.addEventListener('input', () => { fsFillEnabled.checked = true; applyFsFill(); });
      const fsFillClear = fsPanel.querySelector('#fsSel-fillClear');
      if (fsFillClear) fsFillClear.addEventListener('click', () => {
        fsFillEnabled.checked = false; applyFsFill();
        notify('Fill cleared (transparent)', 'ok');
      });
    }
    fsPanel.querySelector('#fsSel-deselect').addEventListener('click', () => this.deselectLine());
  }

  deselectLine(redraw = true) {
    this.selectedLineIdx = -1;
    this.coordLineIdx = -1;
    this.hoveredPtIdx = -1;
    this.focusedPtIdx = -1;
    document.getElementById('selectionPanel').style.display = 'none';
    const fsPanel = document.getElementById('fs-selection-panel');
    if (fsPanel) {
      fsPanel.style.display = 'none';
      const trigger = document.getElementById('fs-top-trigger');
      if (trigger) trigger.style.height = '8px';
    }
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

  // Apply the active segment/whole-line drag at the given cursor position.
  // `shiftKey` decides the mode live: held → translate the entire line shape;
  // released → move only the grabbed segment's two endpoints. Both modes derive
  // from the original snapshot, so toggling Shift mid-drag never accumulates.
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

  getPageDimensions() {
    if (this.pageSize === 'custom') return { width: this.customPageWidth, height: this.customPageHeight };
    const ps = { ...PAGE_SIZES[this.pageSize] };
    // Swap to landscape if image is wider than tall
    if (this.canvas.width > this.canvas.height) return { width: ps.height, height: ps.width };
    return ps;
  }

  pixelToPageCoords(x, y) {
    const ps = this.getPageDimensions();
    const rawX = (ps.width  / this.canvas.width)  * x;
    const rawY = (ps.height / this.canvas.height) * y;
    return {
      x: this.formula.apply(this.formulaX, 'x', rawX, this.allowFormulas),
      y: this.formula.apply(this.formulaY, 'y', rawY, this.allowFormulas)
    };
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
      setVal('selThickness', newT);
      setVal('fsSel-thickness', newT);
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
    const cos = Math.cos(angle);
    const sin = Math.sin(angle);
    line.points.forEach(p => {
      const dx = p.x - cx;
      const dy = p.y - cy;
      p.x = cx + dx * cos - dy * sin;
      p.y = cy + dx * sin + dy * cos;
    });
    this.renderer.redraw();
    if (this.coordLineIdx === this.selectedLineIdx) this.coordTable.update(line.points, this.selectedLineIdx);
    clearTimeout(this.#rotateSaveTimer);
    this.#rotateSaveTimer = setTimeout(() => { this.saveHistory(); this.storage.save(); }, 280);
  }

  saveHistory() {
    this.history.push(this.lines);
    this.storage.save();
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
    if (this.isDrawing && this.currentLine) {
      document.getElementById('undo').disabled = this.currentLine.points.length === 0;
      document.getElementById('redo').disabled = !this.undonePoints || this.undonePoints.length === 0;
    } else {
      document.getElementById('undo').disabled = !this.history.canUndo();
      document.getElementById('redo').disabled = !this.history.canRedo();
    }
    this.updateIncognitoUI();
  }

  updateInfo() {
    const info = document.getElementById('imageInfo');
    const sizeDisplay = document.getElementById('imageSizeDisplay');
    if (this.image) {
      info.textContent = `Image Size: ${this.canvas.width} × ${this.canvas.height} px  |  Zoom: Ctrl+Scroll · Alt+± · +/− btn  (+Shift = larger)  |  Alt+Scroll: thickness  |  Ctrl+Shift+Scroll: rotate selected  |  Ctrl+Click: add point  |  ℹ for full help`;
      if (sizeDisplay) {
        sizeDisplay.textContent = `📐 ${this.canvas.width} × ${this.canvas.height} px`;
        sizeDisplay.style.display = 'inline-block';
      }
    } else {
      info.textContent = 'No image loaded. Upload an image to start.';
      if (sizeDisplay) sizeDisplay.style.display = 'none';
    }
  }

  showSaveStatus(msg, color) {
    const el = document.getElementById('saveStatus');
    if (!el) return;
    el.textContent = msg;
    el.style.color = color;
    clearTimeout(this.#saveStatusTimer);
    this.#saveStatusTimer = setTimeout(() => { el.textContent = ''; }, 3000);
  }

  restoreFromLocalStorage() {
    this.storage.restore();
  }

  // ── Multi-project navigation (called by the projects modal) ──────
  // Switch the editor to a saved project, persisting the current one first.
  switchToProject(id) {
    if (id === this.activeProjectId) return;
    if (!this.storage.temporary && this.activeProjectId != null) this.storage.save();
    if (this.storage.loadProject(id)) {
      this.activeProjectId = id;
      this.tabs.reportActive(id);
    }
  }

  // Start a fresh blank (unsaved) editor.
  newEditor() {
    this.storage.newTemporary();
    this.tabs.reportActive(null);
  }

  // Permanently delete every saved project, then drop to a blank editor.
  clearAllProjects() {
    this.storage.store.clearAll();
    this.storage.newTemporary();
    this.tabs.reportActive(null);
    this.tabs.projectsChanged({ action: PROJECT_ACTION.CLEARED });
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
    const btn = document.getElementById('incognitoToggle');
    if (btn) {
      btn.disabled = !this.#canToggleIncognito();
      btn.classList.toggle('active', this.storage.incognito);
    }
    document.body.classList.toggle('incognito-mode', this.storage.incognito);
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
    if (action === PROJECT_ACTION.UPDATED && id === this.activeProjectId && this.#isIdle())
      this.storage.syncActiveFromStorage();
  }

  saveImage() {
    if (!this.image) {
      alert('No image loaded!');
      return;
    }
    // Render everything onto an offscreen canvas at full resolution
    const offscreen = document.createElement('canvas');
    offscreen.width = this.canvas.width;
    offscreen.height = this.canvas.height;
    const ctx = offscreen.getContext('2d');

    // Draw image with current filter, then render lines/points via main draw helpers
    const savedCtx = this.ctx;
    this.ctx = ctx;
    this.renderer.drawImageWithFilter(ctx);
    if (this.showLines) {
      this.lines.forEach((line, i) => this.renderer.drawLine(line, false));
    } else if (this.showPoints) {
      this.lines.forEach(line => {
        line.points.forEach(p => this.renderer.drawPoint(p, line.color, line.markerSize ?? this.markerSize, false));
      });
    }
    this.ctx = savedCtx;

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
  }

  downloadJSON() {
    if (this.lines.length === 0) {
      alert('No lines to export!');
      return;
    }

    const data = {
      imageWidth: this.canvas.width,
      imageHeight: this.canvas.height,
      lines: this.lines
    };

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
    reader.onload = event => {
      try {
        const data = JSON.parse(event.target.result);

        if (!this.image) {
          notify('Load an image first', 'fail');
          return;
        }
        if (this.lines && this.lines.length > 0) {
          if (!confirm('Replace current layout with uploaded JSON?')) {
            notify('Upload canceled', 'fail');
            return;
          }
        }
        if (data.imageWidth !== this.canvas.width || data.imageHeight !== this.canvas.height) {
          if (!confirm('Image dimensions do not match. Continue anyway?')) {
            notify('Upload canceled', 'fail');
            return;
          }
        }

        this.lines = data.lines || [];
        this.saveHistory();
        this.renderer.redraw();
        this.updateButtons();
        if (this.lines.length > 0) this.coordTable.update(this.lines[this.lines.length - 1].points);
        notify('JSON loaded successfully', 'ok');
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
      const off = document.createElement('canvas');
      off.width = this.canvas.width;
      off.height = this.canvas.height;
      this.renderer.drawImageWithFilter(off.getContext('2d'));
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
  clearAllLines() {
    if ((!this.lines || this.lines.length === 0) && (!this.currentLine || this.currentLine.points.length === 0)) {
      notify('No lines to clear', 'info');
      return;
    }
    if (!confirm('Wipe ALL lines from the canvas? This cannot be undone except via Undo.')) {
      notify('Clear canceled', 'fail');
      return;
    }
    this.lines = [];
    if (this.currentLine) this.currentLine.points = [];
    this.selectedLineIdx = -1;
    this.coordLineIdx = -1;
    this.focusedPtIdx = -1;
    this.hoveredPtIdx = -1;
    document.getElementById('selectionPanel').style.display = 'none';
    const fsPanel = document.getElementById('fs-selection-panel');
    if (fsPanel) fsPanel.style.display = 'none';
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
    const data = {
      imageWidth: this.canvas.width,
      imageHeight: this.canvas.height,
      lines: this.lines
    };
    const txt = JSON.stringify(data, null, 2);
    navigator.clipboard.writeText(txt).then(
      () => notify('Layout JSON copied', 'ok'),
      err => notify('Copy failed: ' + (err.message || err), 'fail')
    );
  }

  // ── Apply a layout object pasted from the clipboard ──
  applyPastedLayout(data) {
    if (!this.image) {
      notify('Load an image first', 'fail');
      return;
    }
    if (this.lines && this.lines.length > 0) {
      if (!confirm('Replace current layout with pasted JSON?')) {
        notify('Layout paste canceled', 'fail');
        return;
      }
    }
    if (data.imageWidth !== this.canvas.width || data.imageHeight !== this.canvas.height) {
      if (!confirm('Image dimensions do not match. Continue anyway?')) {
        notify('Layout paste canceled', 'fail');
        return;
      }
    }
    this.lines = data.lines || [];
    this.saveHistory();
    this.renderer.redraw();
    this.updateButtons();
    if (this.lines.length > 0) this.coordTable.update(this.lines[this.lines.length - 1].points);
    notify('Layout pasted from clipboard', 'ok');
  }
}
