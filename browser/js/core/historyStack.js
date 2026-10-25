import constants from '../../../common/config/constants.json' with { type: 'json' };

// Snapshot history: a snapshot is a Lines array, or an editor memento {lines, cropRect,
// rotationQuarters, mirrored, filter, filterColor} — the view and the filter the lines sit on, so a crop, a
// turn or a filter switch is one undo step too. Deep-copied on push/undo/redo; "step 0 → the
// floor, step -1" on undo. Twin: core/state/HistoryStack.

// Depth cap shared with core/state/HistoryStack.hpp's MAX_STEPS (drift-tested in
// tests/core/history.test.js); cli's max_states and pystencil's _MAX_STATES carry the same 64.
export const MAX_STEPS = constants.LIMITS.historyMax;
// Points kept across every step: two layouts at layoutPointsMax, so a full layout still has one
// undo step; pinned beside MAX_STEPS in core/state/HistoryStack.hpp's MAX_POINTS.
export const MAX_POINTS = constants.LIMITS.historyPointsMax;

const linesOf = (s) => (Array.isArray(s) ? s : (s?.lines ?? []));
const pointsOf = (s) => linesOf(s).reduce((n, l) => n + (l?.points?.length ?? 0), 0);
// The step -1 stop: no lines, on the view of the step the stack starts from.
const floorOf = (s) => (Array.isArray(s) || !s ? [] : { ...s, lines: [] });

// Points dominate a step, so a stored line keeps its plain {x, y} points as one Float64Array (a
// tenth of structuredClone's cost, a third of its memory); a point carrying anything else keeps
// the whole array as a deep copy.
const plainPoint = (p) => {
  let n = 0;
  for (const k in p) { if (k !== 'x' && k !== 'y') return false; n++; }
  return n === 2 && typeof p.x === 'number' && typeof p.y === 'number';
};
const packPoints = (points) => {
  const flat = new Float64Array(points.length * 2);
  for (let i = 0; i < points.length; i++) {
    const p = points[i];
    if (!p || !plainPoint(p)) return structuredClone(points);
    flat[2 * i] = p.x;
    flat[2 * i + 1] = p.y;
  }
  return flat;
};
const unpackPoints = (flat) => {
  const out = new Array(flat.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = { x: flat[2 * i], y: flat[2 * i + 1] };
  return out;
};
// A copy of `o` with key `k` taken through `fn`, every key kept in its place: a layout's bytes
// follow its key order.
const copyWith = (o, k, fn) => {
  const out = {};
  for (const key of Object.keys(o)) {
    const v = o[key];
    out[key] = key === k ? fn(v) : (v !== null && typeof v === 'object' ? structuredClone(v) : v);
  }
  return out;
};
const storeLine = (l) => (l && Array.isArray(l.points) ? copyWith(l, 'points', packPoints) : structuredClone(l));
const liveLine = (l) => (l && ArrayBuffer.isView(l.points) ? copyWith(l, 'points', unpackPoints) : structuredClone(l));
const mapLines = (s, fn) => {
  if (Array.isArray(s)) return s.map(fn);
  if (!s || !Array.isArray(s.lines)) return structuredClone(s);
  return copyWith(s, 'lines', (lines) => lines.map(fn));
};

// One undo step of the editor, core's EditorMemento: the lines, the crop and turn under them, and
// the filter over the picture.
export const editorMemento = (app) => ({
  lines: app.lines,
  cropRect: app.cropRect,
  rotationQuarters: app.rotationQuarters,
  mirrored: !!app.mirrored,
  filter: app.imageFilter,
  filterColor: app.filterColor,
});

// The step on screen: the one the cursor names, the floor at step -1.
export const cursorStep = (h) => (h.historyStep >= 0 ? h.history[h.historyStep] : h.floor);

// A step without a filter (a Lines snapshot) leaves the filter as it is, so it names none.
export const sameFilter = (step, app) => !!step?.filter
  && step.filter === app.imageFilter && step.filterColor === app.filterColor;

export class HistoryStack {
// #points[i]: the points of history[i]; #total: their sum, kept on push, truncation and eviction.
  #points = [];
  #total = 0;

  constructor() {
    this.history = [];
    this.historyStep = -1;
    this.floor = [];
  }

// baseStep: loadImage passes lines.length > 0 ? 0 : -1, restore passes 0.
  reset(snapshot, baseStep) {
    const step = baseStep !== undefined ? baseStep : (linesOf(snapshot).length > 0 ? 0 : -1);
// A negative step means "no current snapshot": keep the history empty so canRedo() stays
// false (a phantom empty snapshot would surface a stray redo after a blank image).
    this.history = step >= 0 ? [this.#store(snapshot)] : [];
    this.#points = step >= 0 ? [pointsOf(snapshot)] : [];
    this.#total = this.#points[0] ?? 0;
    this.historyStep = step;
    this.floor = this.#store(floorOf(snapshot));
  }

  push(snapshot) {
    this.historyStep++;
// Truncate in place: a no-op in the common no-redo case, no reallocation per push.
    if (this.history.length > this.historyStep) this.history.length = this.historyStep;
    for (const n of this.#points.splice(this.historyStep)) this.#total -= n ?? 0;
    this.history.push(this.#store(snapshot));
    const n = pointsOf(snapshot);
    this.#points[this.history.length - 1] = n;
    this.#total += n;
// Evict the oldest past the depth, then while the points kept exceed MAX_POINTS (never the step
// just pushed); the floor takes the view of the last one evicted, so undo still ends at step -1.
    const len = this.history.length;
    let drop = 0;
    while (len - drop > MAX_STEPS || (drop < len - 1 && this.#total > MAX_POINTS)) this.#total -= this.#points[drop++] ?? 0;
    if (drop > 0) {
      const dropped = this.history.splice(0, drop);
      this.#points.splice(0, drop);
      this.historyStep -= drop;
      this.floor = floorOf(dropped[dropped.length - 1]);
    }
  }

  canUndo() {
    return this.historyStep >= 0;
  }

  canRedo() {
    return this.historyStep < this.history.length - 1;
  }

// The snapshot to apply, or null.
  undo() {
    if (this.historyStep > 0) {
      this.historyStep--;
      return this.#live(this.history[this.historyStep]);
    } else if (this.historyStep === 0) {
      this.historyStep = -1;
      return this.#live(this.floor);
    }
    return null;
  }

// The snapshot to apply, or null.
  redo() {
    if (this.historyStep < this.history.length - 1) {
      this.historyStep++;
      return this.#live(this.history[this.historyStep]);
    }
    return null;
  }

// Stored history must be immune to later mutation of live lines (their points arrays), and what
// undo/redo hand back must be free to mutate: both directions copy.
  #store(snapshot) { return mapLines(snapshot, storeLine); }

  #live(stored) { return mapLines(stored, liveLine); }
}
