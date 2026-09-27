// The context menu's style, filter, view and tooltip rows.
import { clampThickness, clampPointSize } from '../../core/settings/limits.js';

const TINT_PREVIEW_MS = 80;

export const wireCtxStyleActions = (app, { menu, closeMenu }) => {
  document.getElementById('ctx-point-size').addEventListener('input', e => {
    const v = parseInt(e.target.value, 10);
    if (!isNaN(v) && v === clampPointSize(v)) {
      app.pointSize = v;
      const inp = document.getElementById('point-size');
      if (inp) inp.value = v;
      app.renderer.redraw();
    }
  });
  document.getElementById('ctx-point-size').addEventListener('change', e => {
    const v = clampPointSize(parseInt(e.target.value, 10) || app.pointSize);
    e.target.value = v; app.pointSize = v;
    const inp = document.getElementById('point-size');
    if (inp) inp.value = v;
    app.renderer.redraw(); app.storage.saveSoon();
  });

  document.getElementById('ctx-thickness').addEventListener('input', e => {
    const v = parseInt(e.target.value, 10);
    if (!isNaN(v) && v === clampThickness(v)) {
      app.thickness = v;
      const inp = document.getElementById('line-thickness');
      if (inp) inp.value = v;
      app.renderer.redraw();
    }
  });
  document.getElementById('ctx-thickness').addEventListener('change', e => {
    const v = clampThickness(parseInt(e.target.value, 10) || app.thickness);
    e.target.value = v; app.thickness = v;
    const inp = document.getElementById('line-thickness');
    if (inp) inp.value = v;
    app.renderer.redraw(); app.storage.saveSoon();
  });

  document.querySelectorAll('input[name="ctxLineStyle"]').forEach(r => {
    r.addEventListener('change', () => {
      app.style = r.value;
      const sel = document.getElementById('line-style');
      if (sel) sel.value = r.value;
      app.renderer.redraw(); app.storage.saveSoon();
    });
  });

  document.querySelectorAll('input[name="ctxFilter"]').forEach(r => {
    r.addEventListener('change', () => app.settings.setImageFilter(r.value));
  });

  // A tint drag previews (its repaint debounced) and commits on release, as one undo step.
  let ctxTintTimer = null;
  const ctxTint = document.getElementById('ctx-tint-color');
  ctxTint.addEventListener('input', e => {
    app.filterColor = e.target.value;
    const mainPicker = document.getElementById('filter-color');
    if (mainPicker) mainPicker.value = e.target.value;
    clearTimeout(ctxTintTimer);
    ctxTintTimer = setTimeout(() => app.settings.setFilterColor(e.target.value, { persist: false }), TINT_PREVIEW_MS);
  });
  ctxTint.addEventListener('change', e => {
    clearTimeout(ctxTintTimer);
    app.settings.setFilterColor(e.target.value);
  });

  document.getElementById('ctx-fullscreen').addEventListener('click', () => {
    closeMenu();
    if (typeof app.toggleFullscreen === 'function') app.toggleFullscreen();
  });

  document.getElementById('ctx-fit-window').addEventListener('click', () => {
    closeMenu();
    if (app && app.zoomPan && typeof app.zoomPan.fitToWindow === 'function') app.zoomPan.fitToWindow();
  });

  document.getElementById('ctx-tt-enabled').addEventListener('change', e => {
    app.tooltipEnabled = e.target.checked;
    if (!app.tooltipEnabled) app.tooltip.hide();
    app.storage.saveSoon();
  });
  document.getElementById('ctx-tt-page').addEventListener('change', e => {
    app.tooltipShowPage = e.target.checked;
    app.storage.saveSoon();
  });
  document.getElementById('ctx-tt-screen').addEventListener('change', e => {
    app.tooltipShowScreen = e.target.checked;
    app.storage.saveSoon();
  });
  document.getElementById('ctx-tt-coords').addEventListener('change', e => {
    app.tooltipShowCoords = e.target.checked;
    app.storage.saveSoon();
  });

  // Transformation submenu: formulas — the shared sync (controller.js), so the
  // toolbar pill's .on class stays in step too.
  document.getElementById('ctx-allow-formulas').addEventListener('change', e => {
    app.allowFormulas = e.target.checked;
    app.settings.syncFormulaUI(e.target.checked);
    if (!e.target.checked) { app.formulaX = ''; app.formulaY = ''; app.settings.showFormulaError(false); }
    app.settings.refreshFormulaCoords();
    app.storage.saveSoon();
  });
  // Same debounced commit as the toolbar pair, mirroring the other way — one wiring, so
  // both entry points flag errors, persist and sync to peers identically.
  app.settings.wireFormulaInputs({
    x: 'ctx-formula-x', y: 'ctx-formula-y', mirrorX: 'formula-x', mirrorY: 'formula-y',
  });
  menu.addEventListener('mousedown', e => e.stopPropagation());
};
