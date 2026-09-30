import { HoldDrawController, holdDrawTarget } from '../draw/holdDraw.js';
import { classifyEnd } from '../touch/gestures.js';
import { nowMs, setHoldPreview, clearHoldPreview, holdAnchor, holdDrawEligible } from '../draw/holdDrawView.js';
import { touchHandlers } from '../touch/input.js';
import constants from '../../../../common/config/constants.json' with { type: 'json' };
import { canvasCoords } from './canvasCoords.js';
import { startDrawingMode, stopDrawingMode } from '../draw/mode.js';
import { tryCloseShapeAt, insertPointOnSegment } from '../line/shapeBuilder.js';
import { CHANGE, changed } from '../app/changes.js';

// Hold-to-draw: the machine's wiring plus the seam the touch flow (input.js) drives.
// The mouse drag path stays in controller.js; both reuse the app's drag-state fields.
export class InputController {
  #holdDraw = null;
// A dwell CLOSED the shape while the button is still down; the release must still swallow its click.
  #holdClosedShape = false;
  #holdTickTimer = null;
  #holdAutoEnabled = false;
// A hold stroke extending a line BACKWARD from its first point (points prepended).
  #holdPrepend = false;
  #touch = null;

  constructor(app) {
    this.app = app;
  }

// Keeps the tooltip off a mid-hold ghost line (canvasMouseMove's drag guard).
  get holdEngaged() { return !!this.#holdDraw && this.#holdDraw.engaged; }
  get holdDrawing() { return !!this.#holdDraw && this.#holdDraw.active; }

// The live touch gesture; input.js drives it, this class only ends a tap.
  get touchSession() { return this.#touch; }
  set touchSession(st) { this.#touch = st; }

// HoldDrawController (./holdDraw.js) decides timing/transitions; this wiring owns the DOM
// timers, coordinate conversion and rendering.
  wireHoldDraw() {
    const app = this.app;
    const ctrl = this.#holdDraw = new HoldDrawController({ holdDelay: app.holdDrawDelay });

    const onDown = e => {
      if (e.button !== 0 || e.altKey || e.shiftKey || e.ctrlKey || e.metaKey) return;
      if (app.compareReadOnly()) return;
      this.armHold(e.clientX, e.clientY);
    };
    app.canvas.addEventListener('mousedown', onDown);

    document.addEventListener('mousemove', e => {
      if (!ctrl.engaged) return;
      const r = ctrl.pointerMove(e.clientX, e.clientY, nowMs());
      if (!r) return;
      if (r.type === 'abort') { this.#stopHoldTicks(); ctrl.cancel(); return; }
      if (r.type === 'preview') this.#holdSetPreview(e.clientX, e.clientY);
    });

    document.addEventListener('mouseup', () => {
      if (this.#holdClosedShape) { this.#holdReleaseAfterClose(); return; }
      if (ctrl.state === 'idle') return;
      const r = ctrl.pointerUp(nowMs());
      this.#stopHoldTicks();
      if (r && r.type === 'commit') this.#holdCommit();
      else this.#holdClearPreview();
    });

    window.addEventListener('blur', () => { this.#stopHoldTicks(); ctrl.cancel(); this.#holdClearPreview(); });
  }

  #stopHoldTicks() { if (this.#holdTickTimer) { clearInterval(this.#holdTickTimer); this.#holdTickTimer = null; } }
  #startHoldTicks() { this.#holdTickTimer ??= setInterval(() => this.#holdTick(nowMs()), constants.HOLD_DRAW.tickMs); }

// Shared by mouse and touch; the caller has filtered out modified presses. True if armed.
  armHold(clientX, clientY) {
    const app = this.app;
    if (!holdDrawEligible(app)) return false;
    this.#holdDraw.setHoldDelay(app.holdDrawDelay);
    this.#holdDraw.pointerDown(clientX, clientY, nowMs());
    this.#startHoldTicks();
    return true;
  }

// A second finger, a cancel or a geometry long-press abandons whatever hold was armed.
  abandonHold() {
    this.#stopHoldTicks();
    this.#holdClosedShape = false;
    if (this.#holdDraw.engaged) { this.#holdDraw.cancel(); this.#holdClearPreview(); }
  }

// A tap-mode finger wandering: the machine previews along, or gives the hold up.
  moveTapGesture(clientX, clientY) {
    if (!this.#holdDraw.engaged) return;
    const r = this.#holdDraw.pointerMove(clientX, clientY, nowMs());
    if (!r) return;
    if (r.type === 'abort') { this.#stopHoldTicks(); this.#holdDraw.cancel(); }
    else if (r.type === 'preview') this.#holdSetPreview(clientX, clientY);
  }

// The tap release: commit the held stroke, or let `tap` click through.
  endTapGesture(st, tap) {
    if (this.#holdClosedShape) { this.#holdReleaseAfterClose(); this.#touch = null; return; }
    const r = this.#holdDraw.pointerUp(nowMs());
    this.#stopHoldTicks();
    if (r && r.type === 'commit') { this.#holdCommit(); return; }
    this.#holdClearPreview();
    const kind = classifyEnd({ moved: st.moved || 0, elapsed: nowMs() - st.startT });
    if (kind === 'tap') tap();
  }

  wireTouch() {
    const viewport = document.getElementById('canvas-viewport');
    const { onStart, onMove, onEnd, onCancel } = touchHandlers(this, viewport);
    this.app.canvas.addEventListener('touchstart', onStart, { passive: false });
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
    const { x, y } = canvasCoords(app, clientX, clientY);
    this.#holdAutoEnabled = true;
    const target = holdDrawTarget(app.lines, x, y);
    this.#holdPrepend = false;
    if (target.kind === 'point') {
      app.selectedLineIdx = target.lineIdx;
      app.coordLineIdx = target.lineIdx;
      app.focusedPtIdx = target.ptIdx;
      startDrawingMode(app, { connect: true });
// Holding the FIRST point extends the line backward.
      if (target.ptIdx === 0) { this.#holdPrepend = true; app.continueInsertIdx = 0; }
    } else if (target.kind === 'segment') {
      insertPointOnSegment(app, target.lineIdx, target.ptIdx2, x, y);
      startDrawingMode(app, { connect: true });
    } else {
      startDrawingMode(app, { connect: false });
      if (app.currentLine) {
        app.currentLine.points.push({ x, y });
        app.strokeFx.flyIn(app.currentLine, app.currentLine.points.length - 1);
      }
    }
    this.#holdSetPreviewImg(x, y);
    changed(app, CHANGE.history, CHANGE.drawing, CHANGE.selection);
  }

  #holdDrop(clientX, clientY) {
    const app = this.app;
    const { x, y } = canvasCoords(app, clientX, clientY);
// Resting on the first point closes the shape, as clicking does; end the gesture.
    if (tryCloseShapeAt(app, x, y)) {
      this.#stopHoldTicks();
      this.#holdDraw.cancel();
      this.#holdClearPreview();
      this.#holdClosedShape = true;
      changed(app, CHANGE.drawing, CHANGE.lines);
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
    changed(app, CHANGE.history, CHANGE.lines);
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
    if (app.isDrawing) stopDrawingMode(app);
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
    const { x, y } = canvasCoords(this.app, clientX, clientY);
    this.#holdSetPreviewImg(x, y);
  }
  #holdSetPreviewImg(x, y) { setHoldPreview(this.app, x, y); }
  #holdClearPreview() { clearHoldPreview(this.app); }

  holdAnchorPoint() { return holdAnchor(this.app, this.#holdPrepend); }

// Clamped; persisted.
  setHoldDrawDelay(ms, { persist = true } = {}) {
    const app = this.app;
    const n = Number(ms);
    if (!Number.isFinite(n)) return;
    app.holdDrawDelay = Math.max(100, Math.min(3000, Math.round(n)));
    if (this.#holdDraw) this.#holdDraw.setHoldDelay(app.holdDrawDelay);
    if (persist) app.storage.saveSoon();
  }
}
