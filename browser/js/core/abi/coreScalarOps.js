// The core's scalar ops over the wasm ABI: colour parsing, point-to-segment distance, the
// formula parser, duration parsing, the zoom clamp, its bounds and the zoom-to-rect fit, and the
// close-shape hit test.
import { heapAlloc } from './coreMarshal.js';

// An absent field crosses as NaN, which the core reads as "not supplied".
const num = (v) => (Number.isFinite(v) ? v : NaN);
const ctxArgs = (ctx) => [num(ctx?.x), num(ctx?.y), num(ctx?.pageWidthCm), num(ctx?.pageHeightCm), num(ctx?.imageWidth), num(ctx?.imageHeight), ctx?.unit ?? 'cm'];

export const buildScalarOps = (core, { I32, I64, withCString, allocPoints, withF64Out }) => {
  const cParseHex       = core.cwrap('stencil_parseHex', 'number', ['string', 'number']);
  const cDist           = core.cwrap('stencil_distToSegment', 'number', ['number', 'number', 'number', 'number', 'number', 'number']);
  // The formula expr crosses as a heap pointer, not a cwrap 'string': cwrap marshals onto the
  // fixed ~64KB wasm STACK, which an unbounded formula would overflow. See withCString.
  const cFormulaValid   = core.cwrap('stencil_formulaValidate', 'number', ['number', 'number']);
  const cFormulaApply   = core.cwrap('stencil_formulaApply', 'number', ['number', 'number', 'number', 'number']);
  const cFormulaValidCtx = core.cwrap('stencil_formulaValidateCtx', 'number', ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'string']);
  const cFormulaApplyCtx = core.cwrap('stencil_formulaApplyCtx', 'number', ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'string']);
  // The spec is caller text of any length, so it crosses on the heap like the formula expr.
  const cParseDuration  = core.cwrap('stencil_parseDuration', 'number', ['number', 'number']);
  const cClampScale     = core.cwrap('stencil_clampScale', 'number', ['number']);
  const cZoomMin        = core.cwrap('stencil_zoomMin', 'number', []);
  const cZoomMax        = core.cwrap('stencil_zoomMax', 'number', []);
  const cRectZoom       = core.cwrap('stencil_rectZoom', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number']);
  const cShouldClose    = core.cwrap('stencil_shouldCloseShape', 'number', ['number', 'number', 'number', 'number', 'number']);

  return {
    parseHex(hex) {
      const out = heapAlloc(core, 3 * I32);
      try {
        if (cParseHex(hex, out) !== 1) return null;
        return { r: core.getValue(out, 'i32'), g: core.getValue(out + I32, 'i32'), b: core.getValue(out + 2 * I32, 'i32') };
      } finally {
        core._free(out);
      }
    },

    distToSegment(px, py, a, b) {
      return cDist(px, py, a.x, a.y, b.x, b.y);
    },

    formulaValidate(expr, varName) {
      return withCString(expr, p => cFormulaValid(p, varName.charCodeAt(0))) === 1;
    },

    formulaApply(expr, varName, val, allowFormulas) {
      return withCString(expr, p => cFormulaApply(p, varName.charCodeAt(0), val, allowFormulas ? 1 : 0));
    },

    formulaValidateCtx(expr, ctx) {
      return withCString(expr, p => cFormulaValidCtx(p, ...ctxArgs(ctx))) === 1;
    },

    formulaApplyCtx(expr, varName, val, allowFormulas, ctx) {
      return withCString(expr, p => cFormulaApplyCtx(p, varName.charCodeAt(0), val, allowFormulas ? 1 : 0, ...ctxArgs(ctx)));
    },

    // ms (0 = keep forever), or null. The int64 out slot reads as two halves; exact below 2^53.
    parseDuration(spec) {
      const out = heapAlloc(core, I64);
      try {
        if (withCString(spec ?? '', (p) => cParseDuration(p, out)) !== 1) return null;
        return core.getValue(out + 4, 'i32') * 4294967296 + (core.getValue(out, 'i32') >>> 0);
      } finally {
        core._free(out);
      }
    },

    clampScale(scale) {
      return cClampScale(scale);
    },

    zoomMin() {
      return cZoomMin();
    },

    zoomMax() {
      return cZoomMax();
    },

    rectZoom(x1, y1, rectW, rectH, availW, availH) {
      const at = withF64Out(out => cRectZoom(x1, y1, rectW, rectH, availW, availH, out));
      return { scale: at(0), scrollLeft: at(1), scrollTop: at(2) };
    },

    shouldCloseShape(points, click, pointSize) {
      const { ptr, n } = allocPoints(points);
      try {
        return cShouldClose(ptr, n, click.x, click.y, pointSize) === 1;
      } finally {
        core._free(ptr);
      }
    },
  };
};

// The formulas over a whole run of raw page points (pageMetrics.js pixelToPageCoordsAll).
export const buildFormulaPointOps = (core, { withCString, allocPoints }) => {
  const cFormulaApplyPoints = core.cwrap('stencil_formulaApplyPoints', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'string']);
  return {
    // One crossing for a whole run of raw page points. The answer is read through a fresh heap
    // view: evaluating may grow memory, which detaches the one allocPoints made.
    formulaApplyPoints(exprX, exprY, raws, ctx) {
      if (!raws.length) return [];
      const { ptr, n } = allocPoints(raws);
      try {
        withCString(exprX, (px) => withCString(exprY, (py) =>
          cFormulaApplyPoints(px, py, ptr, n, ...ctxArgs(ctx).slice(2))));
        const out = new Float64Array(core.HEAPF64.buffer, ptr, n * 2);
        return raws.map((_, i) => ({ x: out[2 * i], y: out[2 * i + 1] }));
      } finally {
        core._free(ptr);
      }
    },
  };
};
