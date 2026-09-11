import { core } from './stencilCore.js';
import constants from '../config/constants.json' with { type: 'json' };
const { PAGE_SIZES } = constants;

// ── Page metrics: pixels → page centimetres ─────────────────────
// A wasm-parity surface (core/page/pageMetrics): the shared C++ core owns the named-size
// table and the pixel→cm scaling when it is loaded, and the JS here is the reference and
// the fallback. Split out of DrawingApp; reached as app.getPageDimensions / pixelToPageCoords.

export const getPageDimensions = (app) => {
  // Shared C++ core (wasm) owns the named-size table + landscape swap when
  // loaded; the JS below is the reference + fallback (PAGE_SIZES mirrors it).
  const fn = core.op('pageDimensions');
  if (fn) {
    return fn(app.pageSize, app.canvas.width, app.canvas.height,
      app.customPageWidth, app.customPageHeight);
  }
  if (app.pageSize === 'custom') return { width: app.customPageWidth, height: app.customPageHeight };
  const ps = { ...PAGE_SIZES[app.pageSize] };
  // Swap to landscape if image is wider than tall
  if (app.canvas.width > app.canvas.height) return { width: ps.height, height: ps.width };
  return ps;
};

export const pixelToPageCoords = (app, x, y) => {
  const ps = getPageDimensions(app);
  // Raw pixel→cm via the shared core when loaded; formula.apply itself already
  // routes through the wasm parser (see FormulaEngine).
  const pixelToPageRaw = core.op('pixelToPageRaw');
  const raw = pixelToPageRaw
    ? pixelToPageRaw(x, y, ps, app.canvas.width, app.canvas.height)
    : { x: (ps.width / app.canvas.width) * x, y: (ps.height / app.canvas.height) * y };
  return {
    x: app.formula.apply(app.formulaX, 'x', raw.x, app.allowFormulas),
    y: app.formula.apply(app.formulaY, 'y', raw.y, app.allowFormulas)
  };
};
