import { releasePointer } from './release.js';
import { activeGesture } from './gesture.js';
import { nearCompareDivider, canvasCoords } from './canvasCoords.js';
import { startDrawingMode } from '../draw/mode.js';
import { selectedIndices } from '../line/selection.js';
import { beginSegmentDrag, movePointTo, dragMove } from '../touch/dragGestures.js';

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

  // setCompareSplit clamps (a sliver of each side stays grabbable) and repaints on the next frame.
  #moveCompareSplit(clientX, clientY) {
    const app = this.app;
    const { cssX, cssY } = canvasCoords(app, clientX, clientY);
    const scale = app.scale || 1;
    const f = app.compareMode === 'vertical'
      ? cssX / (app.canvas.width * scale)
      : cssY / (app.canvas.height * scale);
    app.settings.setCompareSplit(f, { dragging: true });
  }

  wirePanDrag() {
    const app = this.app;
    const viewport = document.getElementById('canvas-viewport');
    // The frame the zoom-rect release fits into; the window is the fallback before layout.
    const availBox = () => ({
      w: viewport ? viewport.clientWidth : window.innerWidth,
      h: viewport ? viewport.clientHeight : window.innerHeight,
    });

    const startPan = e => {
      // Compare divider: plain left-drag, checked first so it wins over hits underneath,
      // and only when a split mode is the active (not held) view.
      if (e.button === 0 && !e.altKey && !e.shiftKey && !e.ctrlKey && !e.metaKey && app.image &&
          !app.compareHoldOriginal && nearCompareDivider(app, e.clientX, e.clientY)) {
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
        if (!app.isDrawing) startDrawingMode(app);
        if (!app.isDrawing) return;   // declined (no image / read-only) — nothing to sweep
        const { cssX, cssY, x: imgX, y: imgY } = canvasCoords(app, e.clientX, e.clientY);
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
        const { x, y } = canvasCoords(app, e.clientX, e.clientY);
        if (app.beginPullOutDrag(x, y)) {
          e.preventDefault();
          e.stopPropagation();
          app.canvas.style.cursor = 'move';
          return;
        }
      }

      // Alt+Shift+left → drag whole line (takes priority over zoom-rect).
      if (e.button === 0 && e.altKey && e.shiftKey && app.image) {
        const { x, y } = canvasCoords(app, e.clientX, e.clientY);
        const lineIdx = app.findLineAt(x, y);
        if (lineIdx !== -1) {
          e.preventDefault();
          e.stopPropagation();
          const line = app.lines[lineIdx];
          // Part of a multi-selection: snapshot EVERY selected line so the drag moves them all.
          const sel = selectedIndices(app);
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
        const { cssX, cssY, x: imgX, y: imgY } = canvasCoords(app, e.clientX, e.clientY);
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
        const { x, y } = canvasCoords(app, e.clientX, e.clientY);
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
          beginSegmentDrag(app, nearSeg, x, y);
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

    // One move per gesture (gesture.js): the state picks the handler, not a chain of flags.
    const onMove = {
      compareSplit: (e) => this.#moveCompareSplit(e.clientX, e.clientY),
      rectDraw: (e) => {
        const { cssX, cssY, x: imgX, y: imgY } = canvasCoords(app, e.clientX, e.clientY);
        app.rectDrawEnd = { imgX, imgY, cssX, cssY };
        app.zoomPan.updateRectDrawOverlay();
      },
      zoomRect: (e) => {
        const { cssX, cssY, x: imgX, y: imgY } = canvasCoords(app, e.clientX, e.clientY);
        app.zoomRectEnd = { imgX, imgY, cssX, cssY };
        app.zoomPan.updateZoomRectOverlay();
      },
      point: (e) => {
        if (!app.draggingPoint) return;
        const { x, y } = canvasCoords(app, e.clientX, e.clientY);
        movePointTo(app, app.draggingPoint, x, y);
      },
      // Shift is read per-event: pressing or releasing it mid-drag switches live between
      // the grabbed segment and the whole line.
      segment: (e) => { if (app.draggingSegment) dragMove(app, e.clientX, e.clientY, e.shiftKey); },
      line: (e) => { if (app.draggingLine) dragMove(app, e.clientX, e.clientY, e.shiftKey); },
      pan: (e) => {
        // Shift = 2.5× faster, read per event so the speed changes mid-drag.
        const speed = e.shiftKey ? 2.5 : 1;
        viewport.scrollLeft -= (e.clientX - this.#panLastX) * speed;
        viewport.scrollTop  -= (e.clientY - this.#panLastY) * speed;
        this.#panLastX = e.clientX;
        this.#panLastY = e.clientY;
      },
    };
    document.addEventListener('mousemove', e => onMove[activeGesture(app)]?.(e));
    document.addEventListener('mouseup', e => releasePointer(app, e, viewport, availBox));

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
