import { setVal, setRadioGroup, cmToUnit } from '../utils.js';
import { icon } from '../ui/icons.js';
import { normalizePageSize } from './units.js';

// ── SettingsController: the shared editor setters ───────────────────
// Extracted from drawingApp.js. Single source of truth for top-menu settings: toolbar
// handlers AND the console API (window.stencil) both reach these through DrawingApp's thin
// delegators, staying in sync. Each updates the model field on the app, mirrors the UI,
// redraws/persists as needed. Holds no state — back-references the app like the other
// collaborators (Renderer/Storage/CoordTable). `persist:false` is used by live-drag (input)
// events that commit on the trailing change (no write per slider tick).
export class SettingsController {
  constructor(app) {
    this.app = app;
  }

  setColor(v, { persist = true } = {}) {
    const app = this.app;
    app.color = String(v);
    setVal('line-color', app.color);
    if (persist) app.storage.save();
  }

  setThickness(n, { persist = true } = {}) {
    const app = this.app;
    const v = parseInt(n, 10);
    if (Number.isNaN(v)) return;
    app.thickness = v;
    setVal('line-thickness', v);
    const ctx = document.getElementById('ctx-thickness');
    if (ctx && document.activeElement !== ctx) ctx.value = v;
    app.renderer.redraw();
    if (persist) app.storage.save();
  }

  setMarkerSize(n, { persist = true } = {}) {
    const app = this.app;
    const v = parseInt(n, 10);
    if (Number.isNaN(v)) return;
    app.markerSize = v;
    setVal('marker-size', v);
    const ctx = document.getElementById('ctx-marker-size');
    if (ctx && document.activeElement !== ctx) ctx.value = v;
    app.renderer.redraw();
    if (persist) app.storage.save();
  }

  setLineStyle(s) {
    const app = this.app;
    app.style = String(s);
    setVal('line-style', app.style);
    setRadioGroup('ctxLineStyle', app.style);
    app.storage.save();
  }

  setShowPoints(b) {
    const app = this.app;
    app.showPoints = !!b;
    const cb = document.getElementById('show-points');
    if (cb) cb.checked = app.showPoints;
    const chk = document.getElementById('ctx-chk-points');
    if (chk) chk.innerHTML = app.showPoints ? icon('check', { size: 14 }) : '';
    app.renderer.redraw();
    app.storage.save();
  }

  setShowLines(b) {
    const app = this.app;
    app.showLines = !!b;
    const cb = document.getElementById('show-lines');
    if (cb) cb.checked = app.showLines;
    const chk = document.getElementById('ctx-chk-lines');
    if (chk) chk.innerHTML = app.showLines ? icon('check', { size: 14 }) : '';
    app.renderer.redraw();
    app.storage.save();
  }

  setImageFilter(f) {
    const app = this.app;
    app.imageFilter = String(f);
    setVal('image-filter', app.imageFilter);
    const picker = document.getElementById('filter-color');
    if (picker) picker.style.display = app.imageFilter === 'custom' ? 'inline-block' : 'none';
    setRadioGroup('ctxFilter', app.imageFilter);
    const tintRow = document.getElementById('ctx-tint-row');
    if (tintRow) tintRow.classList.toggle('ctx-tint-visible', app.imageFilter === 'custom');
    app.renderer.redraw();
    app.filterDirty = true;   // user changed the filter → our filter wins on save
    app.storage.save();
    app.scheduleRemoteSync();
  }

  setFilterColor(v, { persist = true } = {}) {
    const app = this.app;
    app.filterColor = String(v);
    setVal('filter-color', app.filterColor);
    const ctxTint = document.getElementById('ctx-tint-color');
    if (ctxTint) ctxTint.value = app.filterColor;
    app.renderer.redraw();
    if (persist) { app.filterDirty = true; app.storage.save(); app.scheduleRemoteSync(); }
  }

  setPageSize(size) {
    const app = this.app;
    const n = normalizePageSize(size);
    if (!n) throw new Error(`Unknown page size: ${size} (use a named ISO format (A0–C10) or 'custom')`);
    app.pageSize = n;
    setVal('page-size', n);
    const cg = document.getElementById('custom-size-group');
    if (cg) cg.style.display = n === 'custom' ? 'inline-flex' : 'none';
    app.coordTable.update();
    app.renderer.redraw();
    app.storage.save();
    app.scheduleRemoteSync();   // page format rides the layout — push it to peers/server too
  }

  // Width/height are stored in cm (the model unit); the input is shown in the active
  // display unit. Pass cm from the UI handler (it converts the typed value first).
  setCustomPageWidth(cm) {
    const app = this.app;
    const v = parseFloat(cm);
    if (Number.isNaN(v)) return;
    app.customPageWidth = v;
    setVal('custom-page-width', cmToUnit(v, app.unit));
    app.coordTable.update();
    app.renderer.redraw();
    app.storage.save();
    app.scheduleRemoteSync();
  }

  setCustomPageHeight(cm) {
    const app = this.app;
    const v = parseFloat(cm);
    if (Number.isNaN(v)) return;
    app.customPageHeight = v;
    setVal('custom-page-height', cmToUnit(v, app.unit));
    app.coordTable.update();
    app.renderer.redraw();
    app.storage.save();
    app.scheduleRemoteSync();
  }

  setUnit(u) {
    const app = this.app;
    app.unit = u === 'in' ? 'in' : 'cm';
    setVal('unit-select', app.unit);
    app.applyUnitToUI();
    app.coordTable.update();
    app.updateCoordStatus();
    app.renderer.redraw();
    app.storage.save();
  }

  // ── Formula controls (shared with #wireFormulaControls + #adoptServerFormulas) ──
  syncFormulaUI(checked) {
    const fi = document.getElementById('formula-inputs');
    if (fi) fi.style.display = checked ? 'inline-flex' : 'none';
    const ctxFi = document.getElementById('ctx-formula-inputs');
    if (ctxFi) ctxFi.style.display = checked ? 'block' : 'none';
    const ctxCb = document.getElementById('ctx-allow-formulas');
    if (ctxCb) ctxCb.checked = checked;
  }

  showFormulaError(hasError) {
    const el = document.getElementById('formula-error');
    const ctxEl = document.getElementById('ctx-formula-error');
    if (el) el.style.display = hasError ? 'inline' : 'none';
    if (ctxEl) ctxEl.style.display = hasError ? 'block' : 'none';
  }

  refreshFormulaCoords() {
    const app = this.app;
    const li = app.coordLineIdx;
    const pts = li === -1
      ? (app.currentLine ? app.currentLine.points : null)
      : (app.lines[li] ? app.lines[li].points : null);
    app.coordTable.update(pts, li);
  }

  setAllowFormulas(b) {
    const app = this.app;
    app.allowFormulas = !!b;
    const cb = document.getElementById('allow-formulas');
    if (cb) cb.checked = app.allowFormulas;
    // Toggling only shows/hides the inputs and gates whether formulas are applied to the
    // coordinate conversion (pixelToPageCoords passes allowFormulas) — the expressions are
    // KEPT so re-enabling restores them.
    this.syncFormulaUI(app.allowFormulas);
    if (!app.allowFormulas) this.showFormulaError(false);
    this.refreshFormulaCoords();
    app.storage.save();
    app.scheduleRemoteSync();   // formulas ride the layout — push them to peers/server too
  }

  // Set the x or y coordinate transform. Throws on an invalid expression so the
  // console surfaces it; the UI handler catches and shows the inline error instead.
  setFormula(axis, expr) {
    const app = this.app;
    const a = axis === 'y' ? 'y' : 'x';
    const v = String(expr ?? '').trim();
    if (v && !app.formula.validate(v, a)) throw new Error(`Invalid ${a} formula: ${expr}`);
    if (a === 'x') app.formulaX = v; else app.formulaY = v;
    setVal(`formula-${a}`, v);
    setVal(`ctx-formula-${a}`, v);
    this.showFormulaError(false);
    this.refreshFormulaCoords();
    app.storage.save();
    app.scheduleRemoteSync();
  }

  // Toggle one tooltip section: key ∈ 'enabled' | 'page' | 'screen' | 'coords'.
  setTooltipOption(key, on) {
    const app = this.app;
    const propMap = { enabled: 'tooltipEnabled', page: 'tooltipShowPage', screen: 'tooltipShowScreen', coords: 'tooltipShowCoords' };
    const idMap = { enabled: 'ctx-tt-enabled', page: 'ctx-tt-page', screen: 'ctx-tt-screen', coords: 'ctx-tt-coords' };
    const prop = propMap[key];
    if (!prop) throw new Error(`Unknown tooltip option: ${key}`);
    app[prop] = !!on;
    const el = document.getElementById(idMap[key]);
    if (el) el.checked = !!on;
    app.storage.save();
    try { app.tooltipMgr?.refresh?.(); } catch { /* tooltip not mounted */ }
  }

  // Set one "visual default" colour (shared by the visuals modal + console settings).
  // key ∈ 'fill' | 'selGlow' | 'hoverRing' | 'focusRing'.
  setVisualColor(key, value) {
    const app = this.app;
    const propMap = { fill: 'defaultFillColor', selGlow: 'selGlowColor', hoverRing: 'hoverRingColor', focusRing: 'focusRingColor' };
    const idMap = { fill: 'vs-fill', selGlow: 'vs-sel-glow', hoverRing: 'vs-hover-ring', focusRing: 'vs-focus-ring' };
    const prop = propMap[key];
    if (!prop) throw new Error(`Unknown visual colour: ${key}`);
    app[prop] = String(value);
    setVal(idMap[key], app[prop]);
    app.renderer.redraw();
    app.storage.save();
  }
}
