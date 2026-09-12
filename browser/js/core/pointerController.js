// Mouse interaction wiring: Alt/middle pan, Shift+drag zoom-rect, rect-draw sweep, and
// point/segment/whole-line drag. Every drag helper + drag-state field lives on the app,
// shared with the touch path in inputController.js.
export class PointerController {
  // Last pointer position during a pan (delta-based scroll).
  #panLastX = 0;
  #panLastY = 0;

  constructor(app) {
    this.app = app;
  }

  // setCompareSplit clamps (a sliver of each side stays grabbable) and repaints.
  #moveCompareSplit(clientX, clientY) {
    const app = this.app;
    const { cssX, cssY } = app.canvasCoords(clientX, clientY);
    const scale = app.scale || 1;
    const f = app.compareMode === 'vertical'
      ? cssX / (app.canvas.width * scale)
      : cssY / (app.canvas.height * scale);
    app.settings.setCompareSplit(f);
  }

  wirePanDrag() {
    const app = this.app;
    const viewport = document.getElementById('canvas-viewport');

    const startPan = e => {
      // Compare divider: plain left-drag, checked first so it wins over hits underneath,
      // and only when a split mode is the active (not held) view.
      if (e.button === 0 && !e.altKey && !e.shiftKey && !e.ctrlKey && !e.metaKey && app.image &&
          !app.compareHoldOriginal && app.nearCompareDivider(e.clientX, e.clientY)) {
        app.isDraggingCompareSplit = true;
        app.canvas.style.cursor = app.compareMode === 'vertical' ? 'col-resize' : 'row-resize';
        e.preventDefault();
        e.stopPropagation();
        return;
      }

      // Compare view is read-only: only the pan gestures stay.
      if (app.compareReadOnly()) {
        const isMiddle = e.button === 1;
        const isAltLeft = e.button === 0 && e.altKey;
        if (isMiddle || isAltLeft) {
          e.preventDefault();
          app.isPanning = true;
          this.#panLastX = e.clientX;
          this.#panLastY = e.clientY;
          app.canvas.style.cursor = 'grabbing';
        }
        return;
      }

      // Rect-draw mode: plain left-drag sweeps a rectangle. Picking the tool is the intent,
      // so the press turns drawing on itself (there is no hold-to-draw flow to fall back on).
      if (app.drawMode === 'rect' && e.button === 0 &&
        !e.altKey && !e.shiftKey && !e.ctrlKey && !e.metaKey && app.image) {
        if (!app.isDrawing) app.startDrawingMode();
        if (!app.isDrawing) return;   // declined (no image / read-only) — nothing to sweep
        const { cssX, cssY, x: imgX, y: imgY } = app.canvasCoords(e.clientX, e.clientY);
        app.isRectDrawDragging = true;
        app.rectDrawStart = { imgX, imgY, cssX, cssY };
        app.rectDrawEnd = { ...app.rectDrawStart };
        e.preventDefault();
        e.stopPropagation();
        return;
      }

      // Alt+Ctrl/⌘+left → pull a NEW point out of the line under the cursor (on a closed area
      // it breaks the shape open). Before the plain Alt gestures, which would move the existing point.
      if (e.button === 0 && e.altKey && (e.ctrlKey || e.metaKey) && !e.shiftKey && app.image) {
        const { x, y } = app.canvasCoords(e.clientX, e.clientY);
        if (app.beginPullOutDrag(x, y)) {
          e.preventDefault();
          e.stopPropagation();
          app.canvas.style.cursor = 'move';
          return;
        }
      }

      // Alt+Shift+left → drag whole line (takes priority over zoom-rect).
      if (e.button === 0 && e.altKey && e.shiftKey && app.image) {
        const { x, y } = app.canvasCoords(e.clientX, e.clientY);
        const lineIdx = app.findLineAt(x, y);
        if (lineIdx !== -1) {
          e.preventDefault();
          e.stopPropagation();
          const line = app.lines[lineIdx];
          // Part of a multi-selection: snapshot EVERY selected line so the drag moves them all.
          const sel = app.selectedIndices();
          const multiOrig = (sel.length >= 2 && sel.includes(lineIdx))
            ? sel.map((li) => ({ li, pts: app.lines[li].points.map(p => ({ x: p.x, y: p.y })) }))
            : null;
          app.isDraggingLine = true;
          app.draggingLine = {
            lineIdx,
            startX: x,
            startY: y,
            origPoints: line.points.map(p => ({ x: p.x, y: p.y })),
            multiOrig,
          };
          app.canvas.style.cursor = 'move';
          return;
        }
      }

      // Shift+left (no Alt, no Ctrl/⌘) → zoom rect. Ctrl/⌘+Shift+click is multi-line select (canvasClick).
      if (e.button === 0 && e.shiftKey && !e.altKey && !e.ctrlKey && !e.metaKey && app.image) {
        const { cssX, cssY, x: imgX, y: imgY } = app.canvasCoords(e.clientX, e.clientY);
        app.isZoomRectDragging = true;
        app.zoomRectStart = { imgX, imgY, cssX, cssY };
        app.zoomRectEnd = { imgX, imgY, cssX, cssY };
        e.preventDefault();
        e.stopPropagation();
        return;
      }

      const isMiddle = e.button === 1;
      // Alt+left on an empty area (Alt+Shift on a line already returned) → pan.
      const isAltLeft = e.button === 0 && e.altKey;
      if (!isMiddle && !isAltLeft) return;

      if (isAltLeft) {
        const { x, y } = app.canvasCoords(e.clientX, e.clientY);
        const nearPt = app.findNearestPointWithIdx(x, y);
        if (nearPt) {
          e.preventDefault();
          app.isDraggingPoint = true;
          app.draggingPoint = nearPt;
          app.canvas.style.cursor = 'move';
          return;
        }
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

    // Both canvas and viewport, so middle-click anywhere inside works.
    app.canvas.addEventListener('mousedown', startPan);
    viewport.addEventListener('mousedown', startPan);

    // The browser's middle-click auto-scroll mode.
    viewport.addEventListener('mousedown', e => { if (e.button === 1) e.preventDefault(); });

    document.addEventListener('mousemove', e => {
      if (app.isDraggingCompareSplit) {
        this.#moveCompareSplit(e.clientX, e.clientY);
        return;
      }

      if (app.isRectDrawDragging) {
        const { cssX, cssY, x: imgX, y: imgY } = app.canvasCoords(e.clientX, e.clientY);
        app.rectDrawEnd = { imgX, imgY, cssX, cssY };
        app.zoomPan.updateRectDrawOverlay();
        return;
      }

      if (app.isZoomRectDragging) {
        const { cssX, cssY, x: imgX, y: imgY } = app.canvasCoords(e.clientX, e.clientY);
        app.zoomRectEnd = { imgX, imgY, cssX, cssY };
        app.zoomPan.updateZoomRectOverlay();
        return;
      }

      if (app.isDraggingPoint && app.draggingPoint) {
        const { x, y } = app.canvasCoords(e.clientX, e.clientY);
        app.movePointTo(app.draggingPoint, x, y);
        return;
      }

      // Shift is read per-event: pressing or releasing it mid-drag switches live between
      // the grabbed segment and the whole line.
      if ((app.isDraggingSegment && app.draggingSegment) ||
          (app.isDraggingLine && app.draggingLine)) {
        app.dragMove(e.clientX, e.clientY, e.shiftKey);
        return;
      }
      if (!app.isPanning) return;
      // Shift = 2.5× faster, read per event so the speed changes mid-drag.
      const speed = e.shiftKey ? 2.5 : 1;
      viewport.scrollLeft -= (e.clientX - this.#panLastX) * speed;
      viewport.scrollTop  -= (e.clientY - this.#panLastY) * speed;
      this.#panLastX = e.clientX;
      this.#panLastY = e.clientY;
    });
    document.addEventListener('mouseup', e => {
      // Suppress the trailing click so it is not a new point.
      if (app.isDraggingCompareSplit) {
        app.isDraggingCompareSplit = false;
        app.canvas.style.cursor = app.isDrawing ? 'crosshair' : 'default';
        app.dragJustEnded = true;
        setTimeout(() => { app.dragJustEnded = false; }, 50);
        return;
      }

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
            app.createRect(s.imgX, s.imgY, en.imgX, en.imgY, false);
          }
        }
        // Stay in rect-drawing mode; suppress the trailing click.
        app.dragJustEnded = true;
        setTimeout(() => { app.dragJustEnded = false; }, 50);
        return;
      }

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

            // No transition, so scrollLeft/scrollTop see the final canvas size at once.
            app.canvas.classList.add('zoom-no-transition');
            app.zoomPan.setZoom(newScale, false);
            // Force a synchronous layout before assigning scrollLeft/scrollTop.
            void app.canvas.getBoundingClientRect();
            if (vp) {
              vp.scrollLeft = Math.max(0, x1 * newScale - (availW - rectW * newScale) / 2);
              vp.scrollTop = Math.max(0, y1 * newScale - (availH - rectH * newScale) / 2);
            }
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
      app.isPanning = false;
      if (app.isDrawing) {
        app.canvas.style.cursor = 'crosshair';
      } else {
        const { x, y } = app.canvasCoords(e.clientX, e.clientY);
        const overLine = app.findLineAt(x, y) !== -1;
        app.canvas.style.cursor = overLine ? 'pointer' : 'crosshair';
      }
    });

    // Middle double-click OR Alt+double-left-click → fit to window.
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
