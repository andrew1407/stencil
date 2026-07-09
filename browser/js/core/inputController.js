import { HoldDrawController, holdDrawTarget } from './holdDraw.js';
import { classifyEnd, midpoint, touchDist, TOUCH_DEFAULTS } from './touchGestures.js';

// ── InputController: touchscreen + hold-to-draw alternative input ───
// Extracted from drawingApp.js. Owns the two alternative input flows — hold-to-draw (a
// near-stationary press auto-enters drawing) and touchscreen (direct-manipulation drag +
// two-finger pan/pinch) — plus their gesture state. The mouse pointer/drag path stays in
// DrawingApp's #wirePanDrag; both reuse the same drag helpers, which DrawingApp exposes as
// public methods (findNearestPointWithIdx / beginSegmentDrag / movePointTo / dragMove /
// end*Drag / insertPointOnSegment) and public drag-state fields (draggingPoint / draggingSegment
// / continue*Idx / dragJustEnded). Holds a back-reference to the app like the other collaborators.
export class InputController {
  // Hold-to-draw gesture state.
  #holdDraw = null;
  #holdTickTimer = null;
  #holdAutoEnabled = false;
  // True while a hold stroke extends a line BACKWARD from its first point (points prepended).
  #holdPrepend = false;
  // Live touch gesture state (single/two-finger), null between gestures; long-press timer.
  #touch = null;
  #longPressTimer = null;

  constructor(app) {
    this.app = app;
  }

  // ── Hold-to-draw: an alternative drawing flow ──────────────────
  // A near-stationary plain-left press-and-hold auto-enters drawing and drops the first point;
  // dwelling drops more; releasing commits and exits drawing again. The pure HoldDrawController
  // (./holdDraw.js) decides timing/transitions; this wiring owns the DOM timers, coordinate
  // conversion and rendering. Engaged only when NOT already drawing.
  wireHoldDraw() {
    const app = this.app;
    const ctrl = this.#holdDraw = new HoldDrawController({ holdDelay: app.holdDrawDelay });

    const onDown = e => {
      if (e.button !== 0 || e.altKey || e.shiftKey || e.ctrlKey || e.metaKey) return;
      if (app.compareReadOnly()) return;   // compare view is read-only — no hold-to-draw
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
      if (ctrl.state === 'idle') return;
      const r = ctrl.pointerUp(this.#now());
      this.#stopHoldTicks();
      if (r && r.type === 'commit') this.#holdCommit();
      else this.#holdClearPreview();
    });

    // Drop the gesture if focus leaves the window mid-hold.
    window.addEventListener('blur', () => { this.#stopHoldTicks(); ctrl.cancel(); this.#holdClearPreview(); });
  }

  // Monotonic clock for gesture timing (falls back to Date in old environments).
  #now() { return (typeof performance !== 'undefined' ? performance.now() : Date.now()); }
  #stopHoldTicks() { if (this.#holdTickTimer) { clearInterval(this.#holdTickTimer); this.#holdTickTimer = null; } }
  #startHoldTicks() { if (!this.#holdTickTimer) this.#holdTickTimer = setInterval(() => this.#holdTick(this.#now()), 40); }

  // Arm hold-to-draw at a press point, if eligible. Shared by mouse (wireHoldDraw) and touch
  // (wireTouch); the caller has already filtered out modified presses. Returns true if armed.
  #holdTryDown(clientX, clientY) {
    const app = this.app;
    if (!app.image) return false;
    // Only the auto-mode: manual line/rect drawing keeps its click behavior.
    if (app.isDrawing) return false;
    // Never start over another active gesture (pan / drag / zoom / rect).
    if (app.isPanning || app.isDraggingPoint || app.isDraggingSegment ||
        app.isDraggingLine || app.isZoomRectDragging || app.isRectDrawDragging) return false;
    this.#holdDraw.setHoldDelay(app.holdDrawDelay);
    this.#holdDraw.pointerDown(clientX, clientY, this.#now());
    this.#startHoldTicks();
    return true;
  }

  // ── Touchscreen input (direct manipulation + two-finger) ───────
  // Touch-only layer; preventDefault() suppresses the synthetic mouse/click so it can't collide
  // with the mouse handlers. Gesture map:
  //   1 finger on a point   → drag that point      (no Alt needed)
  //   1 finger on a segment → drag that segment
  //   1 finger held still on geometry → context menu (mirrors right-click there)
  //   1 finger on empty: tap → place a point; press-and-hold → hold-to-draw
  //   2 fingers → pan + pinch-zoom (focal = the midpoint between the fingers)
  wireTouch() {
    const app = this.app;
    const viewport = document.getElementById('canvas-viewport');
    const moveTol = TOUCH_DEFAULTS.moveTol;

    const findTouch = (touches, id) => {
      for (let i = 0; i < touches.length; i++) if (touches[i].identifier === id) return touches[i];
      return null;
    };
    // Pinch DOM writes are coalesced into one rAF (like #wireSmoothZoom) so a flood of
    // touchmoves doesn't thrash layout: onMove just stashes the latest scale+midpoint.
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
      // Keep the pinched-down image point pinned under the (moving) midpoint — pan + zoom
      // together. vpLeft/vpTop are cached at pinch start (the viewport can't move mid-gesture).
      viewport.scrollLeft = st.imgX * scale - (midX - st.vpLeft);
      viewport.scrollTop = st.imgY * scale - (midY - st.vpTop);
    };

    const clearLongPress = () => {
      if (this.#longPressTimer) { clearTimeout(this.#longPressTimer); this.#longPressTimer = null; }
    };
    // Abandon every single-finger gesture (used when a 2nd finger lands or on cancel).
    const dropSingle = () => {
      clearLongPress();
      this.#stopHoldTicks();
      if (this.#holdDraw.engaged) { this.#holdDraw.cancel(); this.#holdClearPreview(); }
      app.isDraggingPoint = false; app.draggingPoint = null;
      app.isDraggingSegment = false; app.draggingSegment = null;
    };
    // A stationary tap behaves exactly like a left mouse click: drops a point in empty space, or
    // selects the line/point under the finger and opens its style panel (canvasClick handles
    // both). Synthesise a modifier-free MouseEvent.
    const tapClick = (e, st) => {
      const ct = (e.changedTouches && e.changedTouches[0]) || st;
      app.canvasClick({
        clientX: ct.clientX ?? st.startX, clientY: ct.clientY ?? st.startY,
        altKey: false, shiftKey: false, ctrlKey: false, metaKey: false,
      });
    };

    // Long-press on grabbed geometry that never moved → open the context menu instead of leaving
    // a no-op drag (empty-space holds belong to hold-to-draw).
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

      // Two fingers → pan + pinch. Abandon any in-flight single-finger gesture.
      if (e.touches.length >= 2) {
        e.preventDefault();
        dropSingle();
        const [a, b] = [e.touches[0], e.touches[1]];
        const mid = midpoint(a, b);
        const vpRect = viewport.getBoundingClientRect();
        const contentX = mid.x - vpRect.left + viewport.scrollLeft;
        const contentY = mid.y - vpRect.top + viewport.scrollTop;
        app.canvas.classList.add('zoom-no-transition');
        this.#touch = {
          mode: 'pinch',
          startDist: touchDist(a, b) || 1,
          startScale: app.scale,
          imgX: contentX / app.scale,
          imgY: contentY / app.scale,
          vpLeft: vpRect.left, vpTop: vpRect.top,   // viewport screen pos, fixed for the gesture
          pending: null, raf: null,                 // latest frame awaiting applyPinch
        };
        return;
      }
      if (e.touches.length !== 1) return;
      e.preventDefault();
      const t = e.touches[0];
      const { x, y } = app.canvasCoords(t.clientX, t.clientY);

      // Direct manipulation: a finger landing on a point/segment grabs it.
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

      // Empty space: tap places a point, press-and-hold draws (hold-to-draw).
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
        const newScale = Math.max(0.05, Math.min(5, st.startScale * factor));
        st.pending = { scale: newScale, midX: mid.x, midY: mid.y };
        if (!st.raf) st.raf = requestAnimationFrame(applyPinch);
        return;
      }

      const t = findTouch(e.touches, st.id);
      if (!t) return;
      e.preventDefault();
      const moved = Math.hypot(t.clientX - st.startX, t.clientY - st.startY);

      if (st.mode === 'point') {
        if (moved <= moveTol) return; // below threshold → still a tap; leave room for long-press
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
        // A finger lifted: settle the zoom; ignore the lone remaining finger until all fingers
        // are up, so lifting one doesn't kick off a stray drag.
        if (st.raf) { cancelAnimationFrame(st.raf); st.raf = null; }
        if (st.pending) applyPinch();   // flush the last frame so we settle at the real pinch end
        app.canvas.classList.remove('zoom-no-transition');
        app.zoomPan.setZoom(app.scale, true);
        this.#touch = e.touches.length === 0 ? null : { mode: 'done', id: -1 };
        return;
      }

      if (e.touches.length > 0) return; // wait until the last finger lifts
      clearLongPress();

      if (st.mode === 'point') {
        if (st.dragged) {
          app.endPointDrag(app.draggingPoint, false);
        } else {
          // Tap (no drag) on a point → select its line + focus the point + open the style panel.
          app.isDraggingPoint = false;
          app.draggingPoint = null;
          tapClick(e, st);
        }
      } else if (st.mode === 'segment') {
        if (st.dragged) {
          app.endSegmentDrag(false);
        } else {
          // Tap (no drag) on a line → select it + open the style panel.
          app.isDraggingSegment = false;
          app.draggingSegment = null;
          tapClick(e, st);
        }
      } else if (st.mode === 'tap') {
        const r = this.#holdDraw.pointerUp(this.#now());
        this.#stopHoldTicks();
        if (r && r.type === 'commit') {
          this.#holdCommit();
        } else {
          this.#holdClearPreview();
          // Not a hold stroke → a plain tap drops a point (like a left click).
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

  // Hold completed → auto-enable drawing and seed the stroke. The target under the press point
  // decides: existing point → continue that line from it; line body → insert a point on it then
  // continue; empty → fresh line.
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
      // Holding the FIRST point extends the line backward: prepend new points before it (index 0).
      if (target.ptIdx === 0) { this.#holdPrepend = true; app.continueInsertIdx = 0; }
    } else if (target.kind === 'segment') {
      // Insert the seed point on the existing line, then extend from it.
      app.insertPointOnSegment(target.lineIdx, target.ptIdx2, x, y);
      app.startDrawingMode({ connect: true });
    } else {
      app.startDrawingMode({ connect: false });
      if (app.currentLine) app.currentLine.points.push({ x, y });
    }
    this.#holdSetPreviewImg(x, y);
    app.updateButtons();
  }

  // Dwell completed → drop a point (extends the in-progress / continued line).
  #holdDrop(clientX, clientY) {
    const app = this.app;
    const { x, y } = app.canvasCoords(clientX, clientY);
    if (app.continueLineIdx >= 0 && app.lines[app.continueLineIdx]) {
      const line = app.lines[app.continueLineIdx];
      line.points.splice(app.continueInsertIdx, 0, { x, y });
      app.focusedPtIdx = app.continueInsertIdx;
      // Prepend mode keeps inserting at index 0 (each new point becomes the new head); forward
      // mode advances the insert point so points keep appending.
      if (!this.#holdPrepend) app.continueInsertIdx++;
      app.coordTable.update(line.points, app.continueLineIdx);
    } else if (app.currentLine) {
      app.currentLine.points.push({ x, y });
    }
    this.#holdSetPreviewImg(x, y);
    app.updateButtons();
  }

  // Release after a hold stroke → commit the line and disable drawing mode, then suppress the
  // trailing synthetic click so it isn't read as a point/select.
  #holdCommit() {
    const app = this.app;
    this.#holdClearPreview();
    if (app.isDrawing) app.stopDrawingMode();
    this.#holdAutoEnabled = false;
    this.#holdPrepend = false;
    app.dragJustEnded = true;
    setTimeout(() => { app.dragJustEnded = false; }, 50);
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

  // The point a hold-draw preview line should emanate from: the last point of the in-progress
  // line, or the current tail of the line being extended. null = none.
  holdAnchorPoint() {
    const app = this.app;
    if (app.currentLine && app.currentLine.points.length)
      return app.currentLine.points[app.currentLine.points.length - 1];
    if (app.continueLineIdx >= 0 && app.lines[app.continueLineIdx]) {
      const pts = app.lines[app.continueLineIdx].points;
      // Prepend: the next point connects to the current head (index 0); forward: it connects to
      // the point just before the insertion tail.
      if (this.#holdPrepend) return pts[app.continueInsertIdx] ?? pts[0] ?? null;
      return pts[app.continueInsertIdx - 1] ?? pts[pts.length - 1] ?? null;
    }
    return null;
  }

  // Set the hold-to-draw dwell/hold delay (ms). Clamped to a sane range; persisted.
  setHoldDrawDelay(ms, { persist = true } = {}) {
    const app = this.app;
    const n = Number(ms);
    if (!Number.isFinite(n)) return;
    app.holdDrawDelay = Math.max(100, Math.min(3000, Math.round(n)));
    if (this.#holdDraw) this.#holdDraw.setHoldDelay(app.holdDrawDelay);
    if (persist) app.storage.save();
  }
}
