import { setVal, notify } from '../../utils.js';
import { clampThickness } from '../settings/limits.js';
import { canvasCoords } from '../pointer/canvasCoords.js';
import { selectedIndices, updateMultiSelectStatus, paintMultiSelectStatus } from './selection.js';
import { CHANGE, changed } from '../app/changes.js';

// Point / line mutation shared by the coord table, the Lines tab and the console facade.
// Each keeps the selection, the coord-table target and every cached hover consistent with
// the indices it just shifted, then saves one history entry.

// lineIdx === -1 targets the in-progress currentLine (the coord table's target resolution).
export const setPointCoord = (app, lineIdx, ptIdx, axis, valuePx) => {
  const line = lineIdx === -1 ? app.currentLine : app.lines[lineIdx];
  if (!line || !line.points[ptIdx] || (axis !== 'x' && axis !== 'y')) return this;
  const v = Number(valuePx);
  if (!Number.isFinite(v)) return this;
  line.points[ptIdx][axis] = v;
  app.saveHistory();
  app.renderer.redraw();
  app.coordTable.update(line.points, lineIdx);
  return this;
};

// Line `idx` leaves the set: a selection or table target on it goes, one past it moves down, and a
// multi-selection left holding one line is a single selection again (desktop removeLineByIndex).
const dropLine = (app, idx) => {
  app.lines.splice(idx, 1);
  app.hoverPt = null;
  app.hoverLineIdx = -1;
  app.listHoverLineIdx = -1;
  const past = (i) => (i > idx ? i - 1 : i);
  if (app.selectedLineIdx === idx) app.deselectLine(false);
  else app.selectedLineIdx = past(app.selectedLineIdx);
  if (app.coordLineIdx === idx) { app.coordLineIdx = -1; app.focusedPtIdx = -1; }
  else app.coordLineIdx = past(app.coordLineIdx);
  if (!app.selectedLines?.length) return;
  app.selectedLines = app.selectedLines.filter((i) => i !== idx).map(past);
  if (app.selectedLines.length === 1) {
    app.selectedLineIdx = app.coordLineIdx = app.selectedLines[0];
    app.selectedLines = [];
    app.focusedPtIdx = -1;
    app.showSelectionPanel(app.lines[app.selectedLineIdx]);
  }
  paintMultiSelectStatus(app);
};

const showTableTarget = (app) => {
  const target = app.coordLineIdx >= 0 ? app.lines[app.coordLineIdx] : null;
  app.coordTable.update(target ? target.points : null, app.coordLineIdx);
};

// Emptying a committed line drops the line too.
export const removePoint = (app, lineIdx, ptIdx) => {
  const line = lineIdx === -1 ? app.currentLine : app.lines[lineIdx];
  if (!line || !line.points[ptIdx]) return this;
  line.points.splice(ptIdx, 1);
// Indices shifted: a cached canvas hover would ring a DIFFERENT point.
  app.hoverPt = null;
  app.hoverLineIdx = -1;
  app.listHoverLineIdx = -1;
  if (line.points.length === 0 && lineIdx !== -1) {
    dropLine(app, lineIdx);
    showTableTarget(app);
  } else {
    if (app.focusedPtIdx >= line.points.length) app.focusedPtIdx = line.points.length - 1;
    app.coordTable.update(line.points, lineIdx);
  }
  app.saveHistory();
  app.renderer.redraw();
  changed(app, CHANGE.lines, CHANGE.selection);
  return this;
};

export const removeLine = (app, idx) => {
  if (idx < 0 || idx >= app.lines.length) return this;
  dropLine(app, idx);
  app.saveHistory();
  app.renderer.redraw();
  changed(app, CHANGE.lines, CHANGE.selection);
  showTableTarget(app);
  return this;
};

// Splices from the highest index down, then clears the selection wholesale; one history
// entry for the batch.
export const removeSelectedLines = (app) => {
  const sel = selectedIndices(app)
    .filter(i => i >= 0 && i < app.lines.length)
    .sort((a, b) => b - a);
  if (!sel.length) return this;
  for (const idx of sel) {
    app.lines.splice(idx, 1);
// The coord table can target a line outside the selection; keep its index valid.
    if (app.coordLineIdx === idx) { app.coordLineIdx = -1; app.focusedPtIdx = -1; }
    else if (app.coordLineIdx > idx) app.coordLineIdx -= 1;
  }
// Not deselectLine(): it resets coordLineIdx unconditionally and would blank the coord
// table even when it targets a surviving line.
  app.selectedLineIdx = -1;
  app.selectedLines = [];
  app.hoveredPtIdx = -1;
  app.hoverPt = null;
  app.hoverLineIdx = -1;
  app.listHoverLineIdx = -1;
  app.hideSelectionPanels();
  updateMultiSelectStatus(app);
  app.saveHistory();
  app.renderer.redraw();
  changed(app, CHANGE.lines, CHANGE.selection);
  showTableTarget(app);
  return this;
};

// Alt+wheel: ±1, clamped 1–20. `scheduleSave` is the caller's debounce, so a burst of
// notches becomes one history entry; it is called only when the value actually moved.
export const adjustThicknessAtCursor = (app, e, scheduleSave) => {
  const { x, y } = canvasCoords(app, e.clientX, e.clientY);
  const delta = e.deltaY > 0 ? -1 : 1;
  const nearPt = app.findNearestPointWithIdx(x, y);
  let lineIdx = -1;
  if (nearPt && nearPt.lineIdx !== -1) lineIdx = nearPt.lineIdx;
  else { const li = app.findLineAt(x, y); if (li !== -1) lineIdx = li; }
  if (lineIdx === -1) return false;
  const line = app.lines[lineIdx];
  const newT = clampThickness((line.thickness || 1) + delta);
  if (newT === line.thickness) return true;
  line.thickness = newT;
  changed(app, CHANGE.lines);
  if (lineIdx === app.selectedLineIdx) {
    setVal('sel-thickness', newT);
    setVal('fs-sel-thickness', newT);
  }
  app.renderer.requestRedraw();
  notify('Line thickness: ' + newT, 'info');
  scheduleSave();
  return true;
};
