// The toolbar and panel controls as areas over the editor's change feed (core/app/changes.js):
// each area gates its own controls and follows only the channels its inputs move on, so an edit
// repaints what it touched. updateButtons() runs every area: boot, theme, restore, a new picture.
import { composeControlTitle } from '../../utils.js';
import { hotkeys } from '../../core/settings/hotkeys.js';
import { CHANGE, FLUSH } from '../../core/app/changes.js';
import { revealControls, settleMark } from '../motion.js';
import { motionReduced } from '../motion/motionPrefs.js';
import { updateStencilSyncUI } from '../../core/project/fileIO.js';
import { syncDrawToggleUI, syncDrawModeUI } from '../panel/drawToggleUI.js';
import { updateProjectTitle } from '../projects/window/projectTitle.js';
import { renderLinesList } from '../panel/lines/list.js';

const IDLE_ARRIVE_CLASS = 'idle-arriving';
const followers = new Set();

// `fn(names)` runs after every sweep or flush with the areas that ran, so a surface mirroring the
// editor (the open context menu) follows it.
export const onButtonsUpdated = (fn) => { followers.add(fn); return () => followers.delete(fn); };

const setDisabled = (id, off) => { const el = document.getElementById(id); if (el) el.disabled = off; };

const historyGates = (app) => {
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
  if (app.compareReadOnly()) {
    document.getElementById('undo').disabled = true;
    document.getElementById('redo').disabled = true;
  }
};

const canvasGates = (app) => {
  const readOnly = app.compareReadOnly();
  // Fullscreen is not gated on an image: the empty editor still has the canvas and toolbar.
  const fsBtn = document.getElementById('fullscreen-toggle');
  if (fsBtn) fsBtn.disabled = false;
  const noImage = !app.image;
  // body.canvas-empty keeps fullscreen on the page ground; both classes drive the canvas
  // cursor (css/layout/canvas/cursor.css).
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
  // The blank-image creator lives on the empty canvas only and arrives when a picture leaves,
  // never on the first paint (display starts unset). Desktop twin: CanvasWidget::clearImage.
  const idleCreate = document.getElementById('idle-create-wrap');
  if (idleCreate) {
    const wasShown = idleCreate.style.display !== 'none';
    idleCreate.style.display = noImage ? '' : 'none';
    if (noImage && !wasShown && !motionReduced()) {
      idleCreate.classList.remove(IDLE_ARRIVE_CLASS);
      void idleCreate.offsetWidth;   // restart the keyframe on a second removal
      idleCreate.classList.add(IDLE_ARRIVE_CLASS);
    }
  }
};

// Start/Stop stays clickable while drawing — that is how you stop. Syncing the mode toggle
// records its current face, so the first Line↔Rect switch is a swap.
const drawGates = (app) => {
  const off = !app.image || app.compareReadOnly();
  setDisabled('draw-toggle', off);
  syncDrawToggleUI(app);
  setDisabled('draw-mode-toggle', off);
  syncDrawModeUI(app);
};

// The Image section swap is half sand: the leaving side goes at once, only the arriving
// side slides open under gathering motes (motion.js revealControls).
const swapShown = (el, show) => {
  if (!el) return;
  if (show) { revealControls(el, true); return; }
  settleMark(el);
  el.style.display = 'none';
};

// data-disabled-reason (in the markup) feeds the tooltip via composeControlTitle.
const imageGates = (app) => {
  const hasImage = !!app.image;
  for (const id of ['crop-image', 'flip-horizontal', 'rotate-left', 'rotate-right', 'compare-mode', 'save-image',
    'save-project-btn', 'upload-json-btn']) setDisabled(id, !hasImage);
  setDisabled('image-filter', false);   // a tint chosen ahead colours the next picture
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
};

// Description, keywords and links live in a saved project's meta, so the three gate together.
const projectGates = (app) => {
  updateStencilSyncUI(app);
  const hasProject = app.activeProjectId != null && !app.storage?.incognito;
  for (const id of ['description-btn', 'keywords-btn', 'links-btn']) setDisabled(id, !hasProject);
};

const projectFaces = (app) => {
  app.updateIncognitoUI();
  updateProjectTitle(app);
};

const linesGates = (app) => {
  const hasLines = app.lines?.length > 0;
  setDisabled('download-json', !hasLines);
  setDisabled('copy-json-btn', !hasLines);
  setDisabled('clear-all-lines', !hasLines || app.compareReadOnly());
};

// Recompose tooltips so the reason line appears/clears with the disabled state; an unchanged
// tip is not rewritten, so the page-wide attribute observer sees only real changes.
const recomposeTips = () => {
  document.querySelectorAll('[data-disabled-reason], [data-hk-title]').forEach(el => {
    const tip = composeControlTitle(el, hotkeys.isMac, id => hotkeys.get(id));
    if (el.dataset.tip !== tip) el.dataset.tip = tip;
  });
};

// In sweep order: every area's gates, one tooltip pass, then what each repaints after them.
export const AREAS = Object.freeze([
  { name: 'history', on: [CHANGE.history, CHANGE.drawing, CHANGE.compare], gate: historyGates },
  { name: 'canvas', on: [CHANGE.compare], gate: canvasGates },
  { name: 'draw', on: [CHANGE.drawing, CHANGE.compare], gate: drawGates },
  { name: 'image', on: [CHANGE.project], gate: imageGates },
  { name: 'project', on: [CHANGE.project, CHANGE.lines], gate: projectGates, after: projectFaces },
  { name: 'lines', on: [CHANGE.lines, CHANGE.compare], gate: linesGates },
  { name: 'list', on: [CHANGE.lines, CHANGE.selection], after: renderLinesList },
]);

const runAreas = (app, areas) => {
  if (!areas.length) return;
  let gated = false;
  for (const area of areas) if (area.gate) { area.gate(app); gated = true; }
  if (gated) recomposeTips();
  for (const area of areas) area.after?.(app);
  const names = areas.map((area) => area.name);
  for (const fn of [...followers]) fn(names);
};

export function updateButtons(app) { runAreas(app, AREAS); }

// Each area marks itself dirty on its channels; a FLUSH runs the dirty ones once, in sweep order.
export const wireControlState = (app) => {
  const dirty = new Set();
  const offs = AREAS.flatMap((area) => area.on.map((ch) => app.changes.on(ch, () => dirty.add(area))));
  offs.push(app.changes.on(FLUSH, () => {
    const due = AREAS.filter((area) => dirty.has(area));
    dirty.clear();
    runAreas(app, due);
  }));
  return () => offs.forEach((off) => off());
};
