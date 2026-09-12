// ── Installing a layout: paste, drop, JSON upload and the silent code path ─
// One validation + confirmation flow (validateLayout → replace / combine / dimension
// prompts) behind every route a layout arrives by. installLayout() is the programmatic
// twin: no prompt, no toast — a caller passing lines in code already knows what it is.
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

// ── Apply a layout object pasted from the clipboard ──
// `from` is the drop point when the layout arrived by drag-and-drop; the canvas plays
// in out of it once the lines are installed.
export const applyPastedLayout = async (app, data, from = null) => {
  await applyValidatedLayout(app, data, {
    source: 'pasted JSON',
    cancelMsg: 'Layout paste canceled',
    successMsg: 'Layout pasted from clipboard',
    from,
  });
};

/**
 * Validate a layout payload and, after any needed confirmations, install it
 * as the current lines. Shared by JSON file upload and clipboard paste.
 * @param {object} data - Parsed layout payload (expects a `lines` array).
 * @param {{source: string, cancelMsg: string, successMsg: string}} opts -
 *   `source` names the layout's origin in the replace prompt; `cancelMsg` and
 *   `successMsg` are the toasts shown on cancel and success.
 * @returns {Promise<void>}
 */
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
  // Existing lines: offer to KEEP them and add the incoming ones on top, rather than
  // forcing an all-or-nothing replace. Cancel still backs out entirely.
  let mode = 'replace';
  if (verdict.needsReplaceConfirm) {
    const choice = await app.askAlt(
      `Add ${source} on top of the current layout, or replace it?`,
      {
        title: 'Existing layout',
        // Glyphs for the two real answers: swap one layout for the other, or stack
        // the incoming lines on the existing ones.
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
  // Dropped layouts fly in out of the drop point; a paste (no point) just lands.
  if (from) arriveFrom(document.getElementById('canvas-container'), from);
  notify(mode === 'combine' ? `${successMsg} (combined)` : successMsg, 'ok');
};

/**
 * Install a validated line list and refresh everything that reflects it.
 * @param {Array} lines - Validated lines to become `app.lines`.
 * @param {{history?: boolean}} [opts] - `history:false` keeps the change out of undo.
 */
const installLines = (app, lines, { history = true } = {}) => {
    app.lines = lines;
  if (history) app.saveHistory();
  app.renderer.redraw();
  app.updateButtons();
  if (app.lines.length > 0) app.coordTable.update(app.lines[app.lines.length - 1].points);
};

/**
 * Install a layout with NO prompt and NO toast — the programmatic path behind
 * `stencil.setLines()`. The replace/dimension confirmations exist to protect a user
 * from a surprise paste; a caller passing lines in code already knows what it is
 * installing. Returns true when the layout was applied.
 * @param {object} data - Layout payload (expects a `lines` array).
 * @param {{history?: boolean}} [opts] - `history:false` keeps it out of undo.
 * @returns {boolean}
 */
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
