// The RGBA pass of the pixel filters, DOM-free, so filterCanvas.js on the main thread and
// worker/imageWorker.js run one implementation. `op(name)` is the loaded wasm op or null;
// contourFilter.js is the byte-identical JS twin of the core contour.
import { applyContourRGBA } from './contourFilter.js';

// How the input was read: CONTOUR and CUSTOM over the plain pixels, TINT over pixels the
// canvas already grayscaled (the duotone without wasm).
export const FILTER_RECIPE = Object.freeze({ CONTOUR: 'contour', CUSTOM: 'custom', TINT: 'tint' });

// Duotone: dark pixels → the chosen colour, light pixels → white. The input is gray, so the 256
// levels are tinted once (imageFilter.cpp's CUSTOM table); clamped, as a store into `d` would be.
export const tintRGBA = (d, r, g, b) => {
  const lut = new Uint8ClampedArray(768);
  for (let l = 0; l < 256; l++) {
    const t = l / 255;
    lut[l * 3] = Math.round(r + (255 - r) * t);
    lut[l * 3 + 1] = Math.round(g + (255 - g) * t);
    lut[l * 3 + 2] = Math.round(b + (255 - b) * t);
  }
  for (let i = 0; i < d.length; i += 4) {
    const k = d[i] * 3;
    d[i] = lut[k];
    d[i+1] = lut[k + 1];
    d[i+2] = lut[k + 2];
  }
};

// In place; false when CUSTOM needs the wasm op and `op` has none.
export const filterPixels = (recipe, data, width, height, { r, g, b }, op) => {
  if (recipe === FILTER_RECIPE.CONTOUR) {
    (op('applyContourRGBA') || applyContourRGBA)(data, width, height);
    return true;
  }
  if (recipe === FILTER_RECIPE.CUSTOM) {
    const filter = op('applyFilterRGBA');
    if (!filter) return false;
    filter('custom', data, width * height, r, g, b);
    return true;
  }
  tintRGBA(data, r, g, b);
  return true;
};

// The same pass, answering the array that holds the result: `data` itself, or a view of the wasm
// heap the core wrote into (no copy back; read it before the next core call). Null as filterPixels' false.
export const filteredPixels = (recipe, data, width, height, { r, g, b }, op) => {
  if (recipe === FILTER_RECIPE.CONTOUR) {
    const wasm = op('applyContourRGBA');
    if (!wasm) { applyContourRGBA(data, width, height); return data; }
    return wasm(data, width, height, true);
  }
  if (recipe === FILTER_RECIPE.CUSTOM) {
    const filter = op('applyFilterRGBA');
    return filter ? filter('custom', data, width * height, r, g, b, true) : null;
  }
  tintRGBA(data, r, g, b);
  return data;
};
