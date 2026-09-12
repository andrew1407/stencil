// Installing a layout: paste, drop, JSON upload and the silent code path (installLayout —
// no prompt, no toast).
import { notify } from '../utils.js';
import { arriveFrom } from '../ui/motion.js';
import { validateLayout } from './layout.js';

export const uploadJSON = (app, e) => {
  const file = e.target.files[0];
  if (!file) return;

  const reader = new FileReader();
  reader.onload = async event => {
    try {
      const data = JSON.parse(event.target.result);
      await applyValidatedLayout(app, data, {
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
};

// `from` is the drop point when the layout arrived by drag-and-drop.
export const applyPastedLayout = async (app, data, from = null) => {
  await applyValidatedLayout(app, data, {
    source: 'pasted JSON',
    cancelMsg: 'Layout paste canceled',
    successMsg: 'Layout pasted from clipboard',
    from,
  });
};

// `source` names the layout's origin in the replace prompt; `cancelMsg` / `successMsg` are
// the toasts.
const applyValidatedLayout = async (app, data, { source, cancelMsg, successMsg, from = null }) => {
  const verdict = validateLayout(data, {
    hasImage: !!app.image,
    imgW: app.canvas.width,
    imgH: app.canvas.height,
    hasExistingLines: !!(app.lines && app.lines.length > 0)
  });
  if (!verdict.ok) {
    notify('Load an image first', 'fail');
    return;
  }
// Existing lines: offer to combine rather than force an all-or-nothing replace.
  let mode = 'replace';
  if (verdict.needsReplaceConfirm) {
    const choice = await app.askAlt(
      `Add ${source} on top of the current layout, or replace it?`,
      {
        title: 'Existing layout',
        confirmLabel: 'Replace', confirmIcon: 'swap',
        altLabel: 'Combine', altIcon: 'layers',
      });
    if (!choice) { notify(cancelMsg, 'info'); return; }
    mode = choice === 'alt' ? 'combine' : 'replace';
  }
  if (verdict.needsDimMismatchConfirm && !(await app.confirm('Image dimensions do not match. Continue anyway?', { title: 'Dimension mismatch' }))) {
    notify(cancelMsg, 'info');
    return;
  }
  installLines(app, mode === 'combine' ? [...(app.lines || []), ...verdict.lines] : verdict.lines);
  if (from) arriveFrom(document.getElementById('canvas-container'), from);
  notify(mode === 'combine' ? `${successMsg} (combined)` : successMsg, 'ok');
};

// `history:false` keeps the change out of undo.
const installLines = (app, lines, { history = true } = {}) => {
    app.lines = lines;
  if (history) app.saveHistory();
  app.renderer.redraw();
  app.updateButtons();
  if (app.lines.length > 0) app.coordTable.update(app.lines[app.lines.length - 1].points);
};

// The programmatic path behind `stencil.setLines()`: no prompt, no toast. True when applied.
export const installLayout = (app, data, opts = {}) => {
  const verdict = validateLayout(data, {
    hasImage: !!app.image,
    imgW: app.canvas.width,
    imgH: app.canvas.height,
    hasExistingLines: !!(app.lines && app.lines.length > 0),
  });
  if (!verdict.ok) return false;
  const lines = opts.mode === 'combine' ? [...(app.lines || []), ...verdict.lines] : verdict.lines;
  installLines(app, lines, opts);
  return true;
};
