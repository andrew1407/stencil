// The pixel maths lives in the C++ core (core/raster/imageFilter); contourFilter.js is the JS reference.
import { parseHex } from '../../utils.js';
import { core } from '../abi/stencilCore.js';
import { applyContourRGBA } from './contourFilter.js';

// `filter` is the resolved core.op('applyFilterRGBA'); mode 'custom' does grayscale + tint in one pass.
const applyWasmFilter = (ctx, filter, mode, hexColor) => {
  const { r, g, b } = parseHex(hexColor);
  const w = ctx.canvas.width;
  const h = ctx.canvas.height;
  const imageData = ctx.getImageData(0, 0, w, h);
  filter(mode, imageData.data, w * h, r, g, b);
  ctx.putImageData(imageData, 0, 0);
};

// Sobel edges, dark on white: wasm when loaded, else the byte-identical contourFilter.js.
const applyContourFilter = (ctx) => {
  const w = ctx.canvas.width;
  const h = ctx.canvas.height;
  const imageData = ctx.getImageData(0, 0, w, h);
  const fn = core.op('applyContourRGBA');
  if (fn) fn(imageData.data, w, h);
  else applyContourRGBA(imageData.data, w, h);
  ctx.putImageData(imageData, 0, 0);
};

// Duotone: dark pixels → chosen color, light pixels → white.
const applyTintFilter = (ctx, hexColor) => {
  const { r, g, b } = parseHex(hexColor);
  const w = ctx.canvas.width;
  const h = ctx.canvas.height;
  const imageData = ctx.getImageData(0, 0, w, h);
  const d = imageData.data;
  for (let i = 0; i < d.length; i += 4) {
    const t = d[i] / 255;
    d[i] = Math.round(r + (255 - r) * t);
    d[i+1] = Math.round(g + (255 - g) * t);
    d[i+2] = Math.round(b + (255 - b) * t);
  }
  ctx.putImageData(imageData, 0, 0);
};

// One-slot cache keyed on (image, filter, tint) identity — valid because every pixel
// change swaps app.image via rebuildCroppedImage(). Desktop: CanvasWidget filteredImage_.
export class ImageFilterCanvas {
  #filtered = null;

// `color` is the tint hex for 'custom', null for 'contour'.
  canvasFor(image, filter, color) {
    const c = this.#filtered;
    if (c && c.image === image && c.filter === filter && c.color === color) return c.canvas;
// Never in the document: an OffscreenCanvas where there is one.
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
// One pass over the original pixels — no CSS grayscale prepass.
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
