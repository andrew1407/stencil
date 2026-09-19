// Closing a stroke into an area, inserting + connecting points, rects. Each takes the app
// and mutates its shared drawing state; the click path (canvasClick) and hold-to-draw
// (inputController) both come through here.
import { notify } from '../utils.js';
import { resolveInsertIdx } from './layout.js';
import { shouldCloseShape } from './lineTransforms.js';
import { strokeFoot } from '../ui/motion.js';

// A screen radius divided by the zoom, zoomed out only: core adds its own +8, so hand it the
// size that makes the total screen-constant (at 25% a fixed image-pixel radius was unhittable).
const CLOSE_SLACK = 8;
export const closeGrabSize = (app, line) => {
  const ps = line.pointSize ?? app.pointSize;
  const scale = app.scale || 1;
  if (scale >= 1) return ps;
  const slack = CLOSE_SLACK;
  return Math.max(ps, (ps + slack) / scale - slack);
};

// The one close route: the click path and hold-to-draw both come here.
export const tryCloseShapeAt = (app, x, y) => {
  if (!app.isDrawing) return false;
  if (app.continueLineIdx >= 0 && app.lines[app.continueLineIdx]) {
    const line = app.lines[app.continueLineIdx];
    if (!shouldCloseShape(line.points, x, y, closeGrabSize(app, line))) return false;
    closeContinuedShape(app);
    return true;
  }
  if (!app.currentLine) return false;
  if (!shouldCloseShape(app.currentLine.points, x, y, closeGrabSize(app, app.currentLine)))
    return false;
  closeCurrentShape(app);
  return true;
};

export const closeCurrentShape = (app) => {
  closeShape(app, { line: app.currentLine, isContinuation: false });
};

export const closeContinuedShape = (app) => {
  closeShape(app, { line: app.lines[app.continueLineIdx], idx: app.continueLineIdx, isContinuation: true });
};

// Append a coincident closing point, lock + default-fill, commit, select the area.
export const closeShape = (app, { line, idx, isContinuation }) => {
  if (!line || line.points.length < 3) return;
  line.points.push({ x: line.points[0].x, y: line.points[0].y });
  line.locked = true;
  if (line.fillColor === undefined) line.fillColor = 'transparent';
  let areaIdx;
  if (isContinuation) {
    app.continueLineIdx = -1;
    app.continueInsertIdx = -1;
    areaIdx = idx;
  } else {
    app.lines.push(line);
    areaIdx = app.lines.length - 1;
  }
  app.currentLine = null;
  app.isDrawing = false;
  // Ends like stopDrawingMode: the coordinate table follows it, nothing is selected.
  app.coordLineIdx = areaIdx;
  app.coordTable.update(app.lines[areaIdx].points, areaIdx);
  // A CONTINUED shape's bar is already up: repopulate it (it grew a Fill control) without
  // reopening, so no animation replays.
  if (app.selectedLineIdx === areaIdx) app.showSelectionPanel(app.lines[areaIdx]);
  app.saveHistory();
  app.renderer.redraw();
  app.updateButtons();
  notify('Shape closed — locked area created', 'ok');
};

export const insertPointOnSegment = (app, lineIdx, insertIdx, x, y) => {
  const line = app.lines[lineIdx];
  if (!line) return;
  line.points.splice(insertIdx, 0, { x, y });
  // From its own foot on the old straight line, so the bend grows rather than appearing.
  app.strokeFx.flyIn(line, insertIdx, strokeFoot(line.points[insertIdx - 1], line.points[insertIdx + 1], x, y));
  app.selectedLineIdx = lineIdx;
  app.coordLineIdx = lineIdx;
  app.focusedPtIdx = insertIdx;
  app.showSelectionPanel(line);
  app.coordTable.update(line.points, lineIdx);
  app.saveHistory();
  app.renderer.redraw();
  app.updateButtons();
};

// A new standalone point — or, with a line/point selected, connected to that line's last
// (or focused) point, inheriting its style.
export const addConnectedPoint = (app, x, y) => {
  if (app.selectedLineIdx >= 0 && app.lines[app.selectedLineIdx]) {
    const line = app.lines[app.selectedLineIdx];
    const insertIdx = resolveInsertIdx(line, {
      coordLineIdx: app.coordLineIdx,
      selectedLineIdx: app.selectedLineIdx,
      focusedPtIdx: app.focusedPtIdx
    });
    line.points.splice(insertIdx, 0, { x, y });
    app.strokeFx.flyIn(line, insertIdx);
    app.coordLineIdx = app.selectedLineIdx;
    app.focusedPtIdx = insertIdx;
    app.showSelectionPanel(line);
    app.coordTable.update(line.points, app.selectedLineIdx);
    app.saveHistory();
    app.renderer.redraw();
    app.updateButtons();
    return;
  }
  const newLine = {
    points: [{ x, y }],
    color: app.color,
    pointColor: app.pointColor || app.color,
    thickness: app.thickness,
    pointSize: app.pointSize,
    style: app.style
  };
  app.lines.push(newLine);
  app.strokeFx.flyIn(newLine, 0);
  const idx = app.lines.length - 1;
  app.selectedLineIdx = idx;
  app.coordLineIdx = idx;
  app.focusedPtIdx = 0;
  app.showSelectionPanel(newLine);
  app.coordTable.update(newLine.points, idx);
  app.saveHistory();
  app.renderer.redraw();
  app.updateButtons();
};

// 4 corner points, locked/fillable. With a line selected the corners are appended to it
// (connecting to its last/focused point); otherwise a new locked line.
export const createRect = (app, x1, y1, x2, y2, connect = false) => {
  const xa = Math.min(x1, x2);
  const xb = Math.max(x1, x2);
  const ya = Math.min(y1, y2);
  const yb = Math.max(y1, y2);
  const corners = [
    { x: xa, y: ya }, { x: xb, y: ya },
    { x: xb, y: yb }, { x: xa, y: yb }
  ];
  // Continuation drawing → append the corners to the line being extended.
  if (app.continueLineIdx >= 0 && app.lines[app.continueLineIdx]) {
    const line = app.lines[app.continueLineIdx];
    const insertIdx = app.continueInsertIdx;
    line.points.splice(insertIdx, 0, ...corners);
    app.strokeFx.flyInRange(line, insertIdx, corners.length);
    app.continueInsertIdx = insertIdx + corners.length;
    app.coordLineIdx = app.continueLineIdx;
    app.focusedPtIdx = app.continueInsertIdx - 1;
    app.coordTable.update(line.points, app.continueLineIdx);
    app.saveHistory();
    app.renderer.redraw();
    app.updateButtons();
    return;
  }
  if (connect && app.selectedLineIdx >= 0 && app.lines[app.selectedLineIdx]) {
    const line = app.lines[app.selectedLineIdx];
    const insertIdx = resolveInsertIdx(line, {
      coordLineIdx: app.coordLineIdx,
      selectedLineIdx: app.selectedLineIdx,
      focusedPtIdx: app.focusedPtIdx
    });
    line.points.splice(insertIdx, 0, ...corners);
    app.strokeFx.flyInRange(line, insertIdx, corners.length);
    app.coordLineIdx = app.selectedLineIdx;
    app.focusedPtIdx = insertIdx;
    app.showSelectionPanel(line);
    app.coordTable.update(line.points, app.selectedLineIdx);
    app.saveHistory();
    app.renderer.redraw();
    app.updateButtons();
    return;
  }
  const rect = {
    points: corners,
    color: app.color,
    pointColor: app.pointColor || app.color,
    thickness: app.thickness,
    pointSize: app.pointSize,
    style: app.style,
    locked: true,
    fillColor: 'transparent'
  };
  app.lines.push(rect);
  app.strokeFx.flyInRange(rect, 0, corners.length);
  const idx = app.lines.length - 1;
  app.selectedLineIdx = idx;
  app.coordLineIdx = idx;
  app.focusedPtIdx = -1;
  app.showSelectionPanel(rect);
  app.coordTable.update(rect.points, idx);
  app.saveHistory();
  app.renderer.redraw();
  app.updateButtons();
};