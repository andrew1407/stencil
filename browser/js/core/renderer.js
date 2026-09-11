import { hexToRgba, parseHex } from '../utils.js';
import { core } from './stencilCore.js';
import { applyContourRGBA } from './contourFilter.js';
// ── Renderer: image filter + line/point drawing ─────────────────
// canvas setLineDash patterns for the two non-solid line styles.
const DASH_PATTERN = [10, 5];
const DOT_PATTERN = [2, 5];

// hexToRgba builds a fresh string per call, and the glow/ring colours are asked for once
// per selected line and once per highlighted point EVERY frame. The (colour, alpha) pairs
// are few and fixed, so memoize them; the cap keeps a runaway accent sweep bounded.
const RGBA = new Map();
const rgba = (hex, a) => {
  const key = hex + a;
  let v = RGBA.get(key);
  if (v === undefined) { if (RGBA.size > 64) RGBA.clear(); RGBA.set(key, v = hexToRgba(hex, a)); }
  return v;
};

// The colour a line's points draw in: its own pointColor when set, else its stroke
// colour. JS twin of core's pointColorOr (core/models.hpp) — keep the two identical;
// "unset" must include '' (how the field serialises when a line has no point colour).
export const pointColorOf = (line) => (line.pointColor ? line.pointColor : line.color);

export class Renderer {
  // One-slot cache for the pixel-transform filters ('contour' Sobel, 'custom' duotone),
  // keyed on (image, filter, tint) identity — valid because every pixel change swaps
  // app.image via rebuildCroppedImage(). Without it the getImageData → convolution →
  // putImageData pipeline reruns per mousemove (mirrors canvasWidget.cpp filteredImage_).
  #filtered = null;   // { image, filter, color, canvas }
  // Set per-frame in redraw(): true suppresses selection glow + hover/focus rings (the
  // read-only compare views draw a clean picture).
  #suppressHighlight = false;

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

  // The compare mode actually shown this frame: an Alt+Shift+O hold forces 'original'
  // regardless of the selected mode.
  effectiveCompareMode() {
    return this.app.compareHoldOriginal ? 'original' : (this.app.compareMode || 'none');
  }

  redraw() {
    if (!this.app.image) return;

    this.app.ctx.clearRect(0, 0, this.app.canvas.width, this.app.canvas.height);

    const compare = this.effectiveCompareMode();
    if (compare === 'original') {
      // Original view: the cropped + rotated original alone — no filter, no annotations.
      this.app.ctx.filter = 'none';
      this.app.ctx.drawImage(this.app.image, 0, 0);
      return;
    }

    this.drawImageWithFilter(this.app.ctx);

    // In a split compare view the edit side is read-only: draw lines/points but with no
    // selection glow or hover/focus rings (a clean picture to compare against).
    const ro = compare !== 'none';
    this.#suppressHighlight = ro;

    if (this.app.showLines) {
      this.app.lines.forEach((line, i) => this.drawLine(line, ro ? false : this.app.isLineSelected(i), i));
      if (this.app.currentLine && this.app.currentLine.points.length > 0)
        this.drawLine(this.app.currentLine, false, -1);
    } else if (this.app.showPoints) {
      // Points-only view: iterate committed lines, then the in-progress line separately —
      // avoids cloning the lines array into a combined list every frame.
      const drawPts = (line, li, sel) => {
        const ms = line.pointSize ?? this.app.pointSize;
        const fx = this.app.strokeFx;
        const pts = fx.pointsOf(line);
        pts.forEach((p, pi) => {
          const hs = this.#pointHighlightState(li, pi);
          this.drawPoint(p, pointColorOf(line), ms * fx.scaleAt(line.points[pi]), sel, hs);
        });
        fx.paintOver(this.app.ctx, line, pts);
      };
      this.app.lines.forEach((line, i) => drawPts(line, i, ro ? false : this.app.isLineSelected(i)));
      if (this.app.currentLine) drawPts(this.app.currentLine, -1, false);
    }

    // Hold-to-draw: faded ghost line from the current anchor to the held cursor.
    if (this.app.holdPreview) this.drawHoldPreview();

    // Split compare: paint the untouched original over the original-side region (covering
    // the edited pixels + annotations there), then draw the movable divider on top.
    if (compare === 'vertical' || compare === 'horizontal') this.drawCompareSplit(compare);
  }

  // Overlay the cropped+rotated original onto the "original" half of a split compare view
  // (left half for 'vertical', top half for 'horizontal') and draw the draggable divider.
  // `withDivider: false` (the export path's clean split, e.g. Ctrl+C during a split
  // compare) skips the divider bar/knob — just the two image halves, no UI chrome.
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

    // Divider drawn in image space but kept a constant on-screen thickness by dividing by
    // the current zoom, so it neither vanishes when zoomed out nor bloats when zoomed in.
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

  // Translucent dashed segment from the stroke's anchor point to the live cursor,
  // plus a ghost point at the cursor — shows where the next point would land
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
    ctx.fillStyle = app.pointColor || app.color;
    ctx.beginPath();
    ctx.arc(p.x, p.y, app.pointSize, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
  }

  drawLine(line, isSelected = false, lineIdx = -99) {
    // Points in flight (a just-added vertex travelling to where it was put) are drawn
    // where they are RIGHT NOW; everything below — fill, glows, stroke, points — reads
    // this array, so the segments hanging off a moving vertex follow it for free.
    const fx = this.app.strokeFx;
    const pts = fx.pointsOf(line);
    if (line.points.length < 2) {
      if (line.points.length === 1 && this.app.showPoints) {
        const hs = this.#pointHighlightState(lineIdx, 0);
        const ps = (line.pointSize ?? this.app.pointSize) * fx.scaleAt(line.points[0]);
        this.drawPoint(pts[0], pointColorOf(line), ps, isSelected, hs);
        fx.paintOver(this.app.ctx, line, pts);
      }
      return;
    }

    // Locked-area fill (closed polygon) — drawn beneath stroke & glow
    if (line.locked && line.points.length >= 3 && line.fillColor && line.fillColor !== 'transparent') {
      this.app.ctx.save();
      this.app.ctx.fillStyle = line.fillColor;
      this.app.ctx.beginPath();
      this.app.ctx.moveTo(pts[0].x, pts[0].y);
      for (let i = 1; i < line.points.length; i++)
        this.app.ctx.lineTo(pts[i].x, pts[i].y);
      this.app.ctx.closePath();
      this.app.ctx.fill();
      this.app.ctx.restore();
    }

    // One glow pass, drawn beneath the stroke: the selection's, or — on an unselected line —
    // the thinner, fainter one a Lines-list row hover paints, so the two stay distinguishable.
    const glow = isSelected ? { color: this.app.selGlowColor, alpha: 0.6, pad: 8 }
      : (!this.#suppressHighlight && lineIdx >= 0 && lineIdx === (this.app.listHoverLineIdx ?? -1)
        && line.points.length >= 2 ? { color: this.app.hoverRingColor, alpha: 0.35, pad: 6 } : null);
    if (glow) {
      this.app.ctx.save();
      this.app.ctx.strokeStyle = rgba(glow.color, glow.alpha);
      this.app.ctx.lineWidth = line.thickness + glow.pad;
      this.app.ctx.lineCap = 'round';
      this.app.ctx.lineJoin = 'round';
      this.app.ctx.setLineDash([]);
      this.app.ctx.beginPath();
      this.app.ctx.moveTo(pts[0].x, pts[0].y);
      for (let i = 1; i < line.points.length; i++)
        this.app.ctx.lineTo(pts[i].x, pts[i].y);
      if (line.locked) this.app.ctx.closePath();
      this.app.ctx.stroke();
      this.app.ctx.restore();
    }

    // The heat the flying vertex drags behind it, under the real stroke.
    fx.paintUnder(this.app.ctx, line, pts);

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
    this.app.ctx.moveTo(pts[0].x, pts[0].y);

    for (let i = 1; i < line.points.length; i++)
      this.app.ctx.lineTo(pts[i].x, pts[i].y);
    if (line.locked) this.app.ctx.closePath();

    this.app.ctx.stroke();
    this.app.ctx.setLineDash([]);

    if (this.app.showPoints) {
      pts.forEach((point, pi) => {
        const hs = this.#pointHighlightState(lineIdx, pi);
        const ps = (line.pointSize ?? this.app.pointSize) * fx.scaleAt(line.points[pi]);
        this.drawPoint(point, pointColorOf(line), ps, isSelected, hs);
      });
    }
    // The spark riding a flying vertex and the ring its landing pushes out, over
    // everything else this line drew.
    fx.paintOver(this.app.ctx, line, pts);
  }

  // highlightState: 0 = none, 1 = hover (subtle ring), 2 = focused (bold ring + shadow)
  drawPoint(point, color, pointSize = 4, isSelected = false, highlightState = 0) {
    const r = pointSize;
    if (isSelected) {
      this.app.ctx.fillStyle = rgba(this.app.selGlowColor, 0.5);
      this.app.ctx.beginPath();
      this.app.ctx.arc(point.x, point.y, r + 4, 0, Math.PI * 2);
      this.app.ctx.fill();
    }
    if (highlightState === 1) {
      // Hover — thin translucent ring
      this.app.ctx.save();
      this.app.ctx.strokeStyle = rgba(this.app.hoverRingColor, 0.55);
      this.app.ctx.lineWidth = 1.8;
      this.app.ctx.beginPath();
      this.app.ctx.arc(point.x, point.y, r + 4, 0, Math.PI * 2);
      this.app.ctx.stroke();
      this.app.ctx.restore();
    } else if (highlightState === 2) {
      // Focused/click — bold ring with glow shadow
      this.app.ctx.save();
      this.app.ctx.shadowColor = rgba(this.app.focusRingColor, 0.9);
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

  // Image-sized offscreen canvas with `filter` ('contour' | 'custom') applied, rebuilt
  // only when the (image, filter, tint) key changed. `color` is the tint hex for
  // 'custom', null for 'contour' (a tint change invalidates; a contour redraw never does).
  #filteredCanvas(filter, color) {
    const image = this.app.image;
    const c = this.#filtered;
    if (c && c.image === image && c.filter === filter && c.color === color) return c.canvas;
    // Never in the document — an OffscreenCanvas where there is one, so the pixels do not
    // cost a DOM node (and the raster can live off the main thread's element bookkeeping).
    const canvas = typeof OffscreenCanvas === 'function'
      ? new OffscreenCanvas(image.width, image.height)
      : document.createElement('canvas');
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
    if (this.#suppressHighlight) return 0;
    if (lineIdx === this.app.coordLineIdx && ptIdx === this.app.focusedPtIdx) return 2;
    if (this.app.hoverPt && this.app.hoverPt.lineIdx === lineIdx && this.app.hoverPt.ptIdx === ptIdx) return 1;
    if (lineIdx === this.app.coordLineIdx && ptIdx === this.app.hoveredPtIdx) return 1;
    // Lines-list row hover rings the whole line's points (visible in points-only view too).
    if (lineIdx >= 0 && lineIdx === (this.app.listHoverLineIdx ?? -1)) return 1;
    return 0;
  }
}
