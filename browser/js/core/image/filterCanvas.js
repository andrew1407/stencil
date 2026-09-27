// The pixel maths lives in the C++ core (core/raster/imageFilter); filterPixels.js runs it here and
// in the image worker, over the wasm op when loaded, else its JS twin.
import { parseHex } from '../../utils.js';
import { core } from '../abi/stencilCore.js';
import { FILTER_RECIPE, filterPixels } from './filterPixels.js';

const op = (name) => core.op(name);
const rgbOf = (hex) => (hex ? parseHex(hex) : { r: 0, g: 0, b: 0 });

// Never in the document: an OffscreenCanvas where there is one.
const makeCanvas = (w, h) => {
  const canvas = typeof OffscreenCanvas === 'function'
    ? new OffscreenCanvas(w, h)
    : document.createElement('canvas');
  canvas.width = w;
  canvas.height = h;
  return canvas;
};

const cssFilter = (css) => (fctx, image) => {
  fctx.filter = css;
  fctx.drawImage(image, 0, 0);
  fctx.filter = 'none';
};
const plainDraw = (fctx, image) => { fctx.drawImage(image, 0, 0); };

// A pixel filter's RGBA recipe and the canvas pass that reads its input; null for a CSS one.
// The duotone is wasm grayscale + tint in one pass, else the CSS grayscale prepass and the JS tint.
const pixelRecipe = (filter) => {
  if (filter === 'contour') return { recipe: FILTER_RECIPE.CONTOUR, draw: plainDraw };
  if (filter !== 'custom') return null;
  return op('applyFilterRGBA') ? { recipe: FILTER_RECIPE.CUSTOM, draw: plainDraw }
    : { recipe: FILTER_RECIPE.TINT, draw: cssFilter('grayscale(100%)') };
};

export const isPixelFilter = (filter) => filter === 'contour' || filter === 'custom';

// The input of a pixel filter's RGBA pass, read the way the painter below reads it.
export const readFilterInput = (image, filter, color) => {
  const p = pixelRecipe(filter);
  if (!p) return null;
  const fctx = makeCanvas(image.width, image.height).getContext('2d');
  p.draw(fctx, image);
  return { recipe: p.recipe, pixels: fctx.getImageData(0, 0, image.width, image.height), rgb: rgbOf(color) };
};

const paintPixels = (filter) => (fctx, image, color) => {
  const p = pixelRecipe(filter);
  const w = fctx.canvas.width;
  const h = fctx.canvas.height;
  p.draw(fctx, image);
  const imageData = fctx.getImageData(0, 0, w, h);
  filterPixels(p.recipe, imageData.data, w, h, rgbOf(color), op);
  fctx.putImageData(imageData, 0, 0);
};

// Strategy per imageFilter: how its copy is painted. 'none' (or unknown) has none — the frame
// blits the bitmap itself.
const PAINTERS = Object.freeze({
  bw: cssFilter('grayscale(100%)'),
  sepia: cssFilter('sepia(100%)'),
  invert: cssFilter('invert(100%)'),
  contour: paintPixels('contour'),
  custom: paintPixels('custom'),
});

// The colour `filter` paints a solid `hex` fill in: the middle of a 3×3 tile, where a neighbourhood
// filter sees no edge, as it sees none inside a whole fill. The hex itself where that cannot be read.
export const filteredFill = (hex, filter, tint) => {
  const paint = PAINTERS[filter];
  if (!paint) return hex;
  try {
    const tile = makeCanvas(3, 3);
    const tctx = tile.getContext('2d');
    tctx.fillStyle = hex;
    tctx.fillRect(0, 0, 3, 3);
    const out = makeCanvas(3, 3).getContext('2d');
    paint(out, tile, tint);
    const [r, g, b] = out.getImageData(1, 1, 1, 1).data;
    return `rgb(${r}, ${g}, ${b})`;
  } catch { return hex; }
};

// One-slot cache keyed on (image, filter, tint) identity — valid because every pixel
// change swaps app.image via rebuildCroppedImage(). Desktop: CanvasWidget filteredImage_.
export class ImageFilterCanvas {
  #filtered = null;

  cached(image, filter, color) {
    const c = this.#filtered;
    return c && c.image === image && c.filter === filter && c.color === color ? c.canvas : null;
  }

// `color` is the tint hex for 'custom', null otherwise; null back when the filter paints nothing.
  canvasFor(image, filter, color) {
    const paint = PAINTERS[filter];
    if (!paint) return null;
    const hit = this.cached(image, filter, color);
    if (hit) return hit;
    const canvas = makeCanvas(image.width, image.height);
    paint(canvas.getContext('2d'), image, color);
    this.#filtered = { image, filter, color, canvas };
    return canvas;
  }

  // A copy the image worker painted for exactly this key.
  adopt(image, filter, color, layer) {
    this.#filtered = { image, filter, color, canvas: layer };
  }
}
