import { distToSegment } from '../../utils.js';

// Hold-to-draw: press-and-hold near-stationary for `holdDelay` ms drops the first point;
// resting the cursor drops the next; releasing commits. The host owns timers, coordinates
// and rendering; this is pure and time-injected. C++ mirror: core/holdDraw.

// 'point' over a point → continue that line; 'segment' over a body → insert there; 'new' in
// empty space. Topmost wins, as in findNearestPointWithIdx. ptIdx/ptIdx2 = -1 if unused.
export const holdDrawTarget = (lines, x, y, { pointThreshold = 12, segThreshold = 12 } = {}) => {
  const list = Array.isArray(lines) ? lines : [];
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

// idle → armed → drawing → idle (commit), or armed → aborted past the travel slop.
// Coordinates are host client space, times monotonic ms.
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
// Moved before the hold fired: a real click/drag, not a hold.
      if (dist(x, y, this.#pressX, this.#pressY) > this.#moveTol) {
        this.#state = 'aborted';
        return { type: 'abort' };
      }
      return null;
    }
    if (this.#state === 'drawing') {
// New dwell window whenever the cursor leaves the rest neighbourhood.
      if (dist(x, y, this.#stillX, this.#stillY) > this.#moveTol) {
        this.#stillX = x; this.#stillY = y; this.#stillSince = t;
      }
// Re-arm a drop only once the cursor has left the last dropped point.
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
