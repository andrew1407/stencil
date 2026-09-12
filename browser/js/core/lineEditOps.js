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
    app.lines.splice(lineIdx, 1);
    if (app.selectedLineIdx === lineIdx) app.deselectLine(false);
    app.coordLineIdx = -1;
    app.focusedPtIdx = -1;
    app.coordTable.update(null);
  } else {
    if (app.focusedPtIdx >= line.points.length) app.focusedPtIdx = line.points.length - 1;
    app.coordTable.update(line.points, lineIdx);
  }
  app.saveHistory();
  app.renderer.redraw();
  app.updateButtons();
  return this;
};

export const removeLine = (app, idx) => {
  if (idx < 0 || idx >= app.lines.length) return this;
  app.lines.splice(idx, 1);
  app.hoverPt = null;
  app.hoverLineIdx = -1;
  app.listHoverLineIdx = -1;
// Drop the selection / coord target if it pointed at the removed line, else shift it down.
  if (app.selectedLineIdx === idx) app.deselectLine(false);
  else if (app.selectedLineIdx > idx) app.selectedLineIdx -= 1;
  if (app.coordLineIdx === idx) { app.coordLineIdx = -1; app.focusedPtIdx = -1; }
  else if (app.coordLineIdx > idx) app.coordLineIdx -= 1;
  app.saveHistory();
  app.renderer.redraw();
  app.updateButtons();
  const target = app.coordLineIdx >= 0 ? app.lines[app.coordLineIdx] : null;
  app.coordTable.update(target ? target.points : null, app.coordLineIdx);
  return this;
};

// Splices from the highest index down, then clears the selection wholesale; one history
// entry for the batch.
export const removeSelectedLines = (app) => {
  const sel = app.selectedIndices()
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
  app.updateMultiSelectStatus();
  app.saveHistory();
  app.renderer.redraw();
  app.updateButtons();
  const target = app.coordLineIdx >= 0 ? app.lines[app.coordLineIdx] : null;
  app.coordTable.update(target ? target.points : null, app.coordLineIdx);
  return this;
};
