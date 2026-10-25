// The overlay's lines and points as a bitmap, reused while nothing they are painted from changes:
// a hold ghost, a divider drag or a base repaint then costs one blit, not every mark again. The key
// is every value render.js reads, compared exactly; a stroke in flight is never cached.

const LINE_FIELDS = ['hidden', 'color', 'pointColor', 'thickness', 'style', 'locked', 'fillColor', 'pointSize'];

export class MarksCache {
  #canvas = null;
  #ctx = null;
  #scalars = [];
  #fields = [];
  #coords = new Float64Array(0);
  #filled = false;   // the bitmap holds the key's marks

  // True when the key matches the last frame's; the snapshot is rewritten as it is compared.
  #take(app, scalars) {
    let same = scalars.length === this.#scalars.length && scalars.every((v, i) => v === this.#scalars[i]);
    this.#scalars = scalars;
    const lines = app.currentLine ? [...app.lines, app.currentLine] : app.lines;
    let points = 0;
    for (const l of lines) points += l.points.length;
    if (this.#coords.length !== points * 2) { this.#coords = new Float64Array(points * 2); same = false; }
    const fields = this.#fields;
    if (fields.length !== lines.length * (LINE_FIELDS.length + 1)) { fields.length = 0; same = false; }
    let f = 0, c = 0;
    const coords = this.#coords;
    for (const l of lines) {
      for (const k of LINE_FIELDS) { if (fields[f] !== l[k]) { fields[f] = l[k]; same = false; } f++; }
      if (fields[f] !== l.points.length) { fields[f] = l.points.length; same = false; }
      f++;
      for (const p of l.points) {
        if (coords[c] !== p.x) { coords[c] = p.x; same = false; }
        if (coords[c + 1] !== p.y) { coords[c + 1] = p.y; same = false; }
        c += 2;
      }
    }
    return same;
  }

  // The scalars render.js and the highlight state read; the per-line fields and points are walked in #take.
  static scalars(app, readOnly, w, h) {
    return [readOnly, w, h, app.canvas.width, app.canvas.height, app.showLines, app.showPoints, app.pointSize,
      app.selGlowColor, app.hoverRingColor, app.focusRingColor, app.coordLineIdx, app.focusedPtIdx,
      app.hoveredPtIdx, app.hoverPt?.lineIdx, app.hoverPt?.ptIdx, app.listHoverLineIdx, app.selectedLineIdx,
      (app.selectedLines || []).join(','), app.lines.length, !!app.currentLine];
  }

  // Paints the marks onto `target` (an overlay already cleared and scaled to w×h): from the bitmap when
  // the key held, else through `paint(ctx)`; the bitmap fills only once a key has held for a frame.
  draw(app, target, readOnly, w, h, paint) {
    if (typeof OffscreenCanvas !== 'function' || app.strokeFx?.active) {
      this.#filled = false;
      return paint(target);
    }
    if (!this.#take(app, MarksCache.scalars(app, readOnly, w, h))) {
      this.#filled = false;
      return paint(target);
    }
    if (this.#filled) return this.#blit(target, w, h);
    if (!this.#canvas || this.#canvas.width !== w || this.#canvas.height !== h) {
      this.#canvas = new OffscreenCanvas(w, h);
      const ctx = this.#canvas.getContext('2d');
      this.#ctx = typeof ctx?.clearRect === 'function' && typeof ctx.arc === 'function' ? ctx : null;
    }
    if (!this.#ctx) return paint(target);
    this.#ctx.setTransform?.(1, 0, 0, 1, 0, 0);
    this.#ctx.clearRect(0, 0, w, h);
    this.#ctx.setTransform?.(w / app.canvas.width, 0, 0, h / app.canvas.height, 0, 0);
    paint(this.#ctx);
    this.#filled = true;
    return this.#blit(target, w, h);
  }

  #blit(target, w, h) {
    target.save();
    target.setTransform?.(1, 0, 0, 1, 0, 0);
    target.drawImage(this.#canvas, 0, 0, w, h);
    target.restore();
  }
}
