import constants from '../config/constants.json' with { type: 'json' };

// Line-snapshot history: deep-copy on push/undo/redo, "step 0 → empty lines, step -1" on undo.

// Depth cap shared with core/state/historyStack.hpp's MAX_STEPS (drift-tested in
// tests/history.test.js); cli's max_states and pystencil's _MAX_STATES carry the same 64.
export const MAX_STEPS = constants.LIMITS.historyMax;

export class HistoryStack {
  constructor() {
    this.history = [];
    this.historyStep = -1;
  }

// baseStep: loadImage passes lines.length > 0 ? 0 : -1, restore passes 0.
  reset(lines, baseStep) {
    const step = baseStep !== undefined ? baseStep : (lines.length > 0 ? 0 : -1);
// A negative step means "no current snapshot": keep the history empty so canRedo() stays
// false (a phantom empty snapshot would surface a stray redo after a blank image).
    this.history = step >= 0 ? [this.#clone(lines)] : [];
    this.historyStep = step;
  }

  push(lines) {
    this.historyStep++;
// Truncate in place: a no-op in the common no-redo case, no reallocation per push.
    if (this.history.length > this.historyStep) this.history.length = this.historyStep;
    this.history.push(this.#clone(lines));
// Evict the oldest and shift the cursor down by as many; undoing off the trimmed front
// still ends at the "empty lines, step -1" stop.
    if (this.history.length > MAX_STEPS) {
      this.historyStep -= this.history.splice(0, this.history.length - MAX_STEPS).length;
    }
  }

  canUndo() {
    return this.historyStep >= 0;
  }

  canRedo() {
    return this.historyStep < this.history.length - 1;
  }

// The lines to apply, or null.
  undo() {
    if (this.historyStep > 0) {
      this.historyStep--;
      return this.#clone(this.history[this.historyStep]);
    } else if (this.historyStep === 0) {
      this.historyStep = -1;
      return [];
    }
    return null;
  }

// The lines to apply, or null.
  redo() {
    if (this.historyStep < this.history.length - 1) {
      this.historyStep++;
      return this.#clone(this.history[this.historyStep]);
    }
    return null;
  }

// Stored history must be immune to later mutation of live lines (their points arrays).
  #clone(lines) {
    return structuredClone(lines);
  }
}
