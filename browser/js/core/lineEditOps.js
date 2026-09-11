// ── Point / line mutation ───────────────────────────────────────
// Split out of DrawingApp; shared by the coord table, the Lines tab and the console
// facade. Each one keeps the selection, the coord-table target and every cached hover
// consistent with the indices it just shifted, then saves one history entry.

// Set one point's x or y in crop-local pixels. lineIdx === -1 targets the
// in-progress currentLine (mirrors the coord table's target resolution).
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

// Remove one point; if that empties a committed line, drop the line too. Keeps the
// coord-table focus/active-line state consistent. Shared by the coord table UI and
// the console (Point.remove / Line.remove).
export const removePoint = (app, lineIdx, ptIdx) => {
  const line = lineIdx === -1 ? app.currentLine : app.lines[lineIdx];
  if (!line || !line.points[ptIdx]) return this;
  line.points.splice(ptIdx, 1);
  // Indices shifted: the cached canvas hover would ring a DIFFERENT point until the
  // next mousemove refreshes it.
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
  // Line indices shifted: drop every cached hover (canvas ring + list glow/tint).
  app.hoverPt = null;
  app.hoverLineIdx = -1;
  app.listHoverLineIdx = -1;
  // Keep the selection + coord-table target consistent with the now-shifted indices:
  // drop them if they pointed at the removed line, else shift down past it.
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

// Remove EVERY selected line at once — the multi-select counterpart of removeLine.
// Splices from the highest index down so the lower indices stay valid while removing,
// then clears the selection wholesale (each removed line was, by definition, selected).
// One history entry for the whole batch, so a single undo brings them all back.
export const removeSelectedLines = (app) => {
  const sel = app.selectedIndices()
    .filter(i => i >= 0 && i < app.lines.length)
    .sort((a, b) => b - a);
  if (!sel.length) return this;
  for (const idx of sel) {
    app.lines.splice(idx, 1);
    // The coord table can point at a line that is NOT part of the selection; keep its
    // index valid the same way removeLine does.
    if (app.coordLineIdx === idx) { app.coordLineIdx = -1; app.focusedPtIdx = -1; }
    else if (app.coordLineIdx > idx) app.coordLineIdx -= 1;
  }
  // Clear the selection inline rather than via deselectLine(), which resets coordLineIdx
  // unconditionally — that would throw away the shift just computed and blank the coord
  // table even when it targets a surviving, unselected line (removeLine keeps it too).
  app.selectedLineIdx = -1;
  app.selectedLines = [];
  app.hoveredPtIdx = -1;
  app.hoverPt = null;          // indices shifted — stale hover would ring the wrong point
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
