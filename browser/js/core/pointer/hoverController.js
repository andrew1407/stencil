// Canvas hover + double-click delete: the cursor, the hover ring, the readout/tooltip and
// the Lines-row tint. Bound through a one-per-frame rAF coalescer (ui/bindings/canvasPointer.js).
export const canvasMouseMove = (app, e) => {
// A coalesced pass older than the leave would re-hover an empty canvas.
  if (app.mouseLeftAt >= e.timeStamp) return;
  app.mouseOverCanvas = true;
  app.lastMouseClientX = e.clientX;
  app.lastMouseClientY = e.clientY;
// Only on a CHANGE: an unconditional cursor write invalidates layout the tooltip then re-reads.
  const setCursor = (c) => { if (app.canvas.style.cursor !== c) app.canvas.style.cursor = c; };

  if (!app.image) {
    app.tooltipMgr.hide();
    setCursor('default');
    app.updateCoordStatus();
    return;
  }

// Mid drag/hold the tooltip stays off (the same states tooltip.js refresh() excludes).
  if (app.isPanning || app.isDraggingPoint || app.isDraggingSegment ||
      app.isDraggingLine || app.isZoomRectDragging || app.isRectDrawDragging ||
      app.input.holdEngaged) {
    app.tooltipMgr.hide();
    return;
  }

  const { x, y } = app.canvasCoords(e.clientX, e.clientY);

// A passive readout of where the cursor IS, so it must come BEFORE the compare returns.
  app.updateCoordStatus(x, y);

// Over the movable divider: resize cursor only; dragging is handled in PointerController.
  if (!e.altKey && !e.shiftKey && !e.ctrlKey && !e.metaKey && !app.compareHoldOriginal &&
      (app.nearCompareDivider(e.clientX, e.clientY) || app.isDraggingCompareSplit)) {
    setCursor(app.compareMode === 'vertical' ? 'col-resize' : 'row-resize');
    app.tooltipMgr.hide();
    return;
  }

// Compare view is read-only for EDITING; the coordinate tooltip is display and still answers.
  if (app.compareReadOnly()) {
    setCursor('default');
    app.tooltipMgr.applyHover(e.clientX, e.clientY, x, y, e);
    return;
  }

// The hover ring on ANY line; the coord-table row highlight only for the shown line.
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

// The LINE under the cursor tints the matching Lines-list row (the reverse of the list-row
// hover glow); never scrolls the list.
  const overLineIdx = (nearPtIdx && nearPtIdx.lineIdx !== -1)
    ? nearPtIdx.lineIdx : app.findLineAt(x, y);
  if (overLineIdx !== app.hoverLineIdx) {
    app.hoverLineIdx = overLineIdx;
    app.applyLinesListHover();
  }

// Alt: drag-ready cursors, no tooltip.
  if (e.altKey) {
    if (e.shiftKey) {
      setCursor(overLineIdx !== -1 ? 'move' : 'grab');
    } else {
// point drag > segment drag > pan
      const nearSeg = nearPtIdx ? null : app.findNearestSegmentWithIdx(x, y);
      setCursor((nearPtIdx || nearSeg) ? 'move' : 'grab');
    }
    app.tooltipMgr.hide();
    return;
  }

  if ((e.ctrlKey || e.metaKey) && !e.shiftKey) {
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
  if (app.compareReadOnly()) return;
  if (e.altKey) return;

  const { x, y } = app.canvasCoords(e.clientX, e.clientY);

  const idx = app.findLineAt(x, y);
  if (idx !== -1) {
    app.lines.splice(idx, 1);
    app.hoverPt = null;
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

