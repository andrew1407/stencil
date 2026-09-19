// What every mouse gesture does on the release: the compare divider, the rect-draw and
// zoom-rect bands, a point/segment/whole-line drag, and the end of a pan.

// Suppress the trailing click so it is not a new point.
const suppressTrailingClick = (app) => {
  app.dragJustEnded = true;
  setTimeout(() => { app.dragJustEnded = false; }, 50);
};

const endCompareSplit = (app) => {
  app.isDraggingCompareSplit = false;
  app.canvas.style.cursor = app.isDrawing ? 'crosshair' : 'default';
  suppressTrailingClick(app);
};

const endRectDraw = (app) => {
  app.isRectDrawDragging = false;
  app.zoomPan.hideZoomRectOverlay();
  const s = app.rectDrawStart;
  const en = app.rectDrawEnd;
  app.rectDrawStart = null; app.rectDrawEnd = null;
  if (s && en) {
    const w = Math.abs(en.imgX - s.imgX);
    const h = Math.abs(en.imgY - s.imgY);
    if (w > 3 && h > 3) {
      app.createRect(s.imgX, s.imgY, en.imgX, en.imgY, false);
    }
  }
  // Stay in rect-drawing mode; suppress the trailing click.
  suppressTrailingClick(app);
};

const frameZoomRect = (app, viewport, availBox, s, en) => {
  const x1 = Math.min(s.imgX, en.imgX);
  const y1 = Math.min(s.imgY, en.imgY);
  const rectW = Math.max(s.imgX, en.imgX) - x1;
  const rectH = Math.max(s.imgY, en.imgY) - y1;
  if (rectW <= 4 || rectH <= 4) return;
  const { w: availW, h: availH } = availBox();
  const newScale = Math.min(availW / rectW, availH / rectH, 5);

  // No transition, so scrollLeft/scrollTop see the final canvas size at once.
  app.canvas.classList.add('zoom-no-transition');
  app.zoomPan.setZoom(newScale, false);
  // Force a synchronous layout before assigning scrollLeft/scrollTop.
  void app.canvas.getBoundingClientRect();
  if (viewport) {
    viewport.scrollLeft = Math.max(0, x1 * newScale - (availW - rectW * newScale) / 2);
    viewport.scrollTop = Math.max(0, y1 * newScale - (availH - rectH * newScale) / 2);
  }
  requestAnimationFrame(() => {
    app.canvas.classList.remove('zoom-no-transition');
    if (app.image) app.storage.save();
  });
};

const endZoomRect = (app, viewport, availBox) => {
  app.isZoomRectDragging = false;
  app.zoomPan.hideZoomRectOverlay();
  const s = app.zoomRectStart;
  const en = app.zoomRectEnd;
  if (s && en) frameZoomRect(app, viewport, availBox, s, en);
  app.zoomRectStart = null;
  app.zoomRectEnd = null;
  app.canvas.style.cursor = 'crosshair';
};

const endPan = (app, e) => {
  app.isPanning = false;
  if (app.isDrawing) {
    app.canvas.style.cursor = 'crosshair';
  } else {
    const { x, y } = app.canvasCoords(e.clientX, e.clientY);
    const overLine = app.findLineAt(x, y) !== -1;
    app.canvas.style.cursor = overLine ? 'pointer' : 'crosshair';
  }
};

// `availBox` is called only by the zoom-rect branch: measuring the viewport forces a layout,
// which every other release must not pay for.
export const releasePointer = (app, e, viewport, availBox) => {
  if (app.isDraggingCompareSplit) { endCompareSplit(app); return; }
  if (app.isRectDrawDragging) { endRectDraw(app); return; }
  if (app.isZoomRectDragging) { endZoomRect(app, viewport, availBox); return; }

  if (app.isDraggingPoint) {
    app.endPointDrag(app.draggingPoint, e.altKey);
    return;
  }

  if (app.isDraggingSegment) {
    app.endSegmentDrag(e.altKey);
    return;
  }

  if (app.isDraggingLine) {
    app.isDraggingLine = false;
    app.draggingLine = null;
    app.saveHistory();
    app.finishDragGesture(e.altKey);
    return;
  }
  if (!app.isPanning) return;
  endPan(app, e);
};
