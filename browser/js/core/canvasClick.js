import * as shapeBuilder from './shapeBuilder.js';
import { toggleLineSelection } from './lineSelection.js';

// ── What a click on the canvas means ────────────────────────────
// Split out of DrawingApp. One router: while drawing it extends/closes the stroke, a
// Ctrl/Cmd click inserts or adds a point, and a plain click selects the point or line
// under the cursor (or drops the selection). Reached as app.canvasClick(e).

export const canvasClick = (app, e) => {
  // No image → the canvas is an empty void; don't let clicks drop points.
  if (!app.image) return;
  // Compare view is read-only — clicks never add/select/edit.
  if (app.compareReadOnly()) return;
  // Ctrl/⌘+Shift+click → multi-select: add/toggle the clicked line (handled BEFORE the alt/shift
  // early-returns below). Clicking empty space keeps the current set.
  if ((e.ctrlKey || e.metaKey) && e.shiftKey) {
    const { x, y } = app.canvasCoords(e.clientX, e.clientY);
    const nearPt = app.findNearestPointWithIdx(x, y);
    const idx = (nearPt && nearPt.lineIdx !== -1) ? nearPt.lineIdx : app.findLineAt(x, y);
    if (idx !== -1) toggleLineSelection(app, idx);
    return;
  }
  // Ignore click that ended a pan gesture or point drag
  if (e.altKey) return;
  if (e.shiftKey) return; // Shift+drag is for zoom-area rect
  if (app.dragJustEnded) return;
  const { x, y } = app.canvasCoords(e.clientX, e.clientY);

  if (app.isDrawing) {
    if (app.drawMode === 'rect') return; // rect areas are created by dragging

    // Ctrl/Cmd+click on an existing committed segment → insert a point BETWEEN
    // that segment's two endpoints (same as Ctrl+click outside drawing mode),
    // instead of appending it at the line's tail with a connecting segment.
    if (e.ctrlKey || e.metaKey) {
      const nearSeg = app.findNearestSegmentWithIdx(x, y);
      if (nearSeg) {
        app.insertPointOnSegment(nearSeg.lineIdx, nearSeg.ptIdx2, x, y);
        // Inserting shifts later indices right by one; keep the continuation
        // tail anchored to the same logical spot on the line we're extending.
        if (nearSeg.lineIdx === app.continueLineIdx && nearSeg.ptIdx2 <= app.continueInsertIdx)
          app.continueInsertIdx++;
        return;
      }
      // No segment under the cursor → fall through to normal drawing behavior.
    }

    // A click near the first point closes the stroke into a locked area — whichever
    // stroke is being drawn (continued or fresh).
    if (app.tryCloseShapeAt(x, y)) return;

    // Continuation drawing: extend the selected line at the insert point
    if (app.continueLineIdx >= 0 && app.lines[app.continueLineIdx]) {
      const line = app.lines[app.continueLineIdx];
      line.points.splice(app.continueInsertIdx, 0, { x, y });
      app.strokeFx.flyIn(line, app.continueInsertIdx);
      app.focusedPtIdx = app.continueInsertIdx;
      app.continueInsertIdx++;
      app.coordTable.update(line.points, app.continueLineIdx);
      app.renderer.redraw();
      app.updateButtons();
      return;
    }

    app.undonePoints = [];
    app.currentLine.points.push({ x, y });
    app.strokeFx.flyIn(app.currentLine, app.currentLine.points.length - 1);
    app.renderer.redraw();
    app.updateButtons();
    return;
  }

  // Ctrl/Cmd click → add or insert a point
  if (e.ctrlKey || e.metaKey) {
    const nearSeg = app.findNearestSegmentWithIdx(x, y);
    if (nearSeg) {
      // Hovering a line → insert a point between its two connecting points
      app.insertPointOnSegment(nearSeg.lineIdx, nearSeg.ptIdx2, x, y);
    } else {
      // Empty space → add a new point (connected to selection if any)
      shapeBuilder.addConnectedPoint(this, x, y);
    }
    return;
  }

  // A plain click leaves multi-select mode (back to single-line selection).
  app.selectedLines = [];
  app.updateMultiSelectStatus();

  // Non-drawing mode: priority 1 — click on a point of any committed line
  // → select that line, focus the clicked point in the coord table
  const nearPt = app.findNearestPointWithIdx(x, y);
  if (nearPt && nearPt.lineIdx !== -1) {
    app.selectedLineIdx = nearPt.lineIdx;
    app.showSelectionPanel(app.lines[nearPt.lineIdx]);
    app.coordLineIdx = nearPt.lineIdx;
    app.focusedPtIdx = nearPt.ptIdx;
    app.coordTable.update(app.lines[nearPt.lineIdx].points, nearPt.lineIdx);
    app.renderer.redraw();
    const row = app.coordinatesBody.querySelector(`tr[data-pt-idx="${nearPt.ptIdx}"]`);
    if (row && typeof row.scrollIntoView === 'function') {
      row.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
    }
    return;
  }

  // Priority 2 — click on a line segment → select the line
  const idx = app.findLineAt(x, y);
  if (idx !== -1) {
    app.selectedLineIdx = idx;
    app.showSelectionPanel(app.lines[idx]);
    app.coordLineIdx = idx;
    app.focusedPtIdx = -1;
    app.coordTable.update(app.lines[idx].points, idx);
  } else {
    app.deselectLine();
  }
  app.renderer.redraw();
};
