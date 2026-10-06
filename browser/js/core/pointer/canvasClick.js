import * as shapeBuilder from '../line/shapeBuilder.js';
import { toggleLineSelection, updateMultiSelectStatus } from '../line/selection.js';
import { canvasCoords } from './canvasCoords.js';
import { CHANGE, changed } from '../app/changes.js';
import { breakChainAt, breakChainOnRepeat, noteDrop, dropHoldMs } from '../draw/chainBreak.js';

// One router for a canvas click: extend/close the stroke while drawing (a double-click or
// Ctrl/Cmd breaks the chain), Ctrl/Cmd inserts or adds a point, a plain click selects.

export const canvasClick = (app, e) => {
  if (!app.image) return;
  if (app.compareReadOnly()) return;
// Ctrl/⌘+Shift+click multi-select runs BEFORE the alt/shift early-returns below.
  if ((e.ctrlKey || e.metaKey) && e.shiftKey) {
    const { x, y } = canvasCoords(app, e.clientX, e.clientY);
    const nearPt = app.findNearestPointWithIdx(x, y);
    const idx = (nearPt && nearPt.lineIdx !== -1) ? nearPt.lineIdx : app.findLineAt(x, y);
    if (idx !== -1) toggleLineSelection(app, idx);
    return;
  }
// A click that ended a pan gesture or point drag.
  if (e.altKey) return;
  if (e.shiftKey) return;
  if (app.dragJustEnded) return;
  const { x, y } = canvasCoords(app, e.clientX, e.clientY);

  if (app.isDrawing) {
    if (app.drawMode === 'rect') return;
// A double-click's second click (detail 2; a double-tap's synthetic one too) never adds a point.
    if (e.detail >= 2) { breakChainOnRepeat(app, x, y); return; }
    app.strokeFx.release?.();

// Ctrl/Cmd+click on a committed segment inserts BETWEEN its endpoints rather than
// appending at the tail.
    if (e.ctrlKey || e.metaKey) {
      const nearSeg = app.findNearestSegmentWithIdx(x, y);
      if (nearSeg) {
        shapeBuilder.insertPointOnSegment(app, nearSeg.lineIdx, nearSeg.ptIdx2, x, y);
// Inserting shifts later indices right by one; keep the continuation tail anchored.
        if (nearSeg.lineIdx === app.continueLineIdx && nearSeg.ptIdx2 <= app.continueInsertIdx)
          app.continueInsertIdx++;
        return;
      }
      breakChainAt(app, x, y);
      return;
    }

// A click near the first point closes the stroke into a locked area.
    if (shapeBuilder.tryCloseShapeAt(app, x, y)) return;

    if (app.continueLineIdx >= 0 && app.lines[app.continueLineIdx]) {
      const line = app.lines[app.continueLineIdx];
      line.points.splice(app.continueInsertIdx, 0, { x, y });
      noteDrop(app, line, line.points[app.continueInsertIdx]);
      app.strokeFx.flyIn(line, app.continueInsertIdx, null, dropHoldMs(e));
      app.focusedPtIdx = app.continueInsertIdx;
      app.continueInsertIdx++;
      app.coordTable.update(line.points, app.continueLineIdx);
      app.renderer.redraw();
      changed(app, CHANGE.lines);
      return;
    }

    app.undonePoints = [];
    app.currentLine.points.push({ x, y });
    noteDrop(app, app.currentLine, app.currentLine.points.at(-1));
    app.strokeFx.flyIn(app.currentLine, app.currentLine.points.length - 1, null, dropHoldMs(e));
    app.renderer.redraw();
    changed(app, CHANGE.history);
    return;
  }

  if (e.ctrlKey || e.metaKey) {
    const nearSeg = app.findNearestSegmentWithIdx(x, y);
    if (nearSeg) {
      shapeBuilder.insertPointOnSegment(app, nearSeg.lineIdx, nearSeg.ptIdx2, x, y);
    } else {
      shapeBuilder.addConnectedPoint(app, x, y);
    }
    return;
  }

  app.selectedLines = [];
  updateMultiSelectStatus(app);

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
