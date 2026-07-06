import { hexToRgba, parseHex } from '../utils.js';
import { core } from './stencilCore.js';
import { applyContourRGBA } from './contourFilter.js';
// ── Renderer: image filter + line/point drawing ─────────────────
// canvas setLineDash patterns for the two non-solid line styles.
const DASH_PATTERN = [10, 5];
const DOT_PATTERN = [2, 5];

export class Renderer {
  // One-slot cache for the expensive pixel-transform filters ('contour' Sobel and
  // the 'custom' duotone): an offscreen canvas holding the filtered image, keyed on
  // the exact (image, filter, tint) identity that produced it. Object identity is a
  // sufficient key because every pixel change (load/crop/rotate/replace) routes
  // through rebuildCroppedImage(), which swaps app.image for a fresh canvas. redraw()
  // fires on every hover/drag/zoom repaint, so without this the full getImageData →
  // convolution/tint → putImageData pipeline would rerun per mousemove (mirrors the
  // desktop's filteredImage_/filterDirty_ cache in canvasWidget.cpp).
  #filtered = null;   // { image, filter, color, canvas }

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
      // Sobel edge detection needs the pixel neighborhood, so no CSS filter exists
      // for it: blit the cached filtered copy (rebuilt only when the image changes).
      ctx.filter = 'none';
      ctx.drawImage(this.#filteredCanvas('contour', null), 0, 0);
    } else if (this.app.imageFilter === 'custom') {
      const color = this.app.filterColor || '#7c3aed';
      ctx.filter = 'none';
      ctx.drawImage(this.#filteredCanvas('custom', color), 0, 0);
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
      // Show points only (no connecting lines). Iterate committed lines, then
      // the in-progress line separately — avoids cloning the lines array into
      // a combined list every frame.
      const drawPts = (line, li, sel) => {
        const ms = line.markerSize ?? this.app.markerSize;
        line.points.forEach((p, pi) => {
          const hs = this.#pointHighlightState(li, pi);
          this.drawPoint(p, line.color, ms, sel, hs);
        });
      };
      this.app.lines.forEach((line, i) => drawPts(line, i, i === this.app.selectedLineIdx));
      if (this.app.currentLine) drawPts(this.app.currentLine, -1, false);
    }

    // Hold-to-draw: faded ghost line from the current anchor to the held cursor.
    if (this.app.holdPreview) this.drawHoldPreview();
  }

  // Translucent dashed segment from the stroke's anchor point to the live cursor,
  // plus a ghost marker at the cursor — shows where the next point would land
  // during a hold-to-draw gesture. Purely transient; never committed.
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
    ctx.fillStyle = app.color;
    ctx.beginPath();
    ctx.arc(p.x, p.y, app.markerSize, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
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
      this.app.ctx.setLineDash(DASH_PATTERN);
    } else if (line.style === 'dotted') {
      this.app.ctx.setLineDash(DOT_PATTERN);
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

  // Return an image-sized offscreen canvas with `filter` ('contour' | 'custom')
  // applied to the current image, rebuilding it only when the (image, filter, tint)
  // key changed since the last call. `color` is the tint hex for 'custom', null for
  // 'contour' (so a tint change invalidates but a contour redraw never does).
  #filteredCanvas(filter, color) {
    const image = this.app.image;
    const c = this.#filtered;
    if (c && c.image === image && c.filter === filter && c.color === color) return c.canvas;
    const canvas = document.createElement('canvas');
    canvas.width = image.width;
    canvas.height = image.height;
    const fctx = canvas.getContext('2d');
    if (filter === 'contour') {
      fctx.drawImage(image, 0, 0);
      this.#applyContourFilter(fctx);
    } else {
      const wasmFilter = core.op('applyFilterRGBA');
      if (wasmFilter) {
        // Shared C++ core (wasm): grayscale + duotone tint in one pass over the
        // original pixels — no CSS grayscale prepass needed.
        fctx.drawImage(image, 0, 0);
        this.#applyWasmFilter(fctx, wasmFilter, 'custom', color);
      } else {
        fctx.filter = 'grayscale(100%)';
        fctx.drawImage(image, 0, 0);
        fctx.filter = 'none';
        this.#applyTintFilter(fctx, color);
      }
    }
    this.#filtered = { image, filter, color, canvas };
    return canvas;
  }

  // Run the shared C++ core (wasm) filter over the canvas pixels in place, using
  // the resolved core.op('applyFilterRGBA') fn passed by the caller. mode
  // 'custom' computes grayscale + duotone tint in a single pass.
  #applyWasmFilter(ctx, filter, mode, hexColor) {
    const { r, g, b } = parseHex(hexColor);
    const w = ctx.canvas.width;
    const h = ctx.canvas.height;
    const imageData = ctx.getImageData(0, 0, w, h);
    filter(mode, imageData.data, w * h, r, g, b);
    ctx.putImageData(imageData, 0, 0);
  }

  // Contour (Sobel edges, dark on white) over the drawn original, in place: the
  // shared C++ core (wasm) when loaded, else the byte-identical JS reference in
  // contourFilter.js. Unlike the per-pixel filters this one needs width/height.
  #applyContourFilter(ctx) {
    const w = ctx.canvas.width;
    const h = ctx.canvas.height;
    const imageData = ctx.getImageData(0, 0, w, h);
    const fn = core.op('applyContourRGBA');
    if (fn) fn(imageData.data, w, h);
    else applyContourRGBA(imageData.data, w, h);
    ctx.putImageData(imageData, 0, 0);
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
