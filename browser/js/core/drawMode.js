import { notify } from '../utils.js';
import { resolveInsertIdx } from './layout.js';

// Start / stop drawing: starting either continues the selected line (adopting its style) or
// opens a fresh stroke; stopping commits. The button faces are ui/drawToggleUI.js.

export const startDrawingMode = (app, opts = {}) => {
  if (app.compareReadOnly()) return;
  if (!app.image) {
    notify('Please upload an image first', 'fail');
    return;
  }
  app.isDrawing = true;

// Continuation: new points/rects become part of the selected line and adopt its style.
  if (opts.connect !== false && app.selectedLineIdx >= 0 && app.lines[app.selectedLineIdx]) {
    app.continueLineIdx = app.selectedLineIdx;
    const line = app.lines[app.continueLineIdx];
    app.continueInsertIdx = resolveInsertIdx(line, {
      coordLineIdx: app.coordLineIdx,
      selectedLineIdx: app.selectedLineIdx,
      focusedPtIdx: app.focusedPtIdx
    });
    app.currentLine = null;
    app.undonePoints = [];
    app.coordLineIdx = app.continueLineIdx;
    app.coordTable.update(line.points, app.continueLineIdx);
    app.updateButtons();
    app.renderer.redraw();
    notify('Continuing selected line — new points connect to it', 'info');
    return;
  }

  app.continueLineIdx = -1;
  app.continueInsertIdx = -1;
  app.currentLine = {
    points: [],
    color: app.color,
// Resolved at draw time so a later line-colour change never recolours drawn points.
    pointColor: app.pointColor || app.color,
    thickness: app.thickness,
    pointSize: app.pointSize,
    style: app.style
  };
  if (!opts.keepSelection) {
    app.selectedLineIdx = -1;
    app.hideSelectionPanels();
  }
  app.undonePoints = [];
  app.updateButtons();
  app.renderer.redraw();
};

export const setDrawMode = (app, mode) => {
  app.drawMode = (mode === 'rect') ? 'rect' : 'line';
  app.syncDrawModeUI();
};

export const stopDrawingMode = (app) => {
// Continuation: the line is already in app.lines — commit & reset.
  if (app.continueLineIdx >= 0) {
    const li = app.continueLineIdx;
    app.continueLineIdx = -1;
    app.continueInsertIdx = -1;
    app.currentLine = null;
    app.isDrawing = false;
    if (app.lines[li]) app.coordTable.update(app.lines[li].points, li);
    app.saveHistory();
    app.renderer.redraw();
    app.updateButtons();
    return;
  }
  if (app.currentLine && app.currentLine.points.length > 0) {
    if (app.currentLine.points.length > 1) {
      app.lines.push(app.currentLine);
      app.coordLineIdx = app.lines.length - 1;
      app.saveHistory();
    } else {
      app.coordLineIdx = -1;
    }
    app.coordTable.update(app.currentLine.points, app.coordLineIdx);
  } else {
    app.coordTable.update();
  }
  app.currentLine = null;
  app.isDrawing = false;
  app.renderer.redraw();
  app.updateButtons();
};
