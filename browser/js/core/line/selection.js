import { CHANGE, changed } from '../app/changes.js';
import { clampThickness, clampPointSize } from '../settings/limits.js';
// The single/multi selection set and the ways to change it (⌘/Ctrl+Shift+click on the canvas, a
// click in the Lines tab), and one line's own style by index, the path the bar and the rows share.
// Desktop twin: canvas/draw/CanvasSelection.cpp and CanvasLineEdit.cpp.

// Single-select: `selectedLines` is empty and this is [selectedLineIdx]. Always in range.
export const selectedIndices = (app) => {
  const src = app.selectedLines.length ? app.selectedLines : (app.selectedLineIdx >= 0 ? [app.selectedLineIdx] : []);
  return src.filter((i) => i >= 0 && i < app.lines.length);
};

// Whether line i is selected, for a whole frame: one Set over the multi-selection, not an
// includes per line; single-select reads `selectedLineIdx`.
export const selectionPredicate = (app) => {
  if (!app.selectedLines?.length) { const one = app.selectedLineIdx; return (i) => i === one; }
  const set = new Set(app.selectedLines);
  return (i) => set.has(i);
};

// Ctrl/⌘+Shift+click toggles `idx`; exactly one left drops back to single-select (editor
// reappears), 2+ hides the single-line editor.
export const toggleLineSelection = (app, idx) => {
// Seed the set from the current single selection.
  if (!app.selectedLines.length && app.selectedLineIdx >= 0 && app.selectedLineIdx !== idx)
    app.selectedLines = [app.selectedLineIdx];
  const at = app.selectedLines.indexOf(idx);
  if (at >= 0) app.selectedLines.splice(at, 1);
  else app.selectedLines.push(idx);

  if (app.selectedLines.length === 1) {
    app.selectedLineIdx = app.selectedLines[0];
    app.selectedLines = [];
    app.showSelectionPanel(app.lines[app.selectedLineIdx]);
    app.coordLineIdx = app.selectedLineIdx;
    app.focusedPtIdx = -1;
    app.coordTable.update(app.lines[app.selectedLineIdx].points, app.selectedLineIdx);
  } else {
    app.selectedLineIdx = -1;
    app.hideSelectionPanels();
  }
  updateMultiSelectStatus(app);
  app.renderer.redraw();
};

// "N lines selected" in the status line while multi-selecting (2+). Mirrors the desktop status bar.
export const paintMultiSelectStatus = (app) => {
  const el = document.getElementById('coord-status');
  if (!el) return;
  const n = app.selectedLines.length;
  if (n >= 2) el.textContent = `${n} lines selected — ⌘/Ctrl+Shift+click to add/remove · Alt+Shift+drag to move all · Ctrl+Shift+scroll to rotate all`;
  else if (el.dataset.multi) el.textContent = '';
  el.dataset.multi = n >= 2 ? '1' : '';
};

export const updateMultiSelectStatus = (app) => {
  paintMultiSelectStatus(app);
  changed(app, CHANGE.selection);
};

// A line set replaced whole (undo, redo, an install, a clear) keeps no index into the old one; the
// points table falls back to the stroke in progress, else the last line (desktop panelLine).
export const settleReplacedLines = (app) => {
  app.selectedLineIdx = -1;
  app.selectedLines = [];
  app.focusedPtIdx = -1;
  app.hoveredPtIdx = -1;
  app.hoverPt = null;
  app.hoverLineIdx = -1;
  app.listHoverLineIdx = -1;
  app.hideSelectionPanels();
  paintMultiSelectStatus(app);
  const live = app.currentLine?.points.length ? app.currentLine : null;
  const idx = live ? -1 : app.lines.length - 1;
  app.coordTable.update(live ? live.points : (app.lines[idx]?.points ?? null), idx);
};

// The same lines moved (a turn, a flip, a crop that keeps them, an undo or redo): the selection
// stays on the lines still there and the bar and table re-read them; with none left it settles.
export const keepLineSelection = (app) => {
  const n = app.lines.length;
  const inSet = (i) => i >= 0 && i < n;
  const held = (app.selectedLines ?? []).filter(inSet);
  const one = held.length === 1 ? held[0] : (inSet(app.selectedLineIdx) ? app.selectedLineIdx : -1);
  if (held.length < 2 && one < 0) { settleReplacedLines(app); return; }
  app.hoverPt = null;
  app.hoverLineIdx = -1;
  app.listHoverLineIdx = -1;
  app.hoveredPtIdx = -1;
  app.selectedLines = held.length >= 2 ? held : [];
  app.selectedLineIdx = held.length >= 2 ? -1 : one;
  if (app.selectedLineIdx >= 0) app.showSelectionPanel(app.lines[app.selectedLineIdx]);
  else app.hideSelectionPanels();
  paintMultiSelectStatus(app);
  const t = inSet(app.coordLineIdx) ? app.coordLineIdx : (app.selectedLineIdx >= 0 ? app.selectedLineIdx : n - 1);
  if (app.focusedPtIdx >= app.lines[t].points.length) app.focusedPtIdx = -1;
  app.coordTable.update(app.lines[t].points, t);
};

// A click on the letterbox OUTSIDE the image drops the selection; canvasClick() is bound
// to the <canvas> and needs image coordinates. Guards mirror it.
export const deselectEmptyArea = (app, e) => {
  if (!app.image) return;
  if (app.isDrawing) return;
  if (app.compareReadOnly()) return;
  if (app.dragJustEnded) return;
  if (e && (e.altKey || e.shiftKey || e.ctrlKey || e.metaKey)) return;
  if (app.selectedLineIdx === -1 && !app.selectedLines.length && app.focusedPtIdx === -1) return;
  app.deselectLine();
};

const CLAMP = Object.freeze({ thickness: clampThickness, pointSize: clampPointSize });

// Line `idx`'s own style. `commit:false` is a live preview; the commit is one history step naming
// the line set. A size is clamped to LIMITS, and ignored when it is no number.
export const applyLineChange = (app, idx, prop, value, { commit = true } = {}) => {
  const line = app.lines[idx];
  if (!line || app.compareReadOnly()) return false;
  const clamp = CLAMP[prop];
  if (clamp && !Number.isFinite(value)) return false;
// A line still on the inherit fallback ('' pointColor) pins its rendered colour first.
  if (prop === 'color' && !line.pointColor) line.pointColor = line.color;
  line[prop] = clamp ? clamp(value) : value;
  if (commit) { app.saveHistory(); changed(app, CHANGE.lines); }
  app.renderer.redraw();
  return true;
};

export const applySelectionChange = (app, prop, value, opts) =>
  applyLineChange(app, app.selectedLineIdx, prop, value, opts);

// The reverse of applyLinesListHover; -1 / out-of-range clears the glow.
export const setListHoverLine = (app, idx) => {
  const i = (typeof idx === 'number' && idx >= 0 && idx < app.lines.length) ? idx : -1;
  if (i === app.listHoverLineIdx) return;
  app.listHoverLineIdx = i;
  app.renderer.redraw();
};

// Select from the Lines tab (or console), keyed by index; clears any multi-selection.
// `ctrlShift` toggles the multi-select set instead.
export const selectLineFromList = (app, idx, ctrlShift = false) => {
  if (idx < 0 || idx >= app.lines.length) return this;
  if (ctrlShift) { toggleLineSelection(app, idx); return this; }
  app.selectedLines = [];
  app.selectedLineIdx = idx;
  app.showSelectionPanel(app.lines[idx]);
  app.coordLineIdx = idx;
  app.focusedPtIdx = -1;
  app.coordTable.update(app.lines[idx].points, idx);
  updateMultiSelectStatus(app);
  app.renderer.redraw();
  return this;
};
