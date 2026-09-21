import { ImageFilterCanvas } from './image/imageFilterCanvas.js';
import { drawLine as paintLine, drawPoint as paintPoint, pointColorOf } from './line/lineRender.js';
// Per-frame composition: filtered image, lines, points, compare split. The filter chain
// lives in imageFilterCanvas.js, one line/point in lineRender.js.
export { pointColorOf };

export class Renderer {
  #filters = new ImageFilterCanvas();
  // Set per-frame in redraw(): true suppresses selection glow + hover/focus rings (the
  // read-only compare views draw a clean picture). Read by lineRender's glow decision.
  suppressHighlight = false;

  constructor(app) {
    this.app = app;
  }

  drawImageWithFilter(ctx) {
    if (this.app.imageFilter === 'bw') {
      ctx.filter = 'grayscale(100%)';
      ctx.drawImage(this.app.image, 0, 0);
      ctx.filter = 'none';
    } else if (this.app.imageFilter === 'sepia') {
      ctx.filter = 'sepia(100%)';
      ctx.drawImage(this.app.image, 0, 0);
      ctx.filter = 'none';
    } else if (this.app.imageFilter === 'invert') {
      ctx.filter = 'invert(100%)';
      ctx.drawImage(this.app.image, 0, 0);
      ctx.filter = 'none';
    } else if (this.app.imageFilter === 'contour') {
      // Sobel needs the pixel neighborhood, so no CSS filter exists: blit the cached
      // filtered copy (rebuilt only when the image changes).
      ctx.filter = 'none';
      ctx.drawImage(this.#filters.canvasFor(this.app.image, 'contour', null), 0, 0);
    } else if (this.app.imageFilter === 'custom') {
      const color = this.app.filterColor || '#7c3aed';
      ctx.filter = 'none';
      ctx.drawImage(this.#filters.canvasFor(this.app.image, 'custom', color), 0, 0);
    } else {
      ctx.drawImage(this.app.image, 0, 0);
    }
  }

  // An Alt+Shift+O hold forces 'original' regardless of the selected mode.
  effectiveCompareMode() {
    return this.app.compareHoldOriginal ? 'original' : (this.app.compareMode || 'none');
  }

  redraw() {
    if (!this.app.image) return;

    this.app.ctx.clearRect(0, 0, this.app.canvas.width, this.app.canvas.height);

    const compare = this.effectiveCompareMode();
    if (compare === 'original') {
      this.app.ctx.filter = 'none';
      this.app.ctx.drawImage(this.app.image, 0, 0);
      return;
    }

    this.drawImageWithFilter(this.app.ctx);

    // A split compare view's edit side is read-only: no selection glow or hover/focus rings.
    const ro = compare !== 'none';
    this.suppressHighlight = ro;

    if (this.app.showLines) {
      this.app.lines.forEach((line, i) => this.drawLine(line, ro ? false : this.app.isLineSelected(i), i));
      if (this.app.currentLine && this.app.currentLine.points.length > 0)
        this.drawLine(this.app.currentLine, false, -1);
    } else if (this.app.showPoints) {
      // Committed lines, then the in-progress line, so no combined array is cloned per frame.
      const drawPts = (line, li, sel) => {
        const ms = line.pointSize ?? this.app.pointSize;
        const fx = this.app.strokeFx;
        const pts = fx.pointsOf(line);
        pts.forEach((p, pi) => {
          const hs = this.pointHighlightState(li, pi);
          this.drawPoint(p, pointColorOf(line), ms * fx.scaleAt(line.points[pi]), sel, hs);
        });
        fx.paintOver(this.app.ctx, line, pts);
      };
      this.app.lines.forEach((line, i) => drawPts(line, i, ro ? false : this.app.isLineSelected(i)));
      if (this.app.currentLine) drawPts(this.app.currentLine, -1, false);
    }

    if (this.app.holdPreview) this.drawHoldPreview();

    // The untouched original over the original-side region, then the movable divider.
    if (compare === 'vertical' || compare === 'horizontal') this.drawCompareSplit(compare);
  }

  // `withDivider: false` (the export path's clean split) skips the divider bar/knob.
  drawCompareSplit(mode, { withDivider = true } = {}) {
    const ctx = this.app.ctx;
    const w = this.app.canvas.width;
    const h = this.app.canvas.height;
    const f = Math.min(1, Math.max(0, this.app.compareSplit ?? 0.5));

    ctx.save();
    ctx.beginPath();
    if (mode === 'vertical') ctx.rect(0, 0, w * f, h);
    else ctx.rect(0, 0, w, h * f);
    ctx.clip();
    ctx.filter = 'none';
    ctx.drawImage(this.app.image, 0, 0);
    ctx.restore();

    if (!withDivider) return;

    // Image space, but a constant on-screen thickness: divided by the zoom.
    const scale = this.app.scale || 1;
    const lw = 2 / scale;
    const knob = 7 / scale;
    ctx.save();
    ctx.strokeStyle = 'rgba(255,255,255,0.95)';
    ctx.fillStyle = 'rgba(255,255,255,0.95)';
    ctx.lineWidth = lw;
    ctx.shadowColor = 'rgba(0,0,0,0.55)';
    ctx.shadowBlur = 3 / scale;
    ctx.beginPath();
    if (mode === 'vertical') {
      const x = w * f;
      ctx.moveTo(x, 0); ctx.lineTo(x, h);
      ctx.stroke();
      ctx.beginPath();
      ctx.arc(x, h / 2, knob, 0, Math.PI * 2);
      ctx.fill();
    } else {
      const y = h * f;
      ctx.moveTo(0, y); ctx.lineTo(w, y);
      ctx.stroke();
      ctx.beginPath();
      ctx.arc(w / 2, y, knob, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.restore();
  }

  // Dashed segment from the stroke's anchor to the live cursor plus a ghost point: where
  // the next hold-to-draw point would land. Never committed.
  drawHoldPreview() {
    const app = this.app;
    const p = app.holdPreview;
    if (!p) return;
    const ctx = app.ctx;
    const anchor = typeof app.input?.holdAnchorPoint === 'function' ? app.input.holdAnchorPoint() : null;
    ctx.save();
    if (anchor) {
      ctx.globalAlpha = 0.45;
      ctx.strokeStyle = app.color;
      ctx.lineWidth = app.thickness;
      ctx.lineCap = 'round';
      ctx.setLineDash([6, 4]);
      ctx.beginPath();
      ctx.moveTo(anchor.x, anchor.y);
      ctx.lineTo(p.x, p.y);
      ctx.stroke();
      ctx.setLineDash([]);
    }
    ctx.globalAlpha = 0.6;
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
