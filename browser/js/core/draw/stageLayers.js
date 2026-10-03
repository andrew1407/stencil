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
  #scaled = false;   // the overlay's context carries a density transform

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

  // The overlay emptied at `density` backing px per image px and left drawing in image px (a resize
  // empties it too); false when unlayered.
  clear(canvas, density = 1) {
    const o = this.#overlay;
    if (!o) return false;
    const w = Math.round(canvas.width * density);
    const h = Math.round(canvas.height * density);
    if (o.width !== w || o.height !== h) {
      o.width = w;
      o.height = h;
    } else {
      if (this.#scaled) this.#ctx.setTransform?.(1, 0, 0, 1, 0, 0);
      this.#ctx.clearRect(0, 0, w, h);
    }
    this.#scaled = w !== canvas.width || h !== canvas.height;
    if (this.#scaled) this.#ctx.setTransform?.(w / canvas.width, 0, 0, h / canvas.height, 0, 0);
    return true;
  }

  // Bottom first: what a reader of the stage's pixels composites, in order.
  layers(canvas) { return this.#overlay ? [canvas, this.#overlay] : [canvas]; }
}
