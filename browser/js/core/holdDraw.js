import { distToSegment } from '../utils.js';

// ── Hold-to-draw: pure decision + gesture state machine ─────────
// An alternative drawing flow: press-and-hold the left button (no modifiers)
// near-stationary for `holdDelay` ms → drawing auto-enables and the first
// point drops; moving shows a faded preview line; resting the cursor for
// `holdDelay` drops the next point; releasing commits the line and disables
// drawing again. The host (DrawingApp / CanvasWidget) owns timers, coordinate
// conversion and rendering; this module is pure and time-injected so it can be
// unit-tested without real timers or DOM events. C++ mirror: core/holdDraw.

// Decide what an initial hold over (x,y) targets, given the committed lines:
//   • 'point'   — over an existing point → continue that line from it
//   • 'segment' — over a line body (not a point) → insert a point there, continue
//   • 'new'     — empty space → start a fresh line
// Mirrors the reverse-iteration / topmost-wins semantics of DrawingApp's
// #findNearestPointWithIdx + #findNearestSegmentWithIdx. Returns
// { kind, lineIdx, ptIdx, ptIdx2 } (ptIdx/ptIdx2 = -1 when not applicable).
export const holdDrawTarget = (lines, x, y, { pointThreshold = 12, segThreshold = 12 } = {}) => {
  const list = Array.isArray(lines) ? lines : [];
  // Topmost line wins → iterate last-to-first.
  for (let li = list.length - 1; li >= 0; li--) {
    const pts = (list[li] && list[li].points) || [];
    for (let pi = 0; pi < pts.length; pi++) {
      if (Math.hypot(pts[pi].x - x, pts[pi].y - y) < pointThreshold)
        return { kind: 'point', lineIdx: li, ptIdx: pi, ptIdx2: -1 };
    }
  }
  let bestDist = Infinity;
  let best = null;
  for (let li = list.length - 1; li >= 0; li--) {
    const pts = (list[li] && list[li].points) || [];
    for (let pi = 0; pi < pts.length - 1; pi++) {
      const d = distToSegment(x, y, pts[pi], pts[pi + 1]);
      if (d < segThreshold && d < bestDist) {
        bestDist = d;
        best = { kind: 'segment', lineIdx: li, ptIdx: pi, ptIdx2: pi + 1 };
      }
    }
  }
  return best || { kind: 'new', lineIdx: -1, ptIdx: -1, ptIdx2: -1 };
};

const dist = (ax, ay, bx, by) => Math.hypot(ax - bx, ay - by);

// Gesture state machine. Coordinates are host screen/client space (zoom-independent);
// times are arbitrary monotonic ms. States: idle → armed → drawing → idle (commit), or
// armed → aborted when the pointer moves too far before the hold completes.
//
// Driver contract (host):
//   pointerDown(x,y,t)  on a plain left mousedown that is eligible for hold-draw
//   pointerMove(x,y,t)  on every mousemove while engaged
//   tick(t)             repeatedly (e.g. ~40ms) while engaged
//   pointerUp(t)        on mouseup
//   cancel()            on blur / interruption
// Each call returns null or an action object the host acts on:
//   {type:'armed'}            pointerDown accepted, hold timer running
//   {type:'abort'}            moved too far → not a hold; let the click stand
//   {type:'start',  x, y}     hold completed → enable drawing, drop first point
//   {type:'drop',   x, y}     dwell completed → drop a point
//   {type:'preview',x, y}     cursor moved while drawing → update the ghost line
//   {type:'commit'}           released after drawing → commit + disable drawing
export class HoldDrawController {
  #state = 'idle';
  #holdDelay;
  #moveTol;
  #rearm;
  #pressX = 0;
  #pressY = 0;
  #pressT = 0;
  #stillX = 0;
  #stillY = 0;
  #stillSince = 0;
  #lastDropX = 0;
  #lastDropY = 0;
  #armedForDrop = false;

  constructor({ holdDelay = 500, moveTolerance = 6, rearmDistance = 10 } = {}) {
    this.#holdDelay = Math.max(0, holdDelay);
    this.#moveTol = moveTolerance;
    this.#rearm = rearmDistance;
  }

  get state() { return this.#state; }
  get active() { return this.#state === 'drawing'; }
  get engaged() { return this.#state === 'armed' || this.#state === 'drawing'; }
  setHoldDelay(ms) { const n = Number(ms); if (Number.isFinite(n) && n >= 0) this.#holdDelay = n; }
  get holdDelay() { return this.#holdDelay; }

  cancel() { this.#state = 'idle'; this.#armedForDrop = false; }

  pointerDown(x, y, t) {
    this.#state = 'armed';
    this.#pressX = x; this.#pressY = y; this.#pressT = t;
    this.#stillX = x; this.#stillY = y; this.#stillSince = t;
    this.#armedForDrop = false;
    return { type: 'armed' };
  }

  pointerMove(x, y, t) {
    if (this.#state === 'armed') {
      // Moving away from the press point before the hold fires = a real
      // click/drag, not a hold → abort and let the host's normal handling run.
      if (dist(x, y, this.#pressX, this.#pressY) > this.#moveTol) {
        this.#state = 'aborted';
        return { type: 'abort' };
      }
      return null;
    }
    if (this.#state === 'drawing') {
      // New dwell window whenever the cursor leaves the current rest neighborhood.
      if (dist(x, y, this.#stillX, this.#stillY) > this.#moveTol) {
        this.#stillX = x; this.#stillY = y; this.#stillSince = t;
      }
      // Re-arm a drop only once the cursor has left the last dropped point's vicinity.
      if (dist(x, y, this.#lastDropX, this.#lastDropY) > this.#rearm) this.#armedForDrop = true;
      return { type: 'preview', x, y };
    }
    return null;
  }

  tick(t) {
    if (this.#state === 'armed') {
      if (t - this.#pressT >= this.#holdDelay) {
        this.#state = 'drawing';
        this.#lastDropX = this.#pressX; this.#lastDropY = this.#pressY;
        this.#stillX = this.#pressX; this.#stillY = this.#pressY; this.#stillSince = t;
        this.#armedForDrop = false;
        return { type: 'start', x: this.#pressX, y: this.#pressY };
      }
      return null;
    }
    if (this.#state === 'drawing') {
      if (this.#armedForDrop && t - this.#stillSince >= this.#holdDelay) {
        this.#armedForDrop = false;
        this.#lastDropX = this.#stillX; this.#lastDropY = this.#stillY;
        this.#stillSince = t;
        return { type: 'drop', x: this.#stillX, y: this.#stillY };
      }
      return null;
    }
    return null;
  }

  pointerUp(_t) {
    const wasDrawing = this.#state === 'drawing';
    this.#state = 'idle';
    this.#armedForDrop = false;
    return wasDrawing ? { type: 'commit' } : null;
  }
}
