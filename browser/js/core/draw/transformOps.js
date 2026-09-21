import { bboxCenterOf, rotatePointsAbout, flipPointsAbout } from '../line/transforms.js';

// Rotate / flip / quarter-turn / nudge over the selection: which pivot, which lines, and
// the one debounced history save a burst collapses into (the point maths is transforms.js).

// A burst of key-repeats or wheel steps collapses into a single undo step / persist.
const scheduleTransformSave = (app) => {
  clearTimeout(app.rotateSaveTimer);
  app.rotateSaveTimer = setTimeout(() => { app.saveHistory(); app.storage.save(); }, 280);
};

// Apply `op(points, cx, cy)` about the selection's bounding-box centre (≥2 selected →
// combined centre). No-op when empty or a single line has fewer than 2 points.
const transformSelection = (app, op) => {
  const sel = app.selectedIndices();
  if (sel.length >= 2) {
    const lines = sel.map((i) => app.lines[i]).filter((l) => l && l.points.length);
    if (!lines.length) return;
    const { x: cx, y: cy } = bboxCenterOf(lines.flatMap((l) => l.points));
    for (const l of lines) op(l.points, cx, cy);
  } else {
    const line = app.lines[app.selectedLineIdx];
    if (!line || line.points.length < 2) return;
    const { x: cx, y: cy } = bboxCenterOf(line.points);
    op(line.points, cx, cy);
    if (app.coordLineIdx === app.selectedLineIdx) app.coordTable.update(line.points, app.selectedLineIdx);
  }
  app.renderer.redraw();
  scheduleTransformSave(app);
};

export const rotateSelectedLine = (app, angle) => {
  const sel = app.selectedIndices();
  if (sel.length >= 2) {
    transformSelection(app, (pts, cx, cy) => rotatePointsAbout(pts, cx, cy, angle));
    return;
  }
  const line = app.lines[app.selectedLineIdx];
  if (!line || line.points.length < 2) return;
  // One selected: pivot on the focused point when there is one, else the bbox centre.
  let cx;
  let cy;
  if (app.coordLineIdx === app.selectedLineIdx && app.focusedPtIdx >= 0
      && line.points[app.focusedPtIdx]) {
    cx = line.points[app.focusedPtIdx].x;
    cy = line.points[app.focusedPtIdx].y;
  } else {
    ({ x: cx, y: cy } = bboxCenterOf(line.points));
  }
  rotatePointsAbout(line.points, cx, cy, angle);
  app.renderer.redraw();
  if (app.coordLineIdx === app.selectedLineIdx) app.coordTable.update(line.points, app.selectedLineIdx);
  scheduleTransformSave(app);
};

// `horizontal` mirrors left↔right, else top↔bottom.
export const flipSelectedLine = (app, horizontal) => {
  transformSelection(app, (pts, cx, cy) => flipPointsAbout(pts, horizontal, cx, cy));
};

// dir > 0 → +90° CW, dir < 0 → -90°; the same pivot and debounced save as the angle rotate.
export const rotateSelectedLineQuarter = (app, dir) => {
  rotateSelectedLine(app, dir * (Math.PI / 2));
};

// The arrow-key nudge, in image-space px; a burst of key-repeats collapses into one undo step.
export const nudgeSelected = (app, dx, dy) => {
  if (!dx && !dy) return this;
  const sel = app.selectedIndices();
  if (!sel.length) return this;
  for (const li of sel) {
    const line = app.lines[li];
    if (line) line.points.forEach(p => { p.x += dx; p.y += dy; });
  }
  app.renderer.redraw();
  if (sel.includes(app.coordLineIdx) && app.lines[app.coordLineIdx])
    app.coordTable.update(app.lines[app.coordLineIdx].points, app.coordLineIdx);
  scheduleTransformSave(app);
  return this;
};
