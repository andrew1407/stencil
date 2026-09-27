// The RGBA pass of the pixel filters, DOM-free, so filterCanvas.js on the main thread and
// worker/imageWorker.js run one implementation. `op(name)` is the loaded wasm op or null;
// contourFilter.js is the byte-identical JS twin of the core contour.
import { applyContourRGBA } from './contourFilter.js';

// How the input was read: CONTOUR and CUSTOM over the plain pixels, TINT over pixels the
// canvas already grayscaled (the duotone without wasm).
export const FILTER_RECIPE = Object.freeze({ CONTOUR: 'contour', CUSTOM: 'custom', TINT: 'tint' });

// Duotone: dark pixels → the chosen colour, light pixels → white.
export const tintRGBA = (d, r, g, b) => {
  for (let i = 0; i < d.length; i += 4) {
    const t = d[i] / 255;
    d[i] = Math.round(r + (255 - r) * t);
    d[i+1] = Math.round(g + (255 - g) * t);
    d[i+2] = Math.round(b + (255 - b) * t);
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
