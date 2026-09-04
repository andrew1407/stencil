// ── Alt-drag gesture engine (point / segment / whole-line) ────────────
// Extracted from drawingApp.js; each function takes the app and operates on its shared drag
// state (draggingPoint/Segment/Line + is* flags), which the mouse (pointerController.js) and
// touch (inputController.js) paths both drive through DrawingApp's thin delegators.

// ── Chaining: a closed area, and the ways back out of one ───────────────────
// A click-closed shape repeats its first point at the end; a rect is locked without one.
// "The ring" below is always the points minus that duplicate. Desktop twin: chainEdit.hpp.

// The line's points as a ring: the closing duplicate dropped, if it has one.
export const ringPoints = (points) => {
  const n = points.length;
  if (n >= 2 && points[0].x === points[n - 1].x && points[0].y === points[n - 1].y)
    return points.slice(0, n - 1);
  return points;
};

// Open a ring at vertex k: re-rooted to start there and run back to a copy of it, which
// becomes the free end — so the seam appears where the user pulled, not at point 0.
export const openRingAt = (points, k) => {
  const ring = ringPoints(points);
  const n = ring.length;
  if (n === 0) return points.map((p) => ({ x: p.x, y: p.y }));
  const out = [];
  for (let i = 0; i <= n; i++) {
    const p = ring[(((k + i) % n) + n) % n];
    out.push({ x: p.x, y: p.y });
  }
  return out;
};

// An open line has no area to paint, so its fill goes with the shape. 'transparent' is
// the app's "no fill", so re-closing the line comes up cleared rather than pre-filled.
export const clearFill = (line) => { line.fillColor = 'transparent'; };

// Unchain an area back into an open polyline. Returns whether anything changed.
export const unchainLine = (line) => {
  if (!line || !line.locked) return false;
  line.points = ringPoints(line.points).map((p) => ({ x: p.x, y: p.y }));
  line.locked = false;
  clearFill(line);
  return true;
};

// Where Alt+Ctrl+drag puts its new point: duplicated in place on a vertex, at the cursor
// on a segment body. A locked line is opened there first, so the same gesture that adds a
// point breaks a shape. Mutates `line`; returns the index to drag, or -1.
export const pullOutPoint = (line, target, x, y) => {
  if (!line || !target) return -1;
  const pts = line.points;
  if (line.locked) {
    const k = target.kind === 'point' ? target.ptIdx : target.ptIdx2;
    line.points = openRingAt(pts, k);
    line.locked = false;
    clearFill(line);
    // The seam is the last point; for a segment grab slide it onto the cursor.
    const last = line.points.length - 1;
    if (target.kind !== 'point') { line.points[last].x = x; line.points[last].y = y; }
    return last;
  }
  if (target.kind === 'point') {
    const p = pts[target.ptIdx];
    if (!p) return -1;
    pts.splice(target.ptIdx + 1, 0, { x: p.x, y: p.y });
    return target.ptIdx + 1;
  }
  const at = target.ptIdx2;
  if (at < 0 || at > pts.length) return -1;
  pts.splice(at, 0, { x, y });
  return at;
};

// Begin dragging a segment (shared by the mouse Alt-drag and the touch grab). Snapshots
// the two endpoints + the whole line so a mid-drag Shift can translate the shape.
// (x, y) are the grab point in canvas coords.
export function beginSegmentDrag(app, nearSeg, x, y) {
  const line = app.lines[nearSeg.lineIdx];
  app.isDraggingSegment = true;
  app.draggingSegment = {
    lineIdx: nearSeg.lineIdx, ptIdx1: nearSeg.ptIdx1, ptIdx2: nearSeg.ptIdx2,
    startX: x, startY: y,
    origPt1: { x: line.points[nearSeg.ptIdx1].x, y: line.points[nearSeg.ptIdx1].y },
    origPt2: { x: line.points[nearSeg.ptIdx2].x, y: line.points[nearSeg.ptIdx2].y },
    origPoints: line.points.map(p => ({ x: p.x, y: p.y })),
  };
}

// Move the currently-dragged point (dp = draggingPoint) to canvas coords (x, y) and
// refresh its coordinate row.
export function movePointTo(app, dp, x, y) {
  const line = dp.lineIdx === -1 ? app.currentLine : app.lines[dp.lineIdx];
  if (!line) return;
  line.points[dp.ptIdx].x = x;
  line.points[dp.ptIdx].y = y;
  app.renderer.redraw();
  app.coordTable.refreshCoordRow(dp.ptIdx);
}

// Finish a point drag: clear state, save history (only for a placed line), commit.
export function endPointDrag(app, dp, altKey) {
  app.isDraggingPoint = false;
  app.draggingPoint = null;
  if (dp && dp.lineIdx !== -1) app.saveHistory();
  finishDragGesture(app, altKey);
}

// Finish a segment drag: clear state, save history, commit.
export function endSegmentDrag(app, altKey) {
  app.isDraggingSegment = false;
  app.draggingSegment = null;
  app.saveHistory();
  finishDragGesture(app, altKey);
}

// Apply the active segment/whole-line drag at the cursor. For a SEGMENT drag `shiftKey`
// upgrades live: held → translate the entire line shape; released → just the grabbed
// segment's endpoints. A LINE drag always translates every point. All modes derive from
// the original snapshot, so toggling Shift never accumulates.
export function dragMove(app, clientX, clientY, shiftKey) {
  const { x, y } = app.canvasCoords(clientX, clientY);

  if (app.isDraggingSegment && app.draggingSegment) {
    const ds = app.draggingSegment;
    const line = app.lines[ds.lineIdx];
    if (!line) return;
    const dx = x - ds.startX;
    const dy = y - ds.startY;
    if (shiftKey) {
      line.points.forEach((p, i) => { p.x = ds.origPoints[i].x + dx; p.y = ds.origPoints[i].y + dy; });
      if (app.coordLineIdx === ds.lineIdx) app.coordTable.update(line.points, ds.lineIdx);
    } else {
      line.points.forEach((p, i) => { p.x = ds.origPoints[i].x; p.y = ds.origPoints[i].y; });
      line.points[ds.ptIdx1].x = ds.origPt1.x + dx;
      line.points[ds.ptIdx1].y = ds.origPt1.y + dy;
      line.points[ds.ptIdx2].x = ds.origPt2.x + dx;
      line.points[ds.ptIdx2].y = ds.origPt2.y + dy;
      app.coordTable.refreshCoordRow(ds.ptIdx1);
      app.coordTable.refreshCoordRow(ds.ptIdx2);
    }
    app.renderer.redraw();
    return;
  }

  if (app.isDraggingLine && app.draggingLine) {
    const dl = app.draggingLine;
    const line = app.lines[dl.lineIdx];
    if (!line) return;
    const dx = x - dl.startX;
    const dy = y - dl.startY;
    // Multi-select drag: translate EVERY selected line together (whole-line move).
    if (dl.multiOrig) {
      for (const { li, pts } of dl.multiOrig) {
        const l = app.lines[li];
        if (l) l.points.forEach((p, i) => { p.x = pts[i].x + dx; p.y = pts[i].y + dy; });
      }
      app.renderer.redraw();
      return;
    }
    // A line drag ALWAYS translates every point — degrading to the grabbed segment when
    // Shift reads false snaps most points back when Shift is released before the mouse.
    line.points.forEach((p, i) => { p.x = dl.origPoints[i].x + dx; p.y = dl.origPoints[i].y + dy; });
    app.renderer.redraw();
    if (app.coordLineIdx === dl.lineIdx) app.coordTable.update(line.points, dl.lineIdx);
    return;
  }
}

// Reset drag flags & cursor after any Alt-drag gesture (point/segment/line).
export function finishDragGesture(app, altKey) {
  app.dragJustEnded = true;
  setTimeout(() => { app.dragJustEnded = false; }, 50);
  app.canvas.style.cursor = altKey ? 'grab' : 'crosshair';
}
