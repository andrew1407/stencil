// What every mouse gesture does on the release: the compare divider, the rect-draw and
// zoom-rect bands, a point/segment/whole-line drag, and the end of a pan.
import { activeGesture } from './gesture.js';
import { canvasCoords } from './canvasCoords.js';
import { createRect } from '../line/shapeBuilder.js';
import { endPointDrag, endSegmentDrag, finishDragGesture } from '../touch/dragGestures.js';
import { rectZoom } from '../zoom/pan.js';

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
      createRect(app, s.imgX, s.imgY, en.imgX, en.imgY, false);
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
  const fit = rectZoom(x1, y1, rectW, rectH, availW, availH);

  // No transition, so scrollLeft/scrollTop see the final canvas size at once.
  app.canvas.classList.add('zoom-no-transition');
  app.zoomPan.setZoom(fit.scale, false);
  // Force a synchronous layout before assigning scrollLeft/scrollTop.
  void app.canvas.getBoundingClientRect();
  if (viewport) {
    viewport.scrollLeft = fit.scrollLeft;
    viewport.scrollTop = fit.scrollTop;
  }
  requestAnimationFrame(() => {
    app.canvas.classList.remove('zoom-no-transition');
    if (app.image) app.storage.saveSoon();
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
    const { x, y } = canvasCoords(app, e.clientX, e.clientY);
    const overLine = app.findLineAt(x, y) !== -1;
    app.canvas.style.cursor = overLine ? 'pointer' : 'crosshair';
  }
};

const endLineDrag = (app, e) => {
  app.isDraggingLine = false;
  app.draggingLine = null;
  app.saveHistory();
  finishDragGesture(app, e.altKey);
};

// One release per gesture (gesture.js). `availBox` is called only by the zoom-rect end:
// measuring the viewport forces a layout, which every other release must not pay for.
const ON_RELEASE = Object.freeze({
  compareSplit: (app) => endCompareSplit(app),
  rectDraw: (app) => endRectDraw(app),
  zoomRect: (app, e, viewport, availBox) => endZoomRect(app, viewport, availBox),
  point: (app, e) => endPointDrag(app, app.draggingPoint, e.altKey),
  segment: (app, e) => endSegmentDrag(app, e.altKey),
  line: endLineDrag,
  pan: (app, e) => endPan(app, e),
});

export const releasePointer = (app, e, viewport, availBox) =>
  ON_RELEASE[activeGesture(app)]?.(app, e, viewport, availBox);
