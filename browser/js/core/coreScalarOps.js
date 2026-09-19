// The core's scalar ops over the wasm ABI: colour parsing, point-to-segment distance, the
// formula parser, duration parsing, the zoom clamp and the close-shape hit test.
export const buildScalarOps = (core, { I32, I64, withCString, allocPoints }) => {
  const cParseHex       = core.cwrap('stencil_parseHex', 'number', ['string', 'number']);
  const cDist           = core.cwrap('stencil_distToSegment', 'number', ['number', 'number', 'number', 'number', 'number', 'number']);
  // The formula expr crosses as a heap pointer, not a cwrap 'string': cwrap marshals onto the
  // fixed ~64KB wasm STACK, which an unbounded formula would overflow. See withCString.
  const cFormulaValid   = core.cwrap('stencil_formulaValidate', 'number', ['number', 'number']);
  const cFormulaApply   = core.cwrap('stencil_formulaApply', 'number', ['number', 'number', 'number', 'number']);
  const cParseDuration  = (spec, out) => core.ccall('stencil_parseDuration', 'number', ['string', 'number'], [spec, out]);
  const cClampScale     = core.cwrap('stencil_clampScale', 'number', ['number']);
  const cShouldClose    = core.cwrap('stencil_shouldCloseShape', 'number', ['number', 'number', 'number', 'number', 'number']);

  return {
    parseHex(hex) {
      const out = core._malloc(3 * I32);
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

    // ms (0 = keep forever), or null. The int64 out slot reads as two halves; exact below 2^53.
    parseDuration(spec) {
      const out = core._malloc(I64);
      try {
        if (cParseDuration(spec ?? '', out) !== 1) return null;
        return core.getValue(out + 4, 'i32') * 4294967296 + (core.getValue(out, 'i32') >>> 0);
      } finally {
        core._free(out);
      }
    },

    clampScale(scale) {
      return cClampScale(scale);
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
