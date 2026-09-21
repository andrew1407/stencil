import { setVal, setRadioGroup, cmToUnit } from '../utils.js';
import { normalizePageSize } from '../core/settings/units.js';
import { revealControls } from './motion.js';

// ── Adopting a peer's (or the server's) layout onto the controls ─────────────
// A server layout arrives as model + screen in one step: the editor state it names and the
// form fields that show it. Element ids live here, never in js/core's sync controller.

// Adopt a server layout's filter/tint and clear the dirty flag (its filter is now ours), so a
// conflict-merge of a line-only edit preserves a peer's filter change instead of clobbering it.
export const adoptServerFilter = (app, layout) => {
  if (!layout) return;
  app.imageFilter = layout.imageFilter || (layout.blackAndWhite ? 'bw' : 'none');
  if (layout.filterColor) app.filterColor = layout.filterColor;
  setVal('image-filter', app.imageFilter);
  setRadioGroup('ctxFilter', app.imageFilter);
  const fp = document.getElementById('filter-color');
  if (fp) {
    fp.value = app.filterColor;
    fp.style.display = app.imageFilter === 'custom' ? 'inline-block' : 'none';
  }
  app.filterDirty = false;
};

// Restore a server layout's page format (A3/A4/custom + cm dims) into state + the page UI.
export const adoptServerPageFormat = (app, layout) => {
  if (!layout) return;
  const n = normalizePageSize(layout.pageSize);
  if (n) {
    app.pageSize = n;
    setVal('page-size', n);
    revealControls(document.getElementById('custom-size-group'), n === 'custom');
  }
  if (Number.isFinite(layout.customPageWidth)) {
    app.customPageWidth = layout.customPageWidth;
    setVal('custom-page-width', cmToUnit(layout.customPageWidth, app.unit));
  }
  if (Number.isFinite(layout.customPageHeight)) {
    app.customPageHeight = layout.customPageHeight;
    setVal('custom-page-height', cmToUnit(layout.customPageHeight, app.unit));
  }
};

// Restore a server layout's x/y formulas into state + the formula UI. The expressions are
// kept regardless of the toggle (allow only gates visibility + whether they're applied).
export const adoptServerFormulas = (app, layout) => {
  const allow = !!(layout && layout.allowFormulas);
  app.allowFormulas = allow;
  const cb = document.getElementById('allow-formulas');
  if (cb) cb.checked = allow;
  app.settings.syncFormulaUI(allow);
  const fx = layout && typeof layout.formulaX === 'string' ? layout.formulaX : '';
  const fy = layout && typeof layout.formulaY === 'string' ? layout.formulaY : '';
  app.formulaX = fx;
  app.formulaY = fy;
  setVal('formula-x', app.formulaX);
  setVal('formula-y', app.formulaY);
  setVal('ctx-formula-x', app.formulaX);
  setVal('ctx-formula-y', app.formulaY);
  app.settings.showFormulaError(false);
};
