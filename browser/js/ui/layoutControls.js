import { setChecked } from './control/controlSwap.js';
import { revealControls } from './motion.js';

// Writes a saved (or peer-synced) layout to the form fields, so js/core keeps no element ids.
const el = (id) => document.getElementById(id);
const setValue = (id, v) => { const n = el(id); if (n) n.value = v; };

// Called BEFORE applyUnitToUI, which renders the custom inputs in the restored unit.
export const paintPageSize = (pageSize) => {
  setValue('page-size', pageSize);
  revealControls(el('custom-size-group'), pageSize === 'custom');
};

// pointColor '' means "follow the line colour", which the picker cannot show, so the field
// keeps its last value.
export const paintDrawingControls = (app, layout) => {
  if (layout.color) setValue('line-color', layout.color);
  if (typeof layout.pointColor === 'string' && layout.pointColor) setValue('point-color', layout.pointColor);
  if (layout.thickness) setValue('line-thickness', layout.thickness);
  if (layout.pointSize) setValue('point-size', layout.pointSize);
  if (layout.style) setValue('line-style', layout.style);
  if (layout.filterColor) setValue('filter-color', app.filterColor);
  setChecked(el('show-points'), app.showPoints);
  setChecked(el('show-lines'), app.showLines);
  setValue('image-filter', app.imageFilter);
  const picker = el('filter-color');
  if (picker) picker.style.display = (app.imageFilter === 'custom') ? 'inline-block' : 'none';
};

// A peer's layout arrives with nothing else to paint.
export const paintVisibilityChecks = (app) => {
  setChecked(el('show-points'), app.showPoints);
  setChecked(el('show-lines'), app.showLines);
};

// Both copies of each formula field: the top bar's and the context menu's.
export const paintFormulaFields = (app) => {
  for (const [id, v] of [['formula-x', app.formulaX], ['formula-y', app.formulaY]]) {
    setValue(id, v);
    setValue(`ctx-${id}`, v);
  }
};

// Both panels: docked + fullscreen.
export const hideSelectionPanels = () => {
  for (const id of ['selection-panel', 'fs-selection-panel']) {
    const n = el(id);
    if (n) n.style.display = 'none';
  }
};

// A stale scroll leaves the idle card off-screen.
export const resetViewportScroll = () => scrollViewportTo(0, 0);

// Assigning scroll forces the reflow it needs, so a restore runs synchronously before any
// arrival motion — deferred a frame, the viewport jumped out from under the cloud.
export const scrollViewportTo = (left, top) => {
  const vp = el('canvas-viewport');
  if (vp) { vp.scrollLeft = left || 0; vp.scrollTop = top || 0; }
};
