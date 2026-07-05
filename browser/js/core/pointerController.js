// ── PointerController: mouse pan / rect-draw / zoom-rect / drag ──────
// Extracted from drawingApp.js (#wirePanDrag). Owns the desktop mouse interaction wiring:
// Alt/middle pan, Shift+drag zoom-rect, rect-draw sweep, and point/segment/whole-line drag.
// Holds only the pan cursor delta; every drag helper + drag-state field lives on the
// back-referenced app (findNearestPointWithIdx / beginSegmentDrag / movePointTo / dragMove /
// end*Drag / finishDragGesture; draggingPoint/Segment/Line etc.), shared with the touch path
// in inputController.js.
export class PointerController {
  // Last pointer position during an Alt/middle pan (delta-based scroll).
  #panLastX = 0;
  #panLastY = 0;

  constructor(app) {
    this.app = app;
  }

  wirePanDrag() {
    const app = this.app;
    const viewport = document.getElementById('canvas-viewport');

    // Pan: Alt+left-drag OR middle-mouse-button drag (works in both drawing/non-drawing modes)
    const startPan = e => {
      // Rect-draw mode: plain left-drag sweeps out a rectangle area
      if (app.isDrawing && app.drawMode === 'rect' && e.button === 0 &&
        !e.altKey && !e.shiftKey && !e.ctrlKey && !e.metaKey && app.image) {
        const { cssX, cssY, x: imgX, y: imgY } = app.canvasCoords(e.clientX, e.clientY);
        app.isRectDrawDragging = true;
        app.rectDrawStart = { imgX, imgY, cssX, cssY };
        app.rectDrawEnd = { ...app.rectDrawStart };
        e.preventDefault();
        e.stopPropagation();
        return;
      }

      // Alt+Shift+left → drag whole line (takes priority over zoom-rect)
      if (e.button === 0 && e.altKey && e.shiftKey && app.image) {
        const { x, y } = app.canvasCoords(e.clientX, e.clientY);
        const lineIdx = app.findLineAt(x, y);
        if (lineIdx !== -1) {
          e.preventDefault();
          e.stopPropagation();
          const line = app.lines[lineIdx];
          // Record the grabbed segment too, so releasing Shift mid-drag can
          // drop down to moving just that segment (live modifier switching).
          const seg = app.findNearestSegmentWithIdx(x, y);
          app.isDraggingLine = true;
          app.draggingLine = {
            lineIdx,
            ptIdx1: seg && seg.lineIdx === lineIdx ? seg.ptIdx1 : null,
            ptIdx2: seg && seg.lineIdx === lineIdx ? seg.ptIdx2 : null,
            startX: x,
            startY: y,
            origPoints: line.points.map(p => ({ x: p.x, y: p.y }))
          };
          app.canvas.style.cursor = 'move';
          return;
        }
      }

      // Shift+left (no Alt) → start zoom rect selection
      if (e.button === 0 && e.shiftKey && !e.altKey && app.image) {
        const { cssX, cssY, x: imgX, y: imgY } = app.canvasCoords(e.clientX, e.clientY);
        app.isZoomRectDragging = true;
        app.zoomRectStart = { imgX, imgY, cssX, cssY };
        app.zoomRectEnd = { imgX, imgY, cssX, cssY };
        e.preventDefault();
        e.stopPropagation();
        return;
      }

      const isMiddle = e.button === 1;
      // Alt+left always pans (with or without Shift). If Alt+Shift was on a line,
      // the line-drag block above already returned; here means empty area → fast pan.
      const isAltLeft = e.button === 0 && e.altKey;
      if (!isMiddle && !isAltLeft) return;

      // Alt+left: check if clicking on a point → drag the point instead of panning
      if (isAltLeft) {
        const { x, y } = app.canvasCoords(e.clientX, e.clientY);
        // Priority 1: near a point → drag the point
        const nearPt = app.findNearestPointWithIdx(x, y);
        if (nearPt) {
          e.preventDefault();
          app.isDraggingPoint = true;
          app.draggingPoint = nearPt;
          app.canvas.style.cursor = 'move';
          return;
        }
        // Priority 2: near a segment → drag that segment
        const nearSeg = app.findNearestSegmentWithIdx(x, y);
        if (nearSeg) {
          e.preventDefault();
          app.beginSegmentDrag(nearSeg, x, y);
          app.canvas.style.cursor = 'move';
          return;
        }
      }

      e.preventDefault();
      app.isPanning = true;
      this.#panLastX = e.clientX;
      this.#panLastY = e.clientY;
      app.canvas.style.cursor = 'grabbing';
    };

    // Listen on both canvas and viewport so middle-click anywhere inside works
    app.canvas.addEventListener('mousedown', startPan);
    viewport.addEventListener('mousedown', startPan);

    // Prevent the browser's default middle-click auto-scroll mode
    viewport.addEventListener('mousedown', e => { if (e.button === 1) e.preventDefault(); });

    document.addEventListener('mousemove', e => {
      // Handle rect-draw drag (rect drawing mode, plain left-drag)
      if (app.isRectDrawDragging) {
        const { cssX, cssY, x: imgX, y: imgY } = app.canvasCoords(e.clientX, e.clientY);
        app.rectDrawEnd = { imgX, imgY, cssX, cssY };
        app.zoomPan.updateRectDrawOverlay();
        return;
      }

      // Handle zoom rect drag (Shift+left-drag)
      if (app.isZoomRectDragging) {
        const { cssX, cssY, x: imgX, y: imgY } = app.canvasCoords(e.clientX, e.clientY);
        app.zoomRectEnd = { imgX, imgY, cssX, cssY };
        app.zoomPan.updateZoomRectOverlay();
        return;
      }

      // Handle point drag
      if (app.isDraggingPoint && app.draggingPoint) {
        const { x, y } = app.canvasCoords(e.clientX, e.clientY);
        app.movePointTo(app.draggingPoint, x, y);
        return;
      }

      // Segment / whole-line drag. Shift is read per-event so pressing or
      // releasing it mid-drag switches live between moving just the grabbed
      // segment and translating the whole line shape — no need to restart.
      if ((app.isDraggingSegment && app.draggingSegment) ||
          (app.isDraggingLine && app.draggingLine)) {
        app.dragMove(e.clientX, e.clientY, e.shiftKey);
        return;
      }
      if (!app.isPanning) return;
      // Delta-based pan with Shift = faster (2.5×). Reading shiftKey per
      // event means user can speed up / slow down mid-drag without jumps.
      const speed = e.shiftKey ? 2.5 : 1;
      viewport.scrollLeft -= (e.clientX - this.#panLastX) * speed;
      viewport.scrollTop  -= (e.clientY - this.#panLastY) * speed;
      this.#panLastX = e.clientX;
      this.#panLastY = e.clientY;
    });
    document.addEventListener('mouseup', e => {
      // Finish rect-draw (rect drawing mode)
      if (app.isRectDrawDragging) {
        app.isRectDrawDragging = false;
        app.zoomPan.hideZoomRectOverlay();
        const s = app.rectDrawStart;
        const en = app.rectDrawEnd;
        app.rectDrawStart = null; app.rectDrawEnd = null;
        if (s && en) {
          const w = Math.abs(en.imgX - s.imgX);
          const h = Math.abs(en.imgY - s.imgY);
          if (w > 3 && h > 3) {
            // createRect auto-connects when continuation mode is active
            app.createRect(s.imgX, s.imgY, en.imgX, en.imgY, false);
          }
        }
        // Stay in rect-drawing mode so multiple rects can be drawn;
        // suppress the trailing click so it isn't treated as a point.
        app.dragJustEnded = true;
        setTimeout(() => { app.dragJustEnded = false; }, 50);
        return;
      }

      // Finish zoom rect (Shift+left-drag)
      if (app.isZoomRectDragging) {
        app.isZoomRectDragging = false;
        app.zoomPan.hideZoomRectOverlay();
        const s = app.zoomRectStart;
        const en = app.zoomRectEnd;
        if (s && en) {
          const x1 = Math.min(s.imgX, en.imgX);
          const y1 = Math.min(s.imgY, en.imgY);
          const x2 = Math.max(s.imgX, en.imgX);
          const y2 = Math.max(s.imgY, en.imgY);
          const rectW = x2 - x1;
          const rectH = y2 - y1;
          if (rectW > 4 && rectH > 4) {
            const vp = document.getElementById('canvas-viewport');
            const availW = vp ? vp.clientWidth  : window.innerWidth;
            const availH = vp ? vp.clientHeight : window.innerHeight;
            const newScale = Math.min(availW / rectW, availH / rectH, 5);

            // Disable CSS transition so width/height are applied instantly,
            // allowing scrollLeft/scrollTop to reflect the final canvas size.
            app.canvas.classList.add('zoom-no-transition');
            app.zoomPan.setZoom(newScale, false);
            // Force a synchronous layout — this makes scrollWidth reflect
            // the new canvas size before we assign scrollLeft/scrollTop.
            void app.canvas.getBoundingClientRect();
            if (vp) {
              vp.scrollLeft = Math.max(0, x1 * newScale - (availW - rectW * newScale) / 2);
              vp.scrollTop = Math.max(0, y1 * newScale - (availH - rectH * newScale) / 2);
            }
            // Re-enable transition and persist after layout settles
            requestAnimationFrame(() => {
              app.canvas.classList.remove('zoom-no-transition');
              if (app.image) app.storage.save();
            });
          }
        }
        app.zoomRectStart = null;
        app.zoomRectEnd = null;
        app.canvas.style.cursor = 'crosshair';
        return;
      }

      // Finish point drag
      if (app.isDraggingPoint) {
        app.endPointDrag(app.draggingPoint, e.altKey);
        return;
      }

      // Finish segment drag
      if (app.isDraggingSegment) {
        app.endSegmentDrag(e.altKey);
        return;
      }

      // Finish whole-line drag
      if (app.isDraggingLine) {
        app.isDraggingLine = false;
        app.draggingLine = null;
        app.saveHistory();
        app.finishDragGesture(e.altKey);
        return;
      }
      if (!app.isPanning) return;
      app.isPanning = false;
      // Restore cursor
      if (app.isDrawing) {
        app.canvas.style.cursor = 'crosshair';
      } else {
        const { x, y } = app.canvasCoords(e.clientX, e.clientY);
        const overLine = app.findLineAt(x, y) !== -1;
        app.canvas.style.cursor = overLine ? 'pointer' : 'crosshair';
      }
    });

    // Double-click middle mouse button OR Alt+double-left-click → reset zoom (fit to window)
    const resetZoom = e => {
      const isMiddleDouble = e.button === 1;
      const isAltLeftDouble = e.button === 0 && e.altKey;
      if (!isMiddleDouble && !isAltLeftDouble) return;
      e.preventDefault();
      app.zoomPan.fitToWindow();
    };
    app.canvas.addEventListener('dblclick', resetZoom);
    viewport.addEventListener('dblclick', resetZoom);
  }
}
