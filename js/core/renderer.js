import { hexToRgba, parseHex } from '../utils.js';
// ── Renderer: image filter + line/point drawing ─────────────────
export class Renderer {
  constructor(app) {
    this.app = app;
  }

  // Draw image into ctx with the currently selected filter applied
  drawImageWithFilter(ctx) {
    if (this.app.imageFilter === 'bw') {
      ctx.filter = 'grayscale(100%)';
      ctx.drawImage(this.app.image, 0, 0);
      ctx.filter = 'none';
    } else if (this.app.imageFilter === 'sepia') {
      ctx.filter = 'sepia(100%)';
      ctx.drawImage(this.app.image, 0, 0);
      ctx.filter = 'none';
    } else if (this.app.imageFilter === 'custom') {
      ctx.filter = 'grayscale(100%)';
      ctx.drawImage(this.app.image, 0, 0);
      ctx.filter = 'none';
      this.#applyTintFilter(ctx, this.app.filterColor || '#7c3aed');
    } else {
      ctx.drawImage(this.app.image, 0, 0);
    }
  }

  redraw() {
    if (!this.app.image) return;

    this.app.ctx.clearRect(0, 0, this.app.canvas.width, this.app.canvas.height);
    this.drawImageWithFilter(this.app.ctx);

    if (this.app.showLines) {
      this.app.lines.forEach((line, i) => this.drawLine(line, i === this.app.selectedLineIdx, i));
      if (this.app.currentLine && this.app.currentLine.points.length > 0)
        this.drawLine(this.app.currentLine, false, -1);
    } else if (this.app.showPoints) {
      // Show points only (no connecting lines)
      const allLines = this.app.currentLine
        ? [...this.app.lines, this.app.currentLine]
        : this.app.lines;
      allLines.forEach((line, i) => {
        const sel = i === this.app.selectedLineIdx;
        const li = i < this.app.lines.length ? i : -1;
        line.points.forEach((p, pi) => {
          const hs = this.#pointHighlightState(li, pi);
          this.drawPoint(p, line.color, line.markerSize ?? this.app.markerSize, sel, hs);
        });
      });
    }
  }

  drawLine(line, isSelected = false, lineIdx = -99) {
    if (line.points.length < 2) {
      if (line.points.length === 1 && this.app.showPoints) {
        const hs = this.#pointHighlightState(lineIdx, 0);
        this.drawPoint(line.points[0], line.color, line.markerSize ?? this.app.markerSize, isSelected, hs);
      }
      return;
    }

    // Locked-area fill (closed polygon) — drawn beneath stroke & glow
    if (line.locked && line.points.length >= 3 && line.fillColor && line.fillColor !== 'transparent') {
      this.app.ctx.save();
      this.app.ctx.fillStyle = line.fillColor;
      this.app.ctx.beginPath();
      this.app.ctx.moveTo(line.points[0].x, line.points[0].y);
      for (let i = 1; i < line.points.length; i++)
        this.app.ctx.lineTo(line.points[i].x, line.points[i].y);
      this.app.ctx.closePath();
      this.app.ctx.fill();
      this.app.ctx.restore();
    }

    // Selection highlight glow
    if (isSelected) {
      this.app.ctx.save();
      this.app.ctx.strokeStyle = hexToRgba(this.app.selGlowColor, 0.6);
      this.app.ctx.lineWidth = line.thickness + 8;
      this.app.ctx.lineCap = 'round';
      this.app.ctx.lineJoin = 'round';
      this.app.ctx.setLineDash([]);
      this.app.ctx.beginPath();
      this.app.ctx.moveTo(line.points[0].x, line.points[0].y);
      for (let i = 1; i < line.points.length; i++)
        this.app.ctx.lineTo(line.points[i].x, line.points[i].y);
      if (line.locked) this.app.ctx.closePath();
      this.app.ctx.stroke();
      this.app.ctx.restore();
    }

    this.app.ctx.strokeStyle = line.color;
    this.app.ctx.lineWidth = line.thickness;
    this.app.ctx.lineCap = 'round';
    this.app.ctx.lineJoin = 'round';

    if (line.style === 'dashed') {
      this.app.ctx.setLineDash([10, 5]);
    } else if (line.style === 'dotted') {
      this.app.ctx.setLineDash([2, 5]);
    } else {
      this.app.ctx.setLineDash([]);
    }

    this.app.ctx.beginPath();
    this.app.ctx.moveTo(line.points[0].x, line.points[0].y);

    for (let i = 1; i < line.points.length; i++)
      this.app.ctx.lineTo(line.points[i].x, line.points[i].y);
    if (line.locked) this.app.ctx.closePath();

    this.app.ctx.stroke();
    this.app.ctx.setLineDash([]);

    if (this.app.showPoints) {
      line.points.forEach((point, pi) => {
        const hs = this.#pointHighlightState(lineIdx, pi);
        this.drawPoint(point, line.color, line.markerSize ?? this.app.markerSize, isSelected, hs);
      });
    }
  }

  // highlightState: 0 = none, 1 = hover (subtle ring), 2 = focused (bold ring + shadow)
  drawPoint(point, color, markerSize = 4, isSelected = false, highlightState = 0) {
    const r = markerSize;
    if (isSelected) {
      this.app.ctx.fillStyle = hexToRgba(this.app.selGlowColor, 0.5);
      this.app.ctx.beginPath();
      this.app.ctx.arc(point.x, point.y, r + 4, 0, Math.PI * 2);
      this.app.ctx.fill();
    }
    if (highlightState === 1) {
      // Hover — thin translucent ring
      this.app.ctx.save();
      this.app.ctx.strokeStyle = hexToRgba(this.app.hoverRingColor, 0.55);
      this.app.ctx.lineWidth = 1.8;
      this.app.ctx.beginPath();
      this.app.ctx.arc(point.x, point.y, r + 4, 0, Math.PI * 2);
      this.app.ctx.stroke();
      this.app.ctx.restore();
    } else if (highlightState === 2) {
      // Focused/click — bold ring with glow shadow
      this.app.ctx.save();
      this.app.ctx.shadowColor = hexToRgba(this.app.focusRingColor, 0.9);
      this.app.ctx.shadowBlur = 12;
      this.app.ctx.strokeStyle = this.app.focusRingColor;
      this.app.ctx.lineWidth = 3;
      this.app.ctx.beginPath();
      this.app.ctx.arc(point.x, point.y, r + 6, 0, Math.PI * 2);
      this.app.ctx.stroke();
      this.app.ctx.restore();
    }
    this.app.ctx.fillStyle = color;
    this.app.ctx.beginPath();
    this.app.ctx.arc(point.x, point.y, r, 0, Math.PI * 2);
    this.app.ctx.fill();
    this.app.ctx.strokeStyle = '#000';
    this.app.ctx.lineWidth = 1;
    this.app.ctx.stroke();
  }

  // Duotone tint: dark pixels → chosen color, light pixels → white
  #applyTintFilter(ctx, hexColor) {
    const { r, g, b } = parseHex(hexColor);
    const w = ctx.canvas.width;
    const h = ctx.canvas.height;
    const imageData = ctx.getImageData(0, 0, w, h);
    const d = imageData.data;
    for (let i = 0; i < d.length; i += 4) {
      // Luminance from current (already grayscale) pixel
      const t = d[i] / 255; // 0 = dark → color, 1 = light → white
      d[i] = Math.round(r + (255 - r) * t);
      d[i+1] = Math.round(g + (255 - g) * t);
      d[i+2] = Math.round(b + (255 - b) * t);
      // alpha unchanged
    }
    ctx.putImageData(imageData, 0, 0);
  }

  // Decide a point's highlight state (0 none, 1 hover, 2 focused) regardless
  // of whether its line is the one shown in the coord table.
  #pointHighlightState(lineIdx, ptIdx) {
    if (lineIdx === this.app.coordLineIdx && ptIdx === this.app.focusedPtIdx) return 2;
    if (this.app.hoverPt && this.app.hoverPt.lineIdx === lineIdx && this.app.hoverPt.ptIdx === ptIdx) return 1;
    if (lineIdx === this.app.coordLineIdx && ptIdx === this.app.hoveredPtIdx) return 1;
    return 0;
  }
}
