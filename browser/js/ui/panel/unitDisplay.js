import { cmToUnit, unitLabel } from '../../utils.js';
import { pageFormatLabel } from '../../core/settings/units.js';
import { getPageDimensions, pixelToPageCoords } from '../../core/parse/pageMetrics.js';

// ── The active unit, as the page shows it ───────────────────────
// The two view writers split out of DrawingApp: the live cursor readout under the canvas,
// and the unit relabelling across the toolbar + coord table. Model values never change here.

// Mirrors the desktop status bar (MainWindow.cpp onHovered): ALWAYS Pixel + Page (cm) regardless
// of the tooltip's per-row toggles, plus To edge (cm). No args / no image → idle hint.
export const updateCoordStatus = (app, x, y) => {
  const el = app.coordStatus ??= document.getElementById('coord-status');
  if (!el) return;
  if (!app.image || x === undefined) {
    // Empty either way: this bar reads out the cursor, and off-canvas / imageless there is
    // nothing to read (desktop parity — support/MainWindow.cpp updateStatusIdle).
    el.textContent = '';
    return;
  }
  const page = pixelToPageCoords(app, x, y);
  const ps = getPageDimensions(app);
  const lbl = unitLabel(app.unit);
  const fx = v => cmToUnit(v, app.unit).toFixed(2);
  el.textContent =
    `Pixel (${Math.round(x)}, ${Math.round(y)})` +
    `   ·   Page (${fx(page.x)}, ${fx(page.y)}) ${lbl}` +
    `   ·   To edge (${fx(ps.width - page.x)}, ${fx(ps.height - page.y)}) ${lbl}`;
};

// Custom page-size inputs are stored in cm and shown in the active unit. Model values are
// never mutated — only their presentation.
export const applyUnitToUI = (app) => {
  const lbl = unitLabel(app.unit);
  const sel = document.getElementById('unit-select');
  if (sel) sel.value = app.unit;
  // Re-asserting the model value routes through enhanceSelect's wrapped setter, which refreshes
  // the trigger and, at boot, moves the select off its markup default (Custom is FIRST).
  const psSel = document.getElementById('page-size');
  if (psSel) {
    for (const opt of psSel.options)
      if (opt.value !== 'custom') opt.textContent = pageFormatLabel(opt.value, app.unit);
    psSel.value = app.pageSize;
  }
  const w = document.getElementById('custom-page-width');
  const h = document.getElementById('custom-page-height');
  if (w) w.value = +cmToUnit(app.customPageWidth, app.unit).toFixed(2);
  if (h) h.value = +cmToUnit(app.customPageHeight, app.unit).toFixed(2);
  const ths = document.querySelectorAll('#coordinates-table thead th');
  if (ths[3]) ths[3].textContent = `X ${lbl}`;
  if (ths[4]) ths[4].textContent = `Y ${lbl}`;
};
