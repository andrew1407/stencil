// The editor's two stacked canvases. #canvas holds the picture — the filtered base, and the
// original over a split compare's original side — and repaints only when that picture changes;
// the overlay above it holds the lines, rings, handles and the divider and repaints every frame.
// Zoom is CSS on both, so it never repaints the picture. With no overlay attached, everything
// paints on #canvas every frame.
const sameKey = (a, b) => !!a && a.length === b.length && a.every((v, i) => v === b[i]);

export class StageLayers {
  #overlay = null;
  #ctx = null;
  #painted = null;   // the key of the picture #canvas holds

  attach(canvas) {
    this.#ctx = canvas?.getContext?.('2d') || null;
    this.#overlay = this.#ctx ? canvas : null;
    this.#painted = null;
  }

  get overlay() { return this.#overlay; }
  get ctx() { return this.#ctx; }

  // True, and then remembered, when #canvas does not hold `key`'s picture; always true unlayered.
  stale(key) {
    if (!this.#ctx) return true;
    if (sameKey(this.#painted, key)) return false;
    this.#painted = key;
    return true;
  }

  invalidate() { this.#painted = null; }

  // The overlay emptied at #canvas's backing size (a resize empties it too); false when unlayered.
  clear(canvas) {
    const o = this.#overlay;
    if (!o) return false;
    if (o.width !== canvas.width || o.height !== canvas.height) {
      o.width = canvas.width;
      o.height = canvas.height;
    } else this.#ctx.clearRect(0, 0, o.width, o.height);
    return true;
  }

  // Bottom first: what a reader of the stage's pixels composites, in order.
  layers(canvas) { return this.#overlay ? [canvas, this.#overlay] : [canvas]; }
}
