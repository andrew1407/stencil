// Ported from js/core/renderer.js's filter chain; the pixel maths itself lives in the
// shared C++ core (core/raster/imageFilter) with contourFilter.js as the JS reference.
import { parseHex } from '../utils.js';
import { core } from './stencilCore.js';
import { applyContourRGBA } from './contourFilter.js';

// Run the shared C++ core (wasm) filter over the canvas pixels in place, using the
// resolved core.op('applyFilterRGBA') fn passed by the caller. mode 'custom' computes
// grayscale + duotone tint in a single pass.
const applyWasmFilter = (ctx, filter, mode, hexColor) => {
  const { r, g, b } = parseHex(hexColor);
  const w = ctx.canvas.width;
  const h = ctx.canvas.height;
  const imageData = ctx.getImageData(0, 0, w, h);
  filter(mode, imageData.data, w * h, r, g, b);
  ctx.putImageData(imageData, 0, 0);
};

// Contour (Sobel edges, dark on white) over the drawn original, in place: the
// shared C++ core (wasm) when loaded, else the byte-identical JS reference in
// contourFilter.js. Unlike the per-pixel filters this one needs width/height.
const applyContourFilter = (ctx) => {
  const w = ctx.canvas.width;
  const h = ctx.canvas.height;
  const imageData = ctx.getImageData(0, 0, w, h);
  const fn = core.op('applyContourRGBA');
  if (fn) fn(imageData.data, w, h);
  else applyContourRGBA(imageData.data, w, h);
  ctx.putImageData(imageData, 0, 0);
};

// Duotone tint: dark pixels → chosen color, light pixels → white
const applyTintFilter = (ctx, hexColor) => {
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
};

// One-slot cache for the pixel-transform filters ('contour' Sobel, 'custom' duotone),
// keyed on (image, filter, tint) identity — valid because every pixel change swaps
// app.image via rebuildCroppedImage(). Without it the getImageData → convolution →
// putImageData pipeline reruns per mousemove (mirrors canvasWidget.cpp filteredImage_).
export class ImageFilterCanvas {
  #filtered = null;   // { image, filter, color, canvas }

  // Image-sized offscreen canvas with `filter` ('contour' | 'custom') applied, rebuilt
  // only when the (image, filter, tint) key changed. `color` is the tint hex for
  // 'custom', null for 'contour' (a tint change invalidates; a contour redraw never does).
  canvasFor(image, filter, color) {
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
      applyContourFilter(fctx);
    } else {
      const wasmFilter = core.op('applyFilterRGBA');
      if (wasmFilter) {
        // Shared C++ core (wasm): grayscale + duotone tint in one pass over the
        // original pixels — no CSS grayscale prepass needed.
        fctx.drawImage(image, 0, 0);
        applyWasmFilter(fctx, wasmFilter, 'custom', color);
      } else {
        fctx.filter = 'grayscale(100%)';
        fctx.drawImage(image, 0, 0);
        fctx.filter = 'none';
        applyTintFilter(fctx, color);
      }
    }
    this.#filtered = { image, filter, color, canvas };
    return canvas;
  }
}
