import { setVal, setRadioGroup, cmToUnit } from '../utils.js';
import { icon } from '../ui/icons.js';
import { normalizePageSize } from './units.js';

// ── Mirror kinds: how one bound DOM element reflects a setting value ──
// Each migrated setting lists its bound elements as { id, kind }; applyMirror writes the
// value the way that element expects. Tolerant of missing elements (same guards the old
// per-setter bodies had). 'valueSkipFocus' leaves the element alone while the user is typing
// in it (the ctx-* number twins), matching the old `document.activeElement !== ctx` guard.
// 'radio' addresses a *group* by name (its `id` is the input[name] of a ctx radio group,
// not an element id) and checks the member whose value matches — see setRadioGroup.
const applyMirror = ({ id, kind }, value) => {
  if (kind === 'radio') { setRadioGroup(id, value); return; }
  const el = document.getElementById(id);
  if (!el) return;
  switch (kind) {
    case 'value': el.value = value; break;
    case 'valueSkipFocus': if (document.activeElement !== el) el.value = value; break;
    case 'checked': el.checked = value; break;
    case 'checkIcon': el.innerHTML = value ? icon('check', { size: 14 }) : ''; break;
  }
};

// ── The observer-driven setting registry ─────────────────────────────
// One descriptor per simple setting collapses the near-identical setter bodies (write the
// model field → mirror the bound DOM twins → redraw/persist/sync) into data. `set(key, ...)`
// drives it. Each entry:
//   • field       — the app model field to write.
//   • parse        — normalize the raw value; returning undefined ABORTS (the int NaN guard).
//   • mirror       — bound DOM elements to reflect into ({ id, kind }); see applyMirror.
//   • afterSet     — irreducible per-setter UI side-effects that aren't a plain value mirror
//                    (conditional display toggles, the coord table, applyUnitToUI, the formula
//                    inputs). Receives the controller; runs after the mirrors, before redraw.
//   • redraw       — repaint the canvas (always, even on persist:false live-drag).
//   • save/remoteSync/filterDirty — commit side-effects, all gated behind `persist`.
// The public setter methods below stay as thin wrappers so callers/console API are unchanged.
const SETTINGS = {
  color: {
    field: 'color', parse: v => String(v),
    mirror: [{ id: 'line-color', kind: 'value' }],
    save: true,
  },
  thickness: {
    field: 'thickness', parse: n => { const v = parseInt(n, 10); return Number.isNaN(v) ? undefined : v; },
    mirror: [{ id: 'line-thickness', kind: 'value' }, { id: 'ctx-thickness', kind: 'valueSkipFocus' }],
    redraw: true, save: true,
  },
  markerSize: {
    field: 'markerSize', parse: n => { const v = parseInt(n, 10); return Number.isNaN(v) ? undefined : v; },
    mirror: [{ id: 'marker-size', kind: 'value' }, { id: 'ctx-marker-size', kind: 'valueSkipFocus' }],
    redraw: true, save: true,
  },
  filterColor: {
    field: 'filterColor', parse: v => String(v),
    mirror: [{ id: 'filter-color', kind: 'value' }, { id: 'ctx-tint-color', kind: 'value' }],
    redraw: true, save: true, remoteSync: true, filterDirty: true,
  },
  showPoints: {
    field: 'showPoints', parse: b => !!b,
    mirror: [{ id: 'show-points', kind: 'checked' }, { id: 'ctx-chk-points', kind: 'checkIcon' }],
    redraw: true, save: true,
  },
  showLines: {
    field: 'showLines', parse: b => !!b,
    mirror: [{ id: 'show-lines', kind: 'checked' }, { id: 'ctx-chk-lines', kind: 'checkIcon' }],
    redraw: true, save: true,
  },
  style: {
    field: 'style', parse: v => String(v),
    mirror: [{ id: 'line-style', kind: 'value' }, { id: 'ctxLineStyle', kind: 'radio' }],
    save: true,
  },
  imageFilter: {
    field: 'imageFilter', parse: v => String(v),
    mirror: [{ id: 'image-filter', kind: 'value' }, { id: 'ctxFilter', kind: 'radio' }],
    afterSet: (self) => {
      const app = self.app;
      const picker = document.getElementById('filter-color');
      if (picker) picker.style.display = app.imageFilter === 'custom' ? 'inline-block' : 'none';
      const tintRow = document.getElementById('ctx-tint-row');
      if (tintRow) tintRow.classList.toggle('ctx-tint-visible', app.imageFilter === 'custom');
    },
    redraw: true, save: true, remoteSync: true, filterDirty: true,
  },
  pageSize: {
    field: 'pageSize',
    parse: v => {
      const n = normalizePageSize(v);
      if (!n) throw new Error(`Unknown page size: ${v} (use a named ISO format (A0–C10) or 'custom')`);
      return n;
    },
    mirror: [{ id: 'page-size', kind: 'value' }],
    afterSet: (self) => {
      const app = self.app;
      const cg = document.getElementById('custom-size-group');
      if (cg) cg.style.display = app.pageSize === 'custom' ? 'inline-flex' : 'none';
      app.coordTable.update();
    },
    redraw: true, save: true, remoteSync: true,   // page format rides the layout — push it to peers/server too
  },
  // Width/height are stored in cm (the model unit) but shown in the active display unit, so
  // the mirror is a converted value (cmToUnit) rather than the raw field — done in afterSet.
  customPageWidth: {
    field: 'customPageWidth', parse: n => { const v = parseFloat(n); return Number.isNaN(v) ? undefined : v; },
    afterSet: (self) => {
      const app = self.app;
      setVal('custom-page-width', cmToUnit(app.customPageWidth, app.unit));
      app.coordTable.update();
    },
    redraw: true, save: true, remoteSync: true,
  },
  customPageHeight: {
    field: 'customPageHeight', parse: n => { const v = parseFloat(n); return Number.isNaN(v) ? undefined : v; },
    afterSet: (self) => {
      const app = self.app;
      setVal('custom-page-height', cmToUnit(app.customPageHeight, app.unit));
      app.coordTable.update();
    },
    redraw: true, save: true, remoteSync: true,
  },
  unit: {
    field: 'unit', parse: u => (u === 'in' ? 'in' : 'cm'),
    mirror: [{ id: 'unit-select', kind: 'value' }],
    afterSet: (self) => {
      const app = self.app;
      app.applyUnitToUI();
      app.coordTable.update();
      app.updateCoordStatus();
    },
    redraw: true, save: true,
  },
  allowFormulas: {
    field: 'allowFormulas', parse: b => !!b,
    mirror: [{ id: 'allow-formulas', kind: 'checked' }],
    // Toggling only shows/hides the inputs and gates whether formulas are applied to the
    // coordinate conversion — the expressions are KEPT so re-enabling restores them.
    afterSet: (self) => {
      const app = self.app;
      self.syncFormulaUI(app.allowFormulas);
      if (!app.allowFormulas) self.showFormulaError(false);
      self.refreshFormulaCoords();
    },
    save: true, remoteSync: true,   // formulas ride the layout — push them to peers/server too
  },
};

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

  // Registry-driven setter shared by the simple settings above. Writes the model field,
  // mirrors every bound element, then runs the declared commit (redraw always; save /
  // remoteSync / filterDirty only when persisting).
  set(key, value, { persist = true } = {}) {
    const d = SETTINGS[key];
    if (!d) throw new Error(`Unknown setting: ${key}`);
    const app = this.app;
    const v = d.parse ? d.parse(value) : value;
    if (v === undefined) return;          // parse aborted (e.g. NaN) — no-op, matches old guards
    app[d.field] = v;
    for (const m of d.mirror || []) applyMirror(m, v);
    if (d.afterSet) d.afterSet(this, v);
    if (d.redraw) app.renderer.redraw();
    if (persist) {
      if (d.filterDirty) app.filterDirty = true;   // user changed the filter → our filter wins on save
      if (d.save) app.storage.save();
      if (d.remoteSync) app.remoteSync.scheduleRemoteSync();
    }
  }

  setColor(v, opts) { this.set('color', v, opts); }

  setThickness(n, opts) { this.set('thickness', n, opts); }

  setMarkerSize(n, opts) { this.set('markerSize', n, opts); }

  setLineStyle(s) { this.set('style', s); }

  setShowPoints(b) { this.set('showPoints', b); }

  setShowLines(b) { this.set('showLines', b); }

  setImageFilter(f) { this.set('imageFilter', f); }

  setFilterColor(v, opts) { this.set('filterColor', v, opts); }

  setPageSize(size) { this.set('pageSize', size); }

  // Width/height are stored in cm (the model unit); the input is shown in the active
  // display unit. Pass cm from the UI handler (it converts the typed value first).
  setCustomPageWidth(cm) { this.set('customPageWidth', cm); }

  setCustomPageHeight(cm) { this.set('customPageHeight', cm); }

  setUnit(u) { this.set('unit', u); }

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

  setAllowFormulas(b) { this.set('allowFormulas', b); }

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
    app.remoteSync.scheduleRemoteSync();
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
