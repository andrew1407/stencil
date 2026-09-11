// ── Canvas hover + double-click delete ──────────────────────────
// Extracted from drawingApp.js. Everything a pointer move over the canvas decides: the
// cursor, the hover ring, the coordinate readout/tooltip, and the Lines-row tint.

// Bound through a one-per-frame rAF coalescer (ui/bindings/canvasPointer.js), so a
// burst of moves runs this — two full point scans, a line scan and a redraw — once.
export const canvasMouseMove = (app, e) => {
  // Coalesced to one pass per frame, so a move can still be queued when the pointer leaves:
  // the leave stamps its own time, and a pass older than it would re-hover an empty canvas.
  if (app.mouseLeftAt >= e.timeStamp) return;
  app.mouseOverCanvas = true;
  app.lastMouseClientX = e.clientX;
  app.lastMouseClientY = e.clientY;
  // One cursor write per move, and only when it CHANGES: an unconditional write on every
  // event invalidates layout, which the tooltip's own reads below then have to redo.
  const setCursor = (c) => { if (app.canvas.style.cursor !== c) app.canvas.style.cursor = c; };

  // No image → empty void: no coordinate tooltip, no hover cursor, idle status.
  if (!app.image) {
    app.tooltipMgr.hide();
    setCursor('default');
    app.updateCoordStatus();
    return;
  }

  // Mid drag/hold, the tooltip has no business on screen (same states tooltip.js
  // refresh() excludes) — drop one already showing and don't offer a new one.
  if (app.isPanning || app.isDraggingPoint || app.isDraggingSegment ||
      app.isDraggingLine || app.isZoomRectDragging || app.isRectDrawDragging ||
      app.input.holdEngaged) {
    app.tooltipMgr.hide();
    return;
  }

  const { x, y } = app.canvasCoords(e.clientX, e.clientY);

  // Persistent cursor-coordinate readout (mirrors the desktop status bar). A passive
  // readout of where the cursor IS, so it must come BEFORE the compare returns below —
  // otherwise the strip freezes for the whole compare session.
  app.updateCoordStatus(x, y);

  // Split compare: show a resize cursor over the movable divider (skip the normal hover
  // cursor + tooltip so the affordance reads clearly). Dragging is handled in PointerController.
  if (!e.altKey && !e.shiftKey && !e.ctrlKey && !e.metaKey && !app.compareHoldOriginal &&
      (app.nearCompareDivider(e.clientX, e.clientY) || app.isDraggingCompareSplit)) {
    setCursor(app.compareMode === 'vertical' ? 'col-resize' : 'row-resize');
    app.tooltipMgr.hide();
    return;
  }

  // Compare view is read-only for EDITING — no hover ring, no edit cursor, no drag.
  // The coordinate tooltip is display, so it still answers here, for a point or line
  // visible in the edited half (tooltipMgr.applyHover applies that gate).
  if (app.compareReadOnly()) {
    setCursor('default');
    app.tooltipMgr.applyHover(e.clientX, e.clientY, x, y, e);
    return;
  }

  // Track hovered point on ANY line (drives the hover ring), and keep the
  // coord-table row highlight in sync when the point belongs to the shown line.
  const nearPtIdx = app.findNearestPointWithIdx(x, y);
  const newHoverPt = nearPtIdx ? { lineIdx: nearPtIdx.lineIdx, ptIdx: nearPtIdx.ptIdx } : null;
  const hoverChanged =
    (!!app.hoverPt !== !!newHoverPt) ||
    (app.hoverPt && newHoverPt &&
      (app.hoverPt.lineIdx !== newHoverPt.lineIdx || app.hoverPt.ptIdx !== newHoverPt.ptIdx));
  app.hoverPt = newHoverPt;
  let newHover = -1;
  if (nearPtIdx && nearPtIdx.lineIdx === app.coordLineIdx) newHover = nearPtIdx.ptIdx;
  const rowChanged = newHover !== app.hoveredPtIdx;
  if (rowChanged) { app.hoveredPtIdx = newHover; app.coordTable.applyRowHighlight(); }
  if (hoverChanged || rowChanged) app.renderer.redraw();

  // Track the LINE under the cursor too (a point hit names its line; else a stroke hit):
  // it tints the matching Lines-list row — the reverse of the list-row hover glow. Never
  // scrolls the list.
  const overLineIdx = (nearPtIdx && nearPtIdx.lineIdx !== -1)
    ? nearPtIdx.lineIdx : app.findLineAt(x, y);
  if (overLineIdx !== app.hoverLineIdx) {
    app.hoverLineIdx = overLineIdx;
    app.applyLinesListHover();
  }

  // Alt key held → drag-ready cursors, no tooltip
  if (e.altKey) {
    if (e.shiftKey) {
      // Alt+Shift: whole-line drag mode
      setCursor(overLineIdx !== -1 ? 'move' : 'grab');
    } else {
      // Alt: point drag > segment drag > pan
      const nearSeg = nearPtIdx ? null : app.findNearestSegmentWithIdx(x, y);
      setCursor((nearPtIdx || nearSeg) ? 'move' : 'grab');
    }
    app.tooltipMgr.hide();
    return;
  }

  if ((e.ctrlKey || e.metaKey) && !e.shiftKey) {
    // Ctrl → point-add mode
    setCursor('copy');
  } else if (e.shiftKey && !app.isZoomRectDragging) {
    setCursor('zoom-in');
  } else if (app.isDrawing && app.drawMode === 'rect') {
    setCursor('crosshair');
  } else if (!app.isDrawing) {
    setCursor(overLineIdx !== -1 ? 'pointer' : 'crosshair');
  } else {
    setCursor('crosshair');
  }

  app.tooltipMgr.applyHover(e.clientX, e.clientY, x, y, e);
};

export const canvasDblClick = (app, e) => {
  if (app.isDrawing) return;
  if (app.compareReadOnly()) return; // read-only compare view — no double-click delete
  if (e.altKey) return; // Alt+dblclick is reserved for zoom reset

  const { x, y } = app.canvasCoords(e.clientX, e.clientY);

  const idx = app.findLineAt(x, y);
  if (idx !== -1) {
    app.lines.splice(idx, 1);
    app.hoverPt = null;        // indices shifted (see removeLine)
    app.hoverLineIdx = -1;
    app.listHoverLineIdx = -1;
    if (app.selectedLineIdx === idx) app.deselectLine(false);
    else if (app.selectedLineIdx > idx) app.selectedLineIdx--;
    app.saveHistory();
    app.renderer.redraw();
    app.updateButtons();
    app.coordTable.update();
  }
};

