import { composeControlTitle } from '../utils.js';
import { hotkeys } from '../core/hotkeys.js';
import { revealControls, settleMark } from './motion.js';

// ── App-wide control gating (extracted from drawingApp.js updateButtons) ──
// One sweep that reflects the editor state (image loaded, drawing, history, read-only
// compare, server link) onto every toolbar/panel control. DrawingApp keeps a thin
// updateButtons() delegator — the single entry point every state change already calls.
export function updateButtons(app) {
  // No image → nothing to draw on, so undo/redo are meaningless (and there's
  // no history to act on anyway). Keep them disabled until an image exists.
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
  // A compare view is read-only — undo/redo are disabled regardless of history.
  const readOnly = app.compareReadOnly();
  if (readOnly) {
    document.getElementById('undo').disabled = true;
    document.getElementById('redo').disabled = true;
  }
  // Fullscreen is NOT gated on an image: the empty editor still has a canvas to
  // fill (the "＋ Blank image" card) and the full toolbar, so it stays available.
  const fsBtn = document.getElementById('fullscreen-toggle');
  if (fsBtn) fsBtn.disabled = false;
  const noImage = !app.image;
  // Publish empty/read-only to the stylesheet: body.canvas-empty keeps fullscreen on the
  // page ground (cinema black is for viewing an image), and both classes drive the canvas
  // cursor (css/layout/canvasCursor.css) — the aim belongs to an editable image only.
  const body = document.body;
  const wasAim = !!body && !body.classList.contains('canvas-empty') &&
    !body.classList.contains('canvas-readonly');
  body?.classList?.toggle('canvas-empty', noImage);
  body?.classList?.toggle('canvas-readonly', readOnly);
  // On a transition, drop the inline cursor a hover left behind so the RESTING cursor
  // is the stylesheet's — otherwise a stale crosshair/default outlives the change until
  // the pointer moves again. Only on a change: mid-drag cursors (grabbing/move) survive.
  if (app.canvas && wasAim !== (!noImage && !readOnly)) app.canvas.style.cursor = '';
  for (const id of ['zoom-in', 'zoom-out', 'zoom-fit', 'zoom-input']) {
    const el = document.getElementById(id);
    if (el) el.disabled = noImage;
  }
  // The blank-image creator icon lives on the empty canvas — only the idle
  // (imageless) state shows it; with an image loaded it would cover content.
  const idleCreate = document.getElementById('idle-create-wrap');
  if (idleCreate) idleCreate.style.display = noImage ? '' : 'none';

  // ── Gate every image/lines-dependent action ──────────────────
  // No image → nothing to draw/transform/export, so these are disabled; their
  // data-disabled-reason (in the markup) feeds the tooltip via composeControlTitle to
  // explain why. Layout export/clear also need at least one line.
  const hasImage = !!app.image;
  const hasLines = app.lines && app.lines.length > 0;
  // A compare view is read-only — every annotation-editing control is also disabled.
  const ro = app.compareReadOnly();
  const setDisabled = (id, off) => { const el = document.getElementById(id); if (el) el.disabled = off; };
  // One Start/Stop button: enabled whenever there is something to draw on (it must stay
  // clickable WHILE drawing — that is how you stop), with its label/icon synced here.
  setDisabled('draw-toggle', !hasImage || ro);
  app.syncDrawToggleUI();
  setDisabled('draw-mode-toggle', !hasImage || ro);
  // Both Draw-group faces are owned here. Syncing the mode toggle too is what records its
  // CURRENT face, so the first Line↔Rect switch of the session is a swap and not a silent
  // first paint — and an unchanged face costs nothing (motion.js skips the rewrite).
  app.syncDrawModeUI();
  setDisabled('crop-image', !hasImage);
  setDisabled('rotate-left', !hasImage);
  setDisabled('rotate-right', !hasImage);
  setDisabled('image-filter', !hasImage);
  setDisabled('compare-mode', !hasImage);
  setDisabled('save-image', !hasImage);
  // Saving a .stencil bundles the current image — needs one; opening a .stencil is always allowed.
  setDisabled('save-project-btn', !hasImage);
  app.updateStencilSyncUI();   // live-sync toggle reflects link/support/on state
  // Description, keywords and links live in a SAVED project's meta — a temporary editor or
  // an incognito session has nothing to attach them to, so the three gate together.
  const hasProject = app.activeProjectId != null && !app.storage?.incognito;
  setDisabled('description-btn', !hasProject);
  setDisabled('keywords-btn', !hasProject);
  setDisabled('links-btn', !hasProject);
  setDisabled('download-json', !hasLines);
  setDisabled('copy-json-btn', !hasLines);
  // Importing a layout draws it onto the CURRENT image — needs one loaded (the handler also
  // guards with a toast, but disable the button to match the desktop + the other image actions).
  setDisabled('upload-json-btn', !hasImage);
  setDisabled('clear-all-lines', !hasLines || ro);
  // State-aware Image section: the compact "Load Image" button shows only when
  // empty; the image-actions group (download/copy/share/open) shows only with an
  // image. (The file input itself stays hidden — it's just the picker target.)
  // The swap is HALF sand (user decision): the LEAVING side goes at once — no
  // dust-out, no collapse — and only the ARRIVING side slides its slot open under
  // gathering motes (motion.js revealControls; an unchanged state costs nothing).
  const swapShown = (el, show) => {
    if (!el) return;
    if (show) { revealControls(el, true); return; }
    settleMark(el);              // drop any in-flight gather before the hard hide
    el.style.display = 'none';
  };
  swapShown(document.getElementById('load-image-btn'), !hasImage);
  swapShown(document.getElementById('image-actions'), hasImage);
  // "Open in…" hides entirely when neither target is available (nothing to open into),
  // so it never shows a dead/greyed control. Availability tracks the loaded config +
  // whether this is a server project (see openInAvailable).
  const openInBtn = document.getElementById('open-in-btn');
  if (openInBtn) openInBtn.style.display = (hasImage && app.openInAvailable()) ? '' : 'none';
  // Clear/remove-current-project is hidden for SERVER projects: a server project is removed
  // only from the projects list (its Remove action), so the toolbar never offers a local-only
  // clear that reads ambiguously ("did it delete on the server too?"). Local / temporary
  // editors keep it (clear a local project, or reset a blank editor).
  const clearBtn = document.getElementById('clear-storage');
  if (clearBtn) clearBtn.style.display = app.remoteLink ? 'none' : '';
  // …and an empty editor has nothing to clear, so it greys out rather than asking a
  // question and then "clearing" a canvas that was already blank (desktop parity).
  setDisabled('clear-storage', !hasImage);
  // Recompose tooltips so the reason line appears/clears with the disabled state
  // (and hotkey buttons keep their combo). Covers every control carrying either
  // a hotkey id or a disabled-reason.
  document.querySelectorAll('[data-disabled-reason], [data-hk-title]').forEach(el => {
    el.dataset.tip = composeControlTitle(el, hotkeys.isMac, id => hotkeys.get(id));
  });

  app.updateIncognitoUI();
  app.updateProjectTitle();
  app.renderLinesList();
}
