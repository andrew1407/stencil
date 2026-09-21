import * as shapeBuilder from '../line/shapeBuilder.js';
import { toggleLineSelection } from '../line/lineSelection.js';

// One router for a canvas click: extend/close the stroke while drawing, Ctrl/Cmd inserts
// or adds a point, a plain click selects the point or line under the cursor.

export const canvasClick = (app, e) => {
  if (!app.image) return;
  if (app.compareReadOnly()) return;
// Ctrl/⌘+Shift+click multi-select runs BEFORE the alt/shift early-returns below.
  if ((e.ctrlKey || e.metaKey) && e.shiftKey) {
    const { x, y } = app.canvasCoords(e.clientX, e.clientY);
    const nearPt = app.findNearestPointWithIdx(x, y);
    const idx = (nearPt && nearPt.lineIdx !== -1) ? nearPt.lineIdx : app.findLineAt(x, y);
    if (idx !== -1) toggleLineSelection(app, idx);
    return;
  }
// A click that ended a pan gesture or point drag.
  if (e.altKey) return;
  if (e.shiftKey) return;
  if (app.dragJustEnded) return;
  const { x, y } = app.canvasCoords(e.clientX, e.clientY);

  if (app.isDrawing) {
    if (app.drawMode === 'rect') return;

// Ctrl/Cmd+click on a committed segment inserts BETWEEN its endpoints rather than
// appending at the tail.
    if (e.ctrlKey || e.metaKey) {
      const nearSeg = app.findNearestSegmentWithIdx(x, y);
      if (nearSeg) {
        app.insertPointOnSegment(nearSeg.lineIdx, nearSeg.ptIdx2, x, y);
// Inserting shifts later indices right by one; keep the continuation tail anchored.
        if (nearSeg.lineIdx === app.continueLineIdx && nearSeg.ptIdx2 <= app.continueInsertIdx)
          app.continueInsertIdx++;
        return;
      }
    }

// A click near the first point closes the stroke into a locked area.
    if (app.tryCloseShapeAt(x, y)) return;

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

  if (e.ctrlKey || e.metaKey) {
    const nearSeg = app.findNearestSegmentWithIdx(x, y);
    if (nearSeg) {
      app.insertPointOnSegment(nearSeg.lineIdx, nearSeg.ptIdx2, x, y);
    } else {
      shapeBuilder.addConnectedPoint(this, x, y);
    }
    return;
  }

  app.selectedLines = [];
  app.updateMultiSelectStatus();

// Priority 1: a point of any committed line → select it, focus the point in the table.
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

// Priority 2: a line segment.
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
