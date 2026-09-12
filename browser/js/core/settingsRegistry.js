import { setVal, cmToUnit } from '../utils.js';
import { normalizePageSize } from './units.js';
import { applyMirror, paintTintControls, paintCustomSizeGroup } from '../ui/settingMirrors.js';

// Shared `parse` guards for the settings registry below: the numeric ones return
// undefined on NaN to ABORT the set (matching the old per-setter guards).
const toInt = n => { const v = parseInt(n, 10); return Number.isNaN(v) ? undefined : v; };
const toNum = n => { const v = parseFloat(n); return Number.isNaN(v) ? undefined : v; };
const toStr = v => String(v);

// Compare-view modes, in cycle order (Alt+O steps through them). See DrawingApp.compareMode.
export const COMPARE_MODES = ['none', 'original', 'vertical', 'horizontal'];

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
export const SETTINGS = {
  color: {
    field: 'color', parse: toStr,
    mirror: [{ id: 'line-color', kind: 'value' }],
    save: true,
  },
  // Default point colour for new lines, independent of the line colour above.
  // Redraws, because existing lines that never set their own point colour fall back to
  // the line colour — but the in-progress point preview follows this default live.
  pointColor: {
    field: 'pointColor', parse: toStr,
    mirror: [{ id: 'point-color', kind: 'value' }],
    redraw: true, save: true,
  },
  thickness: {
    field: 'thickness', parse: toInt,
    mirror: [{ id: 'line-thickness', kind: 'value' }, { id: 'ctx-thickness', kind: 'valueSkipFocus' }],
    redraw: true, save: true,
  },
  pointSize: {
    field: 'pointSize', parse: toInt,
    mirror: [{ id: 'point-size', kind: 'value' }, { id: 'ctx-point-size', kind: 'valueSkipFocus' }],
    redraw: true, save: true,
  },
  filterColor: {
    field: 'filterColor', parse: toStr,
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
    field: 'style', parse: toStr,
    mirror: [{ id: 'line-style', kind: 'value' }, { id: 'ctxLineStyle', kind: 'radio' }],
    save: true,
  },
  imageFilter: {
    field: 'imageFilter', parse: toStr,
    mirror: [{ id: 'image-filter', kind: 'value' }, { id: 'ctxFilter', kind: 'radio' }],
    afterSet: (self) => {
      const app = self.app;
      paintTintControls(app.imageFilter === 'custom');
    },
    redraw: true, save: true, remoteSync: true, filterDirty: true,
  },
  compareMode: {
    field: 'compareMode',
    parse: v => {
      const s = String(v);
      if (!COMPARE_MODES.includes(s))
        throw new Error(`Unknown compare mode: ${v} (use ${COMPARE_MODES.join(' | ')})`);
      return s;
    },
    mirror: [{ id: 'compare-mode', kind: 'value' }],
    // Compare is read-only — grey out the editing toolbar controls (Start/Stop/undo/…).
    afterSet: (self) => self.app.updateButtons(),
    redraw: true,   // transient view state — repaint only, no save/remoteSync
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
      paintCustomSizeGroup(app.pageSize === 'custom');
      app.coordTable.update();
    },
    redraw: true, save: true, remoteSync: true,   // page format rides the layout — push it to peers/server too
  },
  // Width/height are stored in cm (the model unit) but shown in the active display unit, so
  // the mirror is a converted value (cmToUnit) rather than the raw field — done in afterSet.
  customPageWidth: {
    field: 'customPageWidth', parse: toNum,
    afterSet: (self) => {
      const app = self.app;
      setVal('custom-page-width', cmToUnit(app.customPageWidth, app.unit));
      app.coordTable.update();
    },
    redraw: true, save: true, remoteSync: true,
  },
  customPageHeight: {
    field: 'customPageHeight', parse: toNum,
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
