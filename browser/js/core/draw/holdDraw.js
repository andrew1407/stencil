import { findNearestPointWithIdx, findNearestSegmentWithIdx } from './hitTest.js';
import constants from '../../config/constants.json' with { type: 'json' };

const { HIT, HOLD_DRAW } = constants;
const NO_POINTS = Object.freeze({ points: [] });

// Hold-to-draw: press-and-hold near-stationary for `holdDelay` ms drops the first point;
// resting the cursor drops the next; releasing commits. The host owns timers, coordinates
// and rendering; this is pure and time-injected. C++ mirror: core/holdDraw.

// 'point' over a point → continue that line; 'segment' over a body → insert there; 'new' in
// empty space. Topmost wins, as in findNearestPointWithIdx. ptIdx/ptIdx2 = -1 if unused.
export const holdDrawTarget = (lines, x, y, { pointThreshold = HIT.grabRadiusPx, segThreshold = HIT.grabRadiusPx } = {}) => {
  const list = Array.isArray(lines) ? lines.map((l) => (l?.points ? l : NO_POINTS)) : [];
  const p = findNearestPointWithIdx(list, null, x, y, pointThreshold);
  if (p) return { kind: 'point', lineIdx: p.lineIdx, ptIdx: p.ptIdx, ptIdx2: -1 };
  const s = findNearestSegmentWithIdx(list, x, y, segThreshold);
  if (s) return { kind: 'segment', lineIdx: s.lineIdx, ptIdx: s.ptIdx1, ptIdx2: s.ptIdx2 };
  return { kind: 'new', lineIdx: -1, ptIdx: -1, ptIdx2: -1 };
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

  constructor({ holdDelay = HOLD_DRAW.delayMs, moveTolerance = HOLD_DRAW.moveTolerancePx,
    rearmDistance = HOLD_DRAW.rearmDistancePx } = {}) {
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
