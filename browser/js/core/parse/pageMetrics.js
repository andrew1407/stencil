import { core } from '../abi/stencilCore.js';
import constants from '../../config/constants.json' with { type: 'json' };
const { PAGE_SIZES } = constants;

// A wasm-parity surface (core/page/pageMetrics): the core owns the named-size table and the
// pixel→cm scaling when loaded; the JS is the reference and the fallback.

const computePageDimensions = (app) => {
  const fn = core.op('pageDimensions');
  if (fn) {
    return fn(app.pageSize, app.canvas.width, app.canvas.height,
      app.customPageWidth, app.customPageHeight);
  }
  if (app.pageSize === 'custom') return { width: app.customPageWidth, height: app.customPageHeight };
  const ps = { ...PAGE_SIZES[app.pageSize] };
  if (app.canvas.width > app.canvas.height) return { width: ps.height, height: ps.width };
  return ps;
};

// Hover asks on every move; the answer changes only with the page or the canvas size.
const dimsCache = new WeakMap();

export const getPageDimensions = (app) => {
  const key = `${app.pageSize}|${app.canvas.width}|${app.canvas.height}|${app.customPageWidth}|${app.customPageHeight}`;
  let hit = dimsCache.get(app);
  if (!hit || hit.key !== key) dimsCache.set(app, hit = { key, dims: computePageDimensions(app) });
  return { ...hit.dims };
};

// What the f(x,y) names resolve to: the page in cm, the image in pixels — absent until one
// is open, so IMAGE_WIDTH / IMAGE_HEIGHT stay unknown — and the active display unit.
export const formulaContext = (app, ps = app.canvas ? getPageDimensions(app) : {}) => ({
  pageWidthCm: ps.width,
  pageHeightCm: ps.height,
  imageWidth: app.image ? app.canvas.width : undefined,
  imageHeight: app.image ? app.canvas.height : undefined,
  unit: app.unit
});

// The scaling is two multiplies, cheaper than a wasm crossing (wasm-parity pins the core's
// pixelToPageRaw to it); an axis with formulas off or blank is identity before any dispatch.
export const pixelToPageCoords = (app, x, y) => {
  const ps = getPageDimensions(app);
  const raw = { x: (ps.width / app.canvas.width) * x, y: (ps.height / app.canvas.height) * y };
  const blankX = !app.allowFormulas || !String(app.formulaX ?? '').trim();
  const blankY = !app.allowFormulas || !String(app.formulaY ?? '').trim();
  if (blankX && blankY) return raw;
// Both axes are in reach of either formula, so f(x) may read y and f(y) may read x.
  const ctx = { ...formulaContext(app, ps), x: raw.x, y: raw.y };
  return {
    x: blankX ? raw.x : app.formula.applyCtx(app.formulaX, 'x', raw.x, app.allowFormulas, ctx),
    y: blankY ? raw.y : app.formula.applyCtx(app.formulaY, 'y', raw.y, app.allowFormulas, ctx)
  };
};
