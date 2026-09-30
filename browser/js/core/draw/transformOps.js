import { perFrame } from '../../utils.js';
import { bboxCenterOf, rotatePointsAbout, flipPointsAbout } from '../line/transforms.js';
import constants from '../../../../common/config/constants.json' with { type: 'json' };
import { selectedIndices } from '../line/selection.js';

// Rotate / flip / quarter-turn / nudge over the selection: which pivot, which lines, and
// the one debounced history save a burst collapses into (the point maths is transforms.js).

// A burst repaints the stage and the points table once per frame; the table only if it still lists that line.
const coordRefresh = new WeakMap();
const refreshShown = (app, li) => {
  let run = coordRefresh.get(app);
  if (!run) {
    run = perFrame((i) => { if (app.coordLineIdx === i && app.lines[i]) app.coordTable.update(app.lines[i].points, i); });
    coordRefresh.set(app, run);
  }
  app.renderer.requestRedraw();
  if (app.coordLineIdx === li && app.lines[li]) run(li);
};

// A burst of key-repeats or wheel steps collapses into a single undo step / persist.
const scheduleTransformSave = (app) => {
  clearTimeout(app.rotateSaveTimer);
  app.rotateSaveTimer = setTimeout(() => { app.saveHistory(); app.storage.saveSoon(); }, constants.DEBOUNCE.editCommitMs);
};

// Apply `op(points, cx, cy)` about the selection's bounding-box centre (≥2 selected →
// combined centre). No-op when empty or a single line has fewer than 2 points.
const transformSelection = (app, op) => {
  const sel = selectedIndices(app);
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
  }
  refreshShown(app, sel.length >= 2 ? -1 : app.selectedLineIdx);
  scheduleTransformSave(app);
};

export const rotateSelectedLine = (app, angle) => {
  const sel = selectedIndices(app);
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
  refreshShown(app, app.selectedLineIdx);
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
  const sel = selectedIndices(app);
  if (!sel.length) return this;
  for (const li of sel) {
    const line = app.lines[li];
    if (line) line.points.forEach(p => { p.x += dx; p.y += dy; });
  }
  refreshShown(app, sel.includes(app.coordLineIdx) ? app.coordLineIdx : -1);
  scheduleTransformSave(app);
  return this;
};
