import { perFrame } from '../../utils.js';
import { BaseLayer } from './baseLayer.js';
import { filteredFill } from '../image/filterCanvas.js';
import { StageLayers } from './stageLayers.js';
import { paintOriginalSide, eraseOriginalSide, paintDivider, splitFraction } from './compareSplit.js';
import { drawLine as paintLine, drawPoint as paintPoint, pointColorOf } from '../line/render.js';
import { selectionPredicate } from '../line/selection.js';
import constants from '../../config/constants.json' with { type: 'json' };
// Per-frame composition over two layers (stageLayers.js): the picture on #canvas, repainted only
// when it changes, and the lines, points, hold preview and divider on the overlay above it. The
// base lives in baseLayer.js, its filter chain in filterCanvas.js, one line/point in render.js.
export { pointColorOf };

const { HOLD_DRAW } = constants;

export class Renderer {
  #base = new BaseLayer(() => this.requestRedraw());
  #stage = new StageLayers();
  #frame = null;   // the overlay's context while a layered frame paints
  #fill = null;    // { image, color, key, css }: a blank's recolour on trial, over that image only
  // Set per-frame in redraw(): true suppresses selection glow + hover/focus rings (the
  // read-only compare views draw a clean picture). Read by lineRender's glow decision.
  suppressHighlight = false;

  constructor(app) {
    this.app = app;
  }

  // A drag's burst of moves paints once, on the next frame; one-shot edits keep redraw().
  requestRedraw = perFrame(() => this.redraw());

  // Where a line or point paints: the overlay mid-frame, else app.ctx (an export points it at its own canvas).
  get ctx() { return this.#frame || this.app.ctx; }

  // The view's lines canvas, stacked over #canvas; the next frame repaints the picture without lines.
  useOverlay(canvas) {
    this.#stage.attach(canvas);
    if (this.app.image) this.requestRedraw();
  }

  // A blank recolour on trial: the picture paints as a `color` fill until the image changes; null ends it.
  previewFill(color) {
    this.#fill = color && this.app.image ? { image: this.app.image, color, key: null, css: color } : null;
    this.requestRedraw();
  }

  // The trial fill as the base would paint it, filter and all; null when none applies.
  #fillColor(compare) {
    const f = this.#fill;
    if (f && f.image !== this.app.image) this.#fill = null;
    if (!this.#fill) return null;
    if (compare === 'original') return f.color;
    const [, filter, tint] = this.#baseKey();
    if (!f.key || f.key[0] !== filter || f.key[1] !== tint) {
      f.key = [filter, tint];
      f.css = filteredFill(f.color, filter, tint);
    }
    return f.css;
  }

  // What the stage shows, bottom layer first: a reader of its pixels composites these in order.
  layers() { return this.#stage.layers(this.app.canvas); }

  // Every filter blits its cached copy (baseLayer.js); only a pixel, filter or tint change
  // rebuilds it, so a hover repaint never re-runs a filter over the full image.
  #paintBase(ctx, source) {
    if (source !== this.app.image) ctx.filter = 'none';
    ctx.drawImage(source, 0, 0);
  }

  #baseKey() {
    const { image, imageFilter } = this.app;
    return [image, imageFilter, imageFilter === 'custom' ? (this.app.filterColor || '#7c3aed') : null];
  }

  // Export's pixels, now; a frame may show the base on screen while a worker copy is painted.
  restingBase() { return this.#base.now(...this.#baseKey()); }

  drawImageWithFilter(ctx) { this.#paintBase(ctx, this.restingBase()); }

  // An Alt+Shift+O hold forces 'original' regardless of the selected mode.
  effectiveCompareMode() {
    return this.app.compareHoldOriginal ? 'original' : (this.app.compareMode || 'none');
  }

  redraw() {
    const app = this.app;
    const layered = this.#stage.clear(app.canvas);
    if (!app.image) { this.#stage.invalidate(); return; }

    const compare = this.effectiveCompareMode();
    const isSplit = compare === 'vertical' || compare === 'horizontal';
    const fill = this.#fillColor(compare);
    const source = fill || (compare === 'original' ? app.image : this.#base.frame(...this.#baseKey()));
    if (this.#stage.stale([source, compare, isSplit ? splitFraction(app) : null, app.canvas.width, app.canvas.height])) {
      app.ctx.clearRect(0, 0, app.canvas.width, app.canvas.height);
      if (fill) {
        app.ctx.filter = 'none';
        app.ctx.fillStyle = fill;
        app.ctx.fillRect(0, 0, app.canvas.width, app.canvas.height);
      } else if (compare === 'original') {
        app.ctx.filter = 'none';
        app.ctx.drawImage(app.image, 0, 0);
      } else this.#paintBase(app.ctx, source);
      if (layered && isSplit) paintOriginalSide(app.ctx, app, compare);
    }
    if (compare === 'original') return;

    this.#frame = layered ? this.#stage.ctx : null;
    try {
      this.#paintAnnotations(isSplit);
      if (layered && isSplit) { eraseOriginalSide(this.ctx, app, compare); paintDivider(this.ctx, app, compare); }
    } finally {
      this.#frame = null;
    }
    // The untouched original over the original-side region, then the movable divider.
    if (!layered && isSplit) this.drawCompareSplit(compare);
  }

  // Lines (or their points alone) and the hold preview; a split view's edit side is read-only,
  // so it shows no selection glow or hover/focus rings.
  #paintAnnotations(readOnly) {
    const app = this.app;
    this.suppressHighlight = readOnly;
    const selected = readOnly ? () => false : selectionPredicate(app);

    if (app.showLines) {
      app.lines.forEach((line, i) => this.drawLine(line, selected(i), i));
      if (app.currentLine?.points.length > 0)
        this.drawLine(app.currentLine, false, -1);
    } else if (app.showPoints) {
      // Committed lines, then the in-progress line, so no combined array is cloned per frame.
      const drawPts = (line, li, sel) => {
        const ms = line.pointSize ?? app.pointSize;
        const fx = app.strokeFx;
        const pts = fx.pointsOf(line);
        pts.forEach((p, pi) => {
          const hs = this.pointHighlightState(li, pi);
          this.drawPoint(p, pointColorOf(line), ms * fx.scaleAt(line.points[pi]), sel, hs);
        });
        fx.paintOver(this.ctx, line, pts);
      };
      app.lines.forEach((line, i) => drawPts(line, i, selected(i)));
      if (app.currentLine) drawPts(app.currentLine, -1, false);
    }

    if (app.holdPreview) this.drawHoldPreview();
  }

  // Both halves into this.ctx; `withDivider: false` (the export path's clean split) skips the bar and knob.
  drawCompareSplit(mode, { withDivider = true } = {}) {
    paintOriginalSide(this.ctx, this.app, mode);
    if (withDivider) paintDivider(this.ctx, this.app, mode);
  }

  // Dashed segment from the stroke's anchor to the live cursor plus a ghost point: where
  // the next hold-to-draw point would land. Never committed.
  drawHoldPreview() {
    const app = this.app;
    const p = app.holdPreview;
    if (!p) return;
    const ctx = this.ctx;
    const anchor = typeof app.input?.holdAnchorPoint === 'function' ? app.input.holdAnchorPoint() : null;
    ctx.save();
    if (anchor) {
      ctx.globalAlpha = HOLD_DRAW.ghostLineAlpha;
      ctx.strokeStyle = app.color;
      ctx.lineWidth = app.thickness;
      ctx.lineCap = 'round';
      ctx.setLineDash(HOLD_DRAW.ghostDashPx);
      ctx.beginPath();
      ctx.moveTo(anchor.x, anchor.y);
      ctx.lineTo(p.x, p.y);
      ctx.stroke();
      ctx.setLineDash([]);
    }
    ctx.globalAlpha = HOLD_DRAW.ghostPointAlpha;
    ctx.fillStyle = app.pointColor || app.color;
    ctx.beginPath();
    ctx.arc(p.x, p.y, app.pointSize, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
  }

  drawLine(line, isSelected = false, lineIdx = -99) { paintLine(this, line, isSelected, lineIdx); }

  drawPoint(point, color, pointSize = 4, isSelected = false, highlightState = 0) {
    paintPoint(this, point, color, pointSize, isSelected, highlightState);
  }

  // 0 none, 1 hover, 2 focused — regardless of whether its line is shown in the coord table.
  pointHighlightState(lineIdx, ptIdx) {
    if (this.suppressHighlight) return 0;
    if (lineIdx === this.app.coordLineIdx && ptIdx === this.app.focusedPtIdx) return 2;
    if (this.app.hoverPt && this.app.hoverPt.lineIdx === lineIdx && this.app.hoverPt.ptIdx === ptIdx) return 1;
    if (lineIdx === this.app.coordLineIdx && ptIdx === this.app.hoveredPtIdx) return 1;
    // Lines-list row hover rings the whole line's points (visible in points-only view too).
    if (lineIdx >= 0 && lineIdx === (this.app.listHoverLineIdx ?? -1)) return 1;
    return 0;
  }
}
