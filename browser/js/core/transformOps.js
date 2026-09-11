import { bboxCenterOf, rotatePointsAbout, flipPointsAbout } from './lineTransforms.js';

// ── Transforms over the selection ───────────────────────────────
// Rotate / flip / quarter-turn / nudge, split out of DrawingApp. The point maths is
// lineTransforms.js; this is the selection plumbing around it — which pivot, which lines,
// and the one debounced history save a burst of them collapses into.

// Debounced history+storage save shared by the in-place geometry transforms (rotate/flip):
// a burst of key-repeats or wheel steps collapses into a single undo step / persist.
const scheduleTransformSave = (app) => {
  clearTimeout(app.rotateSaveTimer);
  app.rotateSaveTimer = setTimeout(() => { app.saveHistory(); app.storage.save(); }, 280);
};

// Apply an in-place per-line transform `op(points, cx, cy)` to the current selection about the
// selection's bounding-box centre (≥2 selected → combined centre; exactly 1 → that line's
// centre). Redraws, refreshes the coord table for a focused single line, and schedules the
// debounced save. Shared by flipSelectedLine and the centre-pivot rotate path; no-op when the
// selection is empty or a single line has fewer than 2 points.
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
  // 2+ selected → rotate the whole set about their combined centre (the shared bbox path).
  const sel = app.selectedIndices();
  if (sel.length >= 2) {
    transformSelection(app, (pts, cx, cy) => rotatePointsAbout(pts, cx, cy, angle));
    return;
  }
  const line = app.lines[app.selectedLineIdx];
  if (!line || line.points.length < 2) return;
  // One selected: pivot on the focused point when there is one, else the line's bbox centre —
  // a rotate-only special case, so it doesn't go through #transformSelection's centre pivot.
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

// Flip the selected line(s) about the selection's bounding-box centre — the same pivot the
// arbitrary-angle rotate uses. `horizontal` mirrors left↔right, else top↔bottom. Debounced
// history save like rotateSelectedLine; no selection is a no-op.
export const flipSelectedLine = (app, horizontal) => {
  transformSelection(app, (pts, cx, cy) => flipPointsAbout(pts, horizontal, cx, cy));
};

// Rotate the selected line(s) a quarter turn (dir > 0 → +90° CW, dir < 0 → -90°) — reuses the
// arbitrary-angle rotate path so the pivot and debounced save are identical.
export const rotateSelectedLineQuarter = (app, dir) => {
  rotateSelectedLine(app, dir * (Math.PI / 2));
};

// Translate every selected line by (dx, dy) image-space px — the arrow-key nudge. Mirrors the
// drag-move translation (dragMove) but keyboard-driven; the history save is debounced (like
// rotateSelectedLine) so a burst of key-repeats collapses into one undo step.
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
