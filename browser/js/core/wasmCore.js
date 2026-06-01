// ── WebAssembly core loader ─────────────────────────────────────
// Instantiates the compiled shared C++ core (desktop/core → wasm, emitted as the
// self-contained ES module js/wasm/stencilCore.js with SINGLE_FILE so it loads
// under file://) and installs typed wrappers into the backend registry. The raw
// extern "C" exports speak only numbers and pointers, so this module owns the
// marshalling (strings, flat point arrays, output pointers, the pixel buffer)
// and hands the rest of the app clean JS-shaped functions.
import createStencilCore from '../wasm/stencilCore.js';
import { backend } from './wasmBackend.js';

// Build the wrappers over an instantiated Emscripten module.
const buildWrappers = core => {
  const F64 = 8;
  const I32 = 4;

  // cwrap'd scalar exports (numbers / strings in, number out).
  const cParseHex       = core.cwrap('stencil_parseHex', 'number', ['string', 'number']);
  const cDist           = core.cwrap('stencil_distToSegment', 'number', ['number', 'number', 'number', 'number', 'number', 'number']);
  const cFormulaValid   = core.cwrap('stencil_formulaValidate', 'number', ['string', 'number']);
  const cFormulaApply   = core.cwrap('stencil_formulaApply', 'number', ['string', 'number', 'number', 'number']);
  const cClampScale     = core.cwrap('stencil_clampScale', 'number', ['number']);
  const cShouldClose    = core.cwrap('stencil_shouldCloseShape', 'number', ['number', 'number', 'number', 'number', 'number']);

  // ccall'd exports that read/write through pointers.
  const cPageDims   = (name, cw, ch, cuW, cuH, out) =>
    core.ccall('stencil_pageDimensions', null, ['string', 'number', 'number', 'number', 'number', 'number', 'number'], [name, cw, ch, cuW, cuH, out, out + F64]);
  const cPixelRaw   = (x, y, dW, dH, cw, ch, out) =>
    core.ccall('stencil_pixelToPageRaw', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'], [x, y, dW, dH, cw, ch, out, out + F64]);
  const cRotate     = (ptr, n, cx, cy, ang) =>
    core.ccall('stencil_rotatePoints', null, ['number', 'number', 'number', 'number', 'number'], [ptr, n, cx, cy, ang]);
  const cBboxCenter = (ptr, n, out) =>
    core.ccall('stencil_boundingBoxCenter', null, ['number', 'number', 'number'], [ptr, n, out]);
  const cFilter     = (mode, ptr, n, r, g, b) =>
    core.ccall('stencil_applyFilterRGBA', null, ['number', 'number', 'number', 'number', 'number', 'number'], [mode, ptr, n, r, g, b]);

  // Copy an array of {x,y} into a freshly malloc'd flat f64 buffer. Caller frees.
  const allocPoints = points => {
    const n = points.length;
    const ptr = core._malloc(n * 2 * F64);
    const view = new Float64Array(core.HEAPF64.buffer, ptr, n * 2);
    for (let i = 0; i < n; i++) {
      view[2 * i] = points[i].x;
      view[2 * i + 1] = points[i].y;
    }
    return { ptr, n, view };
  };

  // FilterMode enum codes (must match desktop/core/imageFilter.hpp).
  const FILTER_MODE = { none: 0, bw: 1, sepia: 2, custom: 3 };

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
      return cFormulaValid(expr ?? '', varName.charCodeAt(0)) === 1;
    },

    formulaApply(expr, varName, val, allowFormulas) {
      return cFormulaApply(expr ?? '', varName.charCodeAt(0), val, allowFormulas ? 1 : 0);
    },

    clampScale(scale) {
      return cClampScale(scale);
    },

    shouldCloseShape(points, click, markerSize) {
      const { ptr, n } = allocPoints(points);
      try {
        return cShouldClose(ptr, n, click.x, click.y, markerSize) === 1;
      } finally {
        core._free(ptr);
      }
    },

    pageDimensions(name, cw, ch, customW, customH) {
      const out = core._malloc(2 * F64);
      try {
        cPageDims(name, cw, ch, customW, customH, out);
        return { width: core.getValue(out, 'double'), height: core.getValue(out + F64, 'double') };
      } finally {
        core._free(out);
      }
    },

    pixelToPageRaw(x, y, dims, cw, ch) {
      const out = core._malloc(2 * F64);
      try {
        cPixelRaw(x, y, dims.width, dims.height, cw, ch, out);
        return { x: core.getValue(out, 'double'), y: core.getValue(out + F64, 'double') };
      } finally {
        core._free(out);
      }
    },

    rotatePoints(points, cx, cy, angle) {
      if (points.length === 0) return;
      const { ptr, n, view } = allocPoints(points);
      try {
        cRotate(ptr, n, cx, cy, angle);
        // HEAPF64 may have detached if memory grew; re-view before reading back.
        const back = new Float64Array(core.HEAPF64.buffer, ptr, n * 2);
        for (let i = 0; i < n; i++) {
          points[i].x = back[2 * i];
          points[i].y = back[2 * i + 1];
        }
      } finally {
        core._free(ptr);
      }
    },

    boundingBoxCenter(points) {
      const { ptr, n } = allocPoints(points);
      const out = core._malloc(2 * F64);
      try {
        cBboxCenter(ptr, n, out);
        return { x: core.getValue(out, 'double'), y: core.getValue(out + F64, 'double') };
      } finally {
        core._free(ptr);
        core._free(out);
      }
    },

    applyFilterRGBA(mode, data, pixelCount, r, g, b) {
      const code = FILTER_MODE[mode] ?? FILTER_MODE.custom;
      const bytes = pixelCount * 4;
      const ptr = core._malloc(bytes);
      try {
        core.HEAPU8.set(data, ptr);
        cFilter(code, ptr, pixelCount, r, g, b);
        data.set(core.HEAPU8.subarray(ptr, ptr + bytes));
      } finally {
        core._free(ptr);
      }
    },
  };
};

// Instantiate the wasm core once and install its wrappers into the backend
// registry. Resolves to true on success, false if the module could not load
// (the app then keeps using its JS reference implementations). Idempotent.
let initPromise = null;
export const initWasmCore = () => {
  if (initPromise) return initPromise;
  initPromise = createStencilCore()
    .then(core => {
      Object.assign(backend, buildWrappers(core));
      return true;
    })
    .catch(err => {
      console.warn('[stencil] wasm core unavailable — using JS fallback:', err);
      return false;
    });
  return initPromise;
};
