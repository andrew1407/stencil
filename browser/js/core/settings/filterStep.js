// The filter as an undo step: a committed mode or tint pushes one editor memento, and only when it
// moved the filter off the step on screen; the commits of one batch share a step. The desktop's
// twin is its filter commit over core's EditorMemento.
import { cursorStep, sameFilter } from '../historyStack.js';

export class FilterSteps {
  #held = 0;
  #moved = false;

  constructor(app) {
    this.app = app;
  }

  // Inside batch() a commit waits for the batch's end.
  committed() {
    if (this.#held) this.#moved = true;
    else this.#push();
  }

  // The commits `fn` makes land as one undo step; `history: false` records none.
  batch(fn, { history = true } = {}) {
    this.#held++;
    try {
      return fn();
    } finally {
      if (--this.#held === 0 && this.#moved) {
        this.#moved = false;
        if (history) this.#push();
      }
    }
  }

  // An editor with no picture has nothing to step back to.
  #push() {
    const app = this.app;
    if (app.image && !sameFilter(cursorStep(app.history), app)) app.saveHistory();
  }
}
