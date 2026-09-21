import { composeControlTitle } from '../../utils.js';
import { hotkeys } from '../../core/hotkeys.js';
import { revealControls, settleMark } from '../motion.js';

// One sweep reflecting the editor state onto every toolbar/panel control; DrawingApp's
// updateButtons() delegates here.
export function updateButtons(app) {
  if (!app.image) {
    document.getElementById('undo').disabled = true;
    document.getElementById('redo').disabled = true;
  } else if (app.isDrawing && app.currentLine) {
    document.getElementById('undo').disabled = app.currentLine.points.length === 0;
    document.getElementById('redo').disabled = !app.undonePoints || app.undonePoints.length === 0;
  } else {
    document.getElementById('undo').disabled = !app.history.canUndo();
    document.getElementById('redo').disabled = !app.history.canRedo();
  }
  const readOnly = app.compareReadOnly();
  if (readOnly) {
    document.getElementById('undo').disabled = true;
    document.getElementById('redo').disabled = true;
  }
  // Fullscreen is not gated on an image: the empty editor still has the canvas and toolbar.
  const fsBtn = document.getElementById('fullscreen-toggle');
  if (fsBtn) fsBtn.disabled = false;
  const noImage = !app.image;
  // body.canvas-empty keeps fullscreen on the page ground; both classes drive the canvas
  // cursor (css/layout/canvasCursor.css).
  const body = document.body;
  const wasAim = !!body && !body.classList.contains('canvas-empty') &&
    !body.classList.contains('canvas-readonly');
  body?.classList?.toggle('canvas-empty', noImage);
  body?.classList?.toggle('canvas-readonly', readOnly);
  // On a transition, drop the inline cursor a hover left behind; mid-drag cursors survive.
  if (app.canvas && wasAim !== (!noImage && !readOnly)) app.canvas.style.cursor = '';
  for (const id of ['zoom-in', 'zoom-out', 'zoom-fit', 'zoom-input']) {
    const el = document.getElementById(id);
    if (el) el.disabled = noImage;
  }
  // The blank-image creator lives on the empty canvas only.
  const idleCreate = document.getElementById('idle-create-wrap');
  if (idleCreate) idleCreate.style.display = noImage ? '' : 'none';

  // data-disabled-reason (in the markup) feeds the tooltip via composeControlTitle.
  const hasImage = !!app.image;
  const hasLines = app.lines && app.lines.length > 0;
  const ro = app.compareReadOnly();
  const setDisabled = (id, off) => { const el = document.getElementById(id); if (el) el.disabled = off; };
  // Start/Stop stays clickable while drawing — that is how you stop.
  setDisabled('draw-toggle', !hasImage || ro);
  app.syncDrawToggleUI();
  setDisabled('draw-mode-toggle', !hasImage || ro);
  // Syncing the mode toggle records its current face, so the first Line↔Rect switch is a swap.
  app.syncDrawModeUI();
  setDisabled('crop-image', !hasImage);
  setDisabled('rotate-left', !hasImage);
  setDisabled('rotate-right', !hasImage);
  setDisabled('image-filter', !hasImage);
  setDisabled('compare-mode', !hasImage);
  setDisabled('save-image', !hasImage);
  setDisabled('save-project-btn', !hasImage);
  app.updateStencilSyncUI();
  // Description, keywords and links live in a saved project's meta, so the three gate together.
  const hasProject = app.activeProjectId != null && !app.storage?.incognito;
  setDisabled('description-btn', !hasProject);
  setDisabled('keywords-btn', !hasProject);
  setDisabled('links-btn', !hasProject);
  setDisabled('download-json', !hasLines);
  setDisabled('copy-json-btn', !hasLines);
  setDisabled('upload-json-btn', !hasImage);
  setDisabled('clear-all-lines', !hasLines || ro);
  // The Image section swap is half sand: the leaving side goes at once, only the arriving
  // side slides open under gathering motes (motion.js revealControls).
  const swapShown = (el, show) => {
    if (!el) return;
    if (show) { revealControls(el, true); return; }
    settleMark(el);
    el.style.display = 'none';
  };
  swapShown(document.getElementById('load-image-btn'), !hasImage);
  swapShown(document.getElementById('image-actions'), hasImage);
  // "Open in…" hides entirely when no target is available (see openInAvailable).
  const openInBtn = document.getElementById('open-in-btn');
  if (openInBtn) openInBtn.style.display = (hasImage && app.openInAvailable()) ? '' : 'none';
  // Hidden for server projects: they are removed from the projects list only.
  const clearBtn = document.getElementById('clear-storage');
  if (clearBtn) clearBtn.style.display = app.remoteLink ? 'none' : '';
  // An empty editor has nothing to clear (desktop parity).
  setDisabled('clear-storage', !hasImage);
  // Recompose tooltips so the reason line appears/clears with the disabled state.
  document.querySelectorAll('[data-disabled-reason], [data-hk-title]').forEach(el => {
    el.dataset.tip = composeControlTitle(el, hotkeys.isMac, id => hotkeys.get(id));
  });

  app.updateIncognitoUI();
  app.updateProjectTitle();
  app.renderLinesList();
}
