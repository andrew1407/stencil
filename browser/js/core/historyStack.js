// ── Line-snapshot history (pure data structure) ─────────────────
// Mirrors DrawingApp's `history` array + `historyStep` cursor semantics exactly:
// deep-copy on push/undo/redo and the "step 0 → empty lines, step -1" undo behavior.
export class HistoryStack {
  constructor() {
    this.history = [];
    this.historyStep = -1;
  }

  // Initialize from a base lines array. baseStep reproduces the original variants:
  //   loadImage: step = lines.length > 0 ? 0 : -1;  restore: step = 0
  reset(lines, baseStep) {
    this.history = [this.#clone(lines)];
    this.historyStep = baseStep !== undefined
      ? baseStep
      : (lines.length > 0 ? 0 : -1);
  }

  // Push a new snapshot of `lines`, truncating any redo branch first.
  push(lines) {
    this.historyStep++;
    // Drop any redo branch by truncating in place (a no-op in the common
    // no-redo case, where historyStep now equals history.length) — avoids
    // reallocating the whole array on every push.
    if (this.history.length > this.historyStep) this.history.length = this.historyStep;
    this.history.push(this.#clone(lines));
  }

  canUndo() {
    return this.historyStep >= 0;
  }

  canRedo() {
    return this.historyStep < this.history.length - 1;
  }

  // Step back one snapshot. Returns the lines to apply, or null if nothing.
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

  // Step forward one snapshot. Returns the lines to apply, or null if nothing.
  redo() {
    if (this.historyStep < this.history.length - 1) {
      this.historyStep++;
      return this.#clone(this.history[this.historyStep]);
    }
    return null;
  }

  // Deep-copy a snapshot so stored history is immune to later mutation of live lines
  // (their points arrays especially). structuredClone is the modern dependency-free deep
  // clone — faithful to the old JSON round-trip for this plain-data shape, without its cost.
  #clone(lines) {
    return structuredClone(lines);
  }
}
