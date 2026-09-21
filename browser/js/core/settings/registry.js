import { setVal, cmToUnit } from '../../utils.js';
import { normalizePageSize } from './units.js';
import { paintTintControls, paintCustomSizeGroup } from '../../ui/settings/settingMirrors.js';

// The numeric parsers return undefined on NaN to ABORT the set.
const toInt = n => { const v = parseInt(n, 10); return Number.isNaN(v) ? undefined : v; };
const toNum = n => { const v = parseFloat(n); return Number.isNaN(v) ? undefined : v; };
const toStr = v => String(v);

// In cycle order (Alt+O steps through them).
export const COMPARE_MODES = Object.freeze(['none', 'original', 'vertical', 'horizontal']);

// One descriptor per simple setting: `field`, `parse` (undefined ABORTS), `mirror` DOM twins,
// `afterSet`, `redraw` (always, even on persist:false); save/remoteSync/filterDirty gated on `persist`.
export const SETTINGS = Object.freeze({
  color: {
    field: 'color', parse: toStr,
    mirror: [{ id: 'line-color', kind: 'value' }],
    save: true,
  },
  // Redraws: the in-progress point preview follows this default live.
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
    // Compare is read-only: grey out the editing toolbar controls.
    afterSet: (self) => self.app.updateButtons(),
    redraw: true,
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
    redraw: true, save: true, remoteSync: true,
  },
  // Stored in cm but shown in the active display unit, so the mirror is a converted value.
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
    // The expressions are KEPT so re-enabling restores them.
    afterSet: (self) => {
      const app = self.app;
      self.syncFormulaUI(app.allowFormulas);
      if (!app.allowFormulas) self.showFormulaError(false);
      self.refreshFormulaCoords();
    },
    save: true, remoteSync: true,
  },
});
