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
    this.history = step >= 0 ? [this.#clone(snapshot)] : [];
    this.historyStep = step;
    this.floor = this.#clone(floorOf(snapshot));
  }

  push(snapshot) {
    this.historyStep++;
// Truncate in place: a no-op in the common no-redo case, no reallocation per push.
    if (this.history.length > this.historyStep) this.history.length = this.historyStep;
    this.history.push(this.#clone(snapshot));
// Evict the oldest past the depth, then while the points kept exceed MAX_POINTS (never the step
// just pushed); the floor takes the view of the last one evicted, so undo still ends at step -1.
    const len = this.history.length;
    let drop = Math.max(0, len - MAX_STEPS);
    let kept = 0;
    for (let i = drop; i < len; i++) kept += pointsOf(this.history[i]);
    while (drop < len - 1 && kept > MAX_POINTS) kept -= pointsOf(this.history[drop++]);
    if (drop > 0) {
      const dropped = this.history.splice(0, drop);
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
      return this.#clone(this.history[this.historyStep]);
    } else if (this.historyStep === 0) {
      this.historyStep = -1;
      return this.#clone(this.floor);
    }
    return null;
  }

// The snapshot to apply, or null.
  redo() {
    if (this.historyStep < this.history.length - 1) {
      this.historyStep++;
      return this.#clone(this.history[this.historyStep]);
    }
    return null;
  }

// Stored history must be immune to later mutation of live lines (their points arrays).
  #clone(snapshot) {
    return structuredClone(snapshot);
  }
}
