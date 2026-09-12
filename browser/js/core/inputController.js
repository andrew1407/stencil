import { HoldDrawController, holdDrawTarget } from './holdDraw.js';
import { classifyEnd, midpoint, touchDist, TOUCH_DEFAULTS } from './touchGestures.js';
import { canvasOrigin } from './zoomPan.js';

// The two alternative input flows: hold-to-draw and touchscreen. The mouse drag path stays
// in DrawingApp; both reuse the same drag helpers and drag-state fields.
// A hold needs an image, drawing mode off, no other gesture, and not the rect tool (a hold
// there would seed a freehand line). Desktop twin: mousePressEvent's eligibleHold.
export const holdDrawEligible = (app) =>
  !!app?.image && !app.isDrawing && app.drawMode !== 'rect' &&
  !(app.isPanning || app.isDraggingPoint || app.isDraggingSegment ||
    app.isDraggingLine || app.isZoomRectDragging || app.isRectDrawDragging);

export class InputController {
  #holdDraw = null;
// A dwell CLOSED the shape while the button is still down; the release must still swallow its click.
  #holdClosedShape = false;
  #holdTickTimer = null;
  #holdAutoEnabled = false;
// A hold stroke extending a line BACKWARD from its first point (points prepended).
  #holdPrepend = false;
  #touch = null;
  #longPressTimer = null;

  constructor(app) {
    this.app = app;
  }

// Keeps the tooltip off a mid-hold ghost line (canvasMouseMove's drag guard).
  get holdEngaged() { return !!this.#holdDraw && this.#holdDraw.engaged; }

// HoldDrawController (./holdDraw.js) decides timing/transitions; this wiring owns the DOM
// timers, coordinate conversion and rendering.
  wireHoldDraw() {
    const app = this.app;
    const ctrl = this.#holdDraw = new HoldDrawController({ holdDelay: app.holdDrawDelay });

    const onDown = e => {
      if (e.button !== 0 || e.altKey || e.shiftKey || e.ctrlKey || e.metaKey) return;
      if (app.compareReadOnly()) return;
      this.#holdTryDown(e.clientX, e.clientY);
    };
    app.canvas.addEventListener('mousedown', onDown);

    document.addEventListener('mousemove', e => {
      if (!ctrl.engaged) return;
      const r = ctrl.pointerMove(e.clientX, e.clientY, this.#now());
      if (!r) return;
      if (r.type === 'abort') { this.#stopHoldTicks(); ctrl.cancel(); return; }
      if (r.type === 'preview') this.#holdSetPreview(e.clientX, e.clientY);
    });

    document.addEventListener('mouseup', () => {
      if (this.#holdClosedShape) { this.#holdReleaseAfterClose(); return; }
      if (ctrl.state === 'idle') return;
      const r = ctrl.pointerUp(this.#now());
      this.#stopHoldTicks();
      if (r && r.type === 'commit') this.#holdCommit();
      else this.#holdClearPreview();
    });

    window.addEventListener('blur', () => { this.#stopHoldTicks(); ctrl.cancel(); this.#holdClearPreview(); });
  }

  #now() { return (typeof performance !== 'undefined' ? performance.now() : Date.now()); }
  #stopHoldTicks() { if (this.#holdTickTimer) { clearInterval(this.#holdTickTimer); this.#holdTickTimer = null; } }
  #startHoldTicks() { if (!this.#holdTickTimer) this.#holdTickTimer = setInterval(() => this.#holdTick(this.#now()), 40); }

// Shared by mouse and touch; the caller has filtered out modified presses. True if armed.
  #holdTryDown(clientX, clientY) {
    const app = this.app;
    if (!holdDrawEligible(app)) return false;
    this.#holdDraw.setHoldDelay(app.holdDrawDelay);
    this.#holdDraw.pointerDown(clientX, clientY, this.#now());
    this.#startHoldTicks();
    return true;
  }

// preventDefault() suppresses the synthetic mouse/click. 1 finger on geometry drags it (no
// Alt), held still opens the context menu; on empty a tap places a point and a press-and-hold
// draws; 2 fingers pan + pinch-zoom about their midpoint.
  wireTouch() {
    const app = this.app;
    const viewport = document.getElementById('canvas-viewport');
    const moveTol = TOUCH_DEFAULTS.moveTol;

    const findTouch = (touches, id) => {
      for (let i = 0; i < touches.length; i++) if (touches[i].identifier === id) return touches[i];
      return null;
    };
// Pinch DOM writes are coalesced into one rAF; onMove just stashes the latest scale+midpoint.
    const applyPinch = () => {
      const st = this.#touch;
      if (!st || st.mode !== 'pinch' || !st.pending) { if (st) st.raf = null; return; }
      const { scale, midX, midY } = st.pending;
      st.pending = null;
      st.raf = null;
      app.scale = scale;
      app.canvas.style.width = (app.canvas.width * scale) + 'px';
      app.canvas.style.height = (app.canvas.height * scale) + 'px';
      app.zoomPan.setZoomInputValue(Math.round(scale * 100));
// Keep the pinched image point under the moving midpoint; vpLeft/vpTop are cached at pinch start.
      viewport.scrollLeft = st.imgX * scale - (midX - st.vpLeft);
      viewport.scrollTop = st.imgY * scale - (midY - st.vpTop);
    };

    const clearLongPress = () => {
      if (this.#longPressTimer) { clearTimeout(this.#longPressTimer); this.#longPressTimer = null; }
    };
    const dropSingle = () => {
      clearLongPress();
      this.#stopHoldTicks();
      this.#holdClosedShape = false;
      if (this.#holdDraw.engaged) { this.#holdDraw.cancel(); this.#holdClearPreview(); }
      app.isDraggingPoint = false; app.draggingPoint = null;
      app.isDraggingSegment = false; app.draggingSegment = null;
    };
// A stationary tap is a modifier-free left click (canvasClick handles both cases).
    const tapClick = (e, st) => {
      const ct = (e.changedTouches && e.changedTouches[0]) || st;
      app.canvasClick({
        clientX: ct.clientX ?? st.startX, clientY: ct.clientY ?? st.startY,
        altKey: false, shiftKey: false, ctrlKey: false, metaKey: false,
      });
    };

// Empty-space holds belong to hold-to-draw.
    const armGeometryLongPress = (t) => {
      this.#longPressTimer = setTimeout(() => {
        this.#longPressTimer = null;
        if (!this.#touch || this.#touch.id !== t.identifier) return;
        dropSingle();
        this.#touch = { mode: 'done', id: t.identifier };
        app.canvas.dispatchEvent(new MouseEvent('contextmenu',
          { clientX: t.clientX, clientY: t.clientY, bubbles: true, cancelable: true }));
      }, TOUCH_DEFAULTS.longPressMs);
    };

    const onStart = e => {
      if (!app.image) return;

// Two fingers abandon any in-flight single-finger gesture.
      if (e.touches.length >= 2) {
        e.preventDefault();
        dropSingle();
        const [a, b] = [e.touches[0], e.touches[1]];
        const mid = midpoint(a, b);
        const vpRect = viewport.getBoundingClientRect();
// Minus the centring margins: a picture smaller than the frame does not start at the scroll origin.
        const org = canvasOrigin();
        const contentX = mid.x - vpRect.left + viewport.scrollLeft - org.x;
        const contentY = mid.y - vpRect.top + viewport.scrollTop - org.y;
        app.canvas.classList.add('zoom-no-transition');
        this.#touch = {
          mode: 'pinch',
          startDist: touchDist(a, b) || 1,
          startScale: app.scale,
          imgX: contentX / app.scale,
          imgY: contentY / app.scale,
          vpLeft: vpRect.left, vpTop: vpRect.top,
          pending: null, raf: null,
        };
        return;
      }
      if (e.touches.length !== 1) return;
      e.preventDefault();
      const t = e.touches[0];
      const { x, y } = app.canvasCoords(t.clientX, t.clientY);

// A finger landing on a point/segment grabs it.
      const nearPt = app.findNearestPointWithIdx(x, y);
      if (nearPt) {
        app.isDraggingPoint = true;
        app.draggingPoint = nearPt;
        this.#touch = { mode: 'point', id: t.identifier, startX: t.clientX, startY: t.clientY };
        armGeometryLongPress(t);
        return;
      }
      const nearSeg = app.findNearestSegmentWithIdx(x, y);
      if (nearSeg) {
        app.beginSegmentDrag(nearSeg, x, y);
        this.#touch = { mode: 'segment', id: t.identifier, startX: t.clientX, startY: t.clientY };
        armGeometryLongPress(t);
        return;
      }

      this.#touch = { mode: 'tap', id: t.identifier, startX: t.clientX, startY: t.clientY, startT: this.#now() };
      this.#holdTryDown(t.clientX, t.clientY);
    };

    const onMove = e => {
      const st = this.#touch;
      if (!st) return;

      if (st.mode === 'pinch') {
        if (e.touches.length < 2) return;
        e.preventDefault();
        const [a, b] = [e.touches[0], e.touches[1]];
        const mid = midpoint(a, b);
        const factor = touchDist(a, b) / st.startDist;
        const newScale = this.app.zoomPan.clampScale(st.startScale * factor);
        st.pending = { scale: newScale, midX: mid.x, midY: mid.y };
        if (!st.raf) st.raf = requestAnimationFrame(applyPinch);
        return;
      }

      const t = findTouch(e.touches, st.id);
      if (!t) return;
      e.preventDefault();
      const moved = Math.hypot(t.clientX - st.startX, t.clientY - st.startY);

      if (st.mode === 'point') {
        if (moved <= moveTol) return;
        clearLongPress();
        st.dragged = true;
        const { x, y } = app.canvasCoords(t.clientX, t.clientY);
        app.movePointTo(app.draggingPoint, x, y);
        return;
      }
      if (st.mode === 'segment') {
        if (moved <= moveTol) return;
        clearLongPress();
        st.dragged = true;
        app.dragMove(t.clientX, t.clientY, false);
        return;
      }
      if (st.mode === 'tap') {
        st.moved = Math.max(st.moved || 0, moved);
        if (!this.#holdDraw.engaged) return;
        const r = this.#holdDraw.pointerMove(t.clientX, t.clientY, this.#now());
        if (!r) return;
        if (r.type === 'abort') { this.#stopHoldTicks(); this.#holdDraw.cancel(); }
        else if (r.type === 'preview') this.#holdSetPreview(t.clientX, t.clientY);
      }
    };

    const onEnd = e => {
      const st = this.#touch;
      if (!st) return;

      if (st.mode === 'pinch') {
// Ignore the lone remaining finger until all are up, so lifting one doesn't start a stray drag.
        if (st.raf) { cancelAnimationFrame(st.raf); st.raf = null; }
        if (st.pending) applyPinch();
        app.canvas.classList.remove('zoom-no-transition');
        app.zoomPan.setZoom(app.scale, true);
        this.#touch = e.touches.length === 0 ? null : { mode: 'done', id: -1 };
        return;
      }

      if (e.touches.length > 0) return;
      clearLongPress();

      if (st.mode === 'point') {
        if (st.dragged) {
          app.endPointDrag(app.draggingPoint, false);
        } else {
          app.isDraggingPoint = false;
          app.draggingPoint = null;
          tapClick(e, st);
        }
      } else if (st.mode === 'segment') {
        if (st.dragged) {
          app.endSegmentDrag(false);
        } else {
          app.isDraggingSegment = false;
          app.draggingSegment = null;
          tapClick(e, st);
        }
      } else if (st.mode === 'tap') {
        if (this.#holdClosedShape) { this.#holdReleaseAfterClose(); this.#touch = null; return; }
        const r = this.#holdDraw.pointerUp(this.#now());
        this.#stopHoldTicks();
        if (r && r.type === 'commit') {
          this.#holdCommit();
        } else {
          this.#holdClearPreview();
          const kind = classifyEnd({ moved: st.moved || 0, elapsed: this.#now() - st.startT });
          if (kind === 'tap') tapClick(e, st);
        }
      }
      this.#touch = null;
    };

    const onCancel = () => {
      if (this.#touch && this.#touch.raf) cancelAnimationFrame(this.#touch.raf);
      dropSingle();
      this.#touch = null;
    };

    app.canvas.addEventListener('touchstart', onStart, { passive: false });
    document.addEventListener('touchmove', onMove, { passive: false });
    document.addEventListener('touchend', onEnd);
    document.addEventListener('touchcancel', onCancel);
  }

  #holdTick(t) {
    const r = this.#holdDraw.tick(t);
    if (!r) return;
    if (r.type === 'start') this.#holdStart(r.x, r.y);
    else if (r.type === 'drop') this.#holdDrop(r.x, r.y);
  }

// The target under the press decides: a point continues that line, a body inserts a point then
// continues, empty starts fresh.
  #holdStart(clientX, clientY) {
    const app = this.app;
    const { x, y } = app.canvasCoords(clientX, clientY);
    this.#holdAutoEnabled = true;
    const target = holdDrawTarget(app.lines, x, y);
    this.#holdPrepend = false;
    if (target.kind === 'point') {
      app.selectedLineIdx = target.lineIdx;
      app.coordLineIdx = target.lineIdx;
      app.focusedPtIdx = target.ptIdx;
      app.startDrawingMode({ connect: true });
// Holding the FIRST point extends the line backward.
      if (target.ptIdx === 0) { this.#holdPrepend = true; app.continueInsertIdx = 0; }
    } else if (target.kind === 'segment') {
      app.insertPointOnSegment(target.lineIdx, target.ptIdx2, x, y);
      app.startDrawingMode({ connect: true });
    } else {
      app.startDrawingMode({ connect: false });
      if (app.currentLine) {
        app.currentLine.points.push({ x, y });
        app.strokeFx.flyIn(app.currentLine, app.currentLine.points.length - 1);
      }
    }
    this.#holdSetPreviewImg(x, y);
    app.updateButtons();
  }

  #holdDrop(clientX, clientY) {
    const app = this.app;
    const { x, y } = app.canvasCoords(clientX, clientY);
// Resting on the first point closes the shape, as clicking does; end the gesture.
    if (app.tryCloseShapeAt(x, y)) {
      this.#stopHoldTicks();
      this.#holdDraw.cancel();
      this.#holdClearPreview();
      this.#holdClosedShape = true;
      app.updateButtons();
      return;
    }
    if (app.continueLineIdx >= 0 && app.lines[app.continueLineIdx]) {
      const line = app.lines[app.continueLineIdx];
      line.points.splice(app.continueInsertIdx, 0, { x, y });
      app.strokeFx.flyIn(line, app.continueInsertIdx);
      app.focusedPtIdx = app.continueInsertIdx;
// Prepend mode keeps inserting at index 0; forward mode advances the insert point.
      if (!this.#holdPrepend) app.continueInsertIdx++;
      app.coordTable.update(line.points, app.continueLineIdx);
    } else if (app.currentLine) {
      app.currentLine.points.push({ x, y });
      app.strokeFx.flyIn(app.currentLine, app.currentLine.points.length - 1);
    }
    this.#holdSetPreviewImg(x, y);
    app.updateButtons();
  }

// Armed on the RELEASE: a guard armed when a dwell closed the shape has expired by the time
// the synthetic click arrives.
  #suppressTrailingClick() {
    const app = this.app;
    app.dragJustEnded = true;
    setTimeout(() => { app.dragJustEnded = false; }, 50);
  }

// Commit the line, leave drawing mode, swallow the trailing click.
  #holdCommit() {
    const app = this.app;
    this.#holdClearPreview();
    if (app.isDrawing) app.stopDrawingMode();
    this.#holdAutoEnabled = false;
    this.#holdPrepend = false;
    this.#suppressTrailingClick();
  }

// Nothing left to commit, but selecting the area opens the bar, which pushes the canvas
// down, so an unswallowed click would land elsewhere and deselect the new shape.
  #holdReleaseAfterClose() {
    this.#holdClosedShape = false;
    this.#holdClearPreview();
    this.#holdAutoEnabled = false;
    this.#holdPrepend = false;
    this.#suppressTrailingClick();
  }

  #holdSetPreview(clientX, clientY) {
    const { x, y } = this.app.canvasCoords(clientX, clientY);
    this.#holdSetPreviewImg(x, y);
  }
  #holdSetPreviewImg(x, y) {
    this.app.holdPreview = { x, y };
    this.app.renderer.redraw();
  }
  #holdClearPreview() {
    if (this.app.holdPreview) { this.app.holdPreview = null; this.app.renderer.redraw(); }
  }

// The last point of the in-progress line, or the current tail of the line being extended.
  holdAnchorPoint() {
    const app = this.app;
    if (app.currentLine && app.currentLine.points.length)
      return app.currentLine.points[app.currentLine.points.length - 1];
    if (app.continueLineIdx >= 0 && app.lines[app.continueLineIdx]) {
      const pts = app.lines[app.continueLineIdx].points;
// Prepend: the current head; forward: the point just before the insertion tail.
      if (this.#holdPrepend) return pts[app.continueInsertIdx] ?? pts[0] ?? null;
      return pts[app.continueInsertIdx - 1] ?? pts[pts.length - 1] ?? null;
    }
    return null;
  }

// Clamped; persisted.
  setHoldDrawDelay(ms, { persist = true } = {}) {
    const app = this.app;
    const n = Number(ms);
    if (!Number.isFinite(n)) return;
    app.holdDrawDelay = Math.max(100, Math.min(3000, Math.round(n)));
    if (this.#holdDraw) this.#holdDraw.setHoldDelay(app.holdDrawDelay);
    if (persist) app.storage.save();
  }
}
