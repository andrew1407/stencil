import { core } from './stencilCore.js';
import constants from '../config/constants.json' with { type: 'json' };
const { PAGE_SIZES } = constants;

// A wasm-parity surface (core/page/pageMetrics): the core owns the named-size table and the
// pixel→cm scaling when loaded; the JS is the reference and the fallback.

export const getPageDimensions = (app) => {
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

export const pixelToPageCoords = (app, x, y) => {
  const ps = getPageDimensions(app);
// formula.apply itself already routes through the wasm parser (see FormulaEngine).
  const pixelToPageRaw = core.op('pixelToPageRaw');
  const raw = pixelToPageRaw
    ? pixelToPageRaw(x, y, ps, app.canvas.width, app.canvas.height)
    : { x: (ps.width / app.canvas.width) * x, y: (ps.height / app.canvas.height) * y };
  return {
    x: app.formula.apply(app.formulaX, 'x', raw.x, app.allowFormulas),
    y: app.formula.apply(app.formulaY, 'y', raw.y, app.allowFormulas)
  };
};
