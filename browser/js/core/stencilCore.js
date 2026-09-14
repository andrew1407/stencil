// The WebAssembly build of the shared C++ core behind the `core` singleton. The artifact is
// generated (core/WASM.md) and may be absent, so it is imported dynamically inside init(),
// degrading to the JS fallback; the dynamic-only import keeps Node from ever loading wasm.
import { buildStateOps, stateExports } from './coreHandles.js';
import { buildScriptOps, scriptExports } from './scriptHandles.js';

// A named constant: native ESM accepts a variable import() specifier, so no build step needs a literal.
const WASM_MODULE_PATH = '../wasm/stencilCore.js';

class StencilCore {
  // Installed wasm wrappers, keyed by op name.
  #ops = {};
  #ready = false;
  #initPromise = null;

  get ready() {
    return this.#ready;
  }

  // Resolves to true, or false when the module could not load (JS fallback). Idempotent.
  init() {
    if (this.#initPromise) return this.#initPromise;
    this.#initPromise = import(WASM_MODULE_PATH)
      .then(({ default: createStencilCore }) => createStencilCore())
      .then(core => {
        // A stale artifact can load yet lack exports: cwrap'ing a missing one yields a
        // non-callable that throws at call time, so verify up front and fall back to JS.
        const missing = this.#missingExports(core);
        if (missing.length) {
          console.warn(`[stencil] wasm core is stale (missing ${missing.length} export(s), e.g. ${missing[0]}) — rebuild per core/WASM.md; using JS fallback.`);
          return false;
        }
        const { withCString, ...wrappers } = this.#buildWrappers(core);
        this.#installWrappers({ ...wrappers, ...buildStateOps(core), ...buildScriptOps(core, { withCString }) });
        return true;
      })
      .catch(err => {
        console.warn('[stencil] wasm core unavailable — using JS fallback:', err);
        return false;
      });
    return this.#initPromise;
  }

  // Emscripten exposes each as `_<symbol>`; any absent rejects the whole core.
  #requiredExports = [
    'stencil_parseHex', 'stencil_distToSegment', 'stencil_formulaValidate',
    'stencil_formulaApply', 'stencil_parseDuration', 'stencil_clampScale', 'stencil_shouldCloseShape',
    'stencil_isAlbumOrientation', 'stencil_cropAspect', 'stencil_cropResizeScale',
    'stencil_pageDimensions', 'stencil_pageFormats', 'stencil_pixelToPageRaw',
    'stencil_rotatePoints', 'stencil_flipPoints', 'stencil_boundingBoxCenter', 'stencil_applyFilterRGBA',
    'stencil_applyContourRGBA', 'stencil_centeredCrop', 'stencil_resizeCropFromCorner',
    'stencil_moveCropClamped', 'stencil_scaleCropCentered', 'stencil_cropChange', 'stencil_rotateCropRectQuarter', ...stateExports, ...scriptExports,
  ];

  #missingExports(core) {
    return this.#requiredExports.filter(sym => typeof core[`_${sym}`] !== 'function');
  }

  // Read per call so a wrapper built at module-eval time picks up the post-load swap.
  // Symmetric ops only; asymmetric sites use op(name) with their own guard.
  bind(name, jsRef) {
    return (...args) => (this.#ops[name] ?? jsRef)(...args);
  }

  // The installed wasm fn, or null. For consumers with their own guard/fallback shape.
  op(name) {
    return this.#ops[name] ?? null;
  }

  // A stable list for tests/introspection.
  get opNames() {
    return [
      'parseHex', 'distToSegment', 'formulaValidate', 'formulaApply', 'parseDuration',
      'pageDimensions', 'pageFormats', 'pixelToPageRaw', 'rotatePoints', 'flipPoints', 'boundingBoxCenter',
      'clampScale', 'shouldCloseShape', 'applyFilterRGBA', 'applyContourRGBA',
      'isAlbumOrientation', 'cropAspect', 'centeredCrop', 'resizeCropFromCorner',
      'moveCropClamped', 'scaleCropCentered', 'cropResizeScale', 'cropChange', 'HoldDrawController', 'HistoryStack',
      'projectPeriodMs', 'projectAddPeriod', 'projectShouldPersist', 'projectIsExpired', 'projectIsExpiringSoon',
    ];
  }

  #installWrappers(wrappers) {
    this.#ops = wrappers;
    this.#ready = true;
  }

  // The raw exports speak only numbers and pointers, so this owns the marshalling.
  #buildWrappers(core) {
    const F64 = 8, I64 = 8, I32 = 4;

    const cParseHex       = core.cwrap('stencil_parseHex', 'number', ['string', 'number']);
    const cDist           = core.cwrap('stencil_distToSegment', 'number', ['number', 'number', 'number', 'number', 'number', 'number']);
    // The formula expr crosses as a heap pointer, not a cwrap 'string': cwrap marshals onto
    // the fixed ~64KB wasm STACK, which an unbounded formula (layout JSON / console / co-edit)
    // would overflow before the parser's depth cap could reject it. See withCString.
    const cFormulaValid   = core.cwrap('stencil_formulaValidate', 'number', ['number', 'number']);
    const cFormulaApply   = core.cwrap('stencil_formulaApply', 'number', ['number', 'number', 'number', 'number']);
    const cParseDuration  = (spec, out) => core.ccall('stencil_parseDuration', 'number', ['string', 'number'], [spec, out]);
    const cClampScale     = core.cwrap('stencil_clampScale', 'number', ['number']);
    const cShouldClose    = core.cwrap('stencil_shouldCloseShape', 'number', ['number', 'number', 'number', 'number', 'number']);
    const cIsAlbum        = core.cwrap('stencil_isAlbumOrientation', 'number', ['number', 'number']);
    const cCropAspect     = core.cwrap('stencil_cropAspect', 'number', ['number', 'number', 'number']);
    const cCropResizeScale = core.cwrap('stencil_cropResizeScale', 'number', ['number', 'number']);
    const cPageFormats    = core.cwrap('stencil_pageFormats', 'string', []);

    const cPageDims   = (name, cw, ch, cuW, cuH, out) =>
      core.ccall('stencil_pageDimensions', null, ['string', 'number', 'number', 'number', 'number', 'number', 'number'], [name, cw, ch, cuW, cuH, out, out + F64]);
    const cPixelRaw   = (x, y, dW, dH, cw, ch, out) =>
      core.ccall('stencil_pixelToPageRaw', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'], [x, y, dW, dH, cw, ch, out, out + F64]);
    const cRotate     = (ptr, n, cx, cy, ang) =>
      core.ccall('stencil_rotatePoints', null, ['number', 'number', 'number', 'number', 'number'], [ptr, n, cx, cy, ang]);
    const cFlip       = (ptr, n, horizontal, cx, cy) =>
      core.ccall('stencil_flipPoints', null, ['number', 'number', 'number', 'number', 'number'], [ptr, n, horizontal, cx, cy]);
    const cBboxCenter = (ptr, n, out) =>
      core.ccall('stencil_boundingBoxCenter', null, ['number', 'number', 'number'], [ptr, n, out]);
    const cFilter     = (mode, ptr, n, r, g, b) =>
      core.ccall('stencil_applyFilterRGBA', null, ['number', 'number', 'number', 'number', 'number', 'number'], [mode, ptr, n, r, g, b]);
    const cContour    = (ptr, w, h) => core.ccall('stencil_applyContourRGBA', null, ['number', 'number', 'number'], [ptr, w, h]);
    const cCenteredCrop = (iw, ih, aspect, out) =>
      core.ccall('stencil_centeredCrop', null, ['number', 'number', 'number', 'number'], [iw, ih, aspect, out]);
    const cResizeCorner = (x, y, w, h, corner, cx, cy, aspect, iw, ih, minSize, out) =>
      core.ccall('stencil_resizeCropFromCorner', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'], [x, y, w, h, corner, cx, cy, aspect, iw, ih, minSize, out]);
    const cMoveCrop   = (x, y, w, h, dx, dy, iw, ih, out) =>
      core.ccall('stencil_moveCropClamped', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'], [x, y, w, h, dx, dy, iw, ih, out]);
    const cScaleCrop  = (x, y, w, h, factor, aspect, iw, ih, out) =>
      core.ccall('stencil_scaleCropCentered', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'], [x, y, w, h, factor, aspect, iw, ih, out]);
    const cCropChange = (ox, oy, ow, oh, nx, ny, nw, nh, out) =>
      core.ccall('stencil_cropChange', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'], [ox, oy, ow, oh, nx, ny, nw, nh, out]);
    const cRotateCrop = (x, y, w, h, iw, ih, cw, out) =>
      core.ccall('stencil_rotateCropRectQuarter', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'], [x, y, w, h, iw, ih, cw, out]);

    // A CropRect {x,y,width,height} from a 4-double out pointer.
    const readRect = out => ({
      x: core.getValue(out, 'double'),
      y: core.getValue(out + F64, 'double'),
      width: core.getValue(out + 2 * F64, 'double'),
      height: core.getValue(out + 3 * F64, 'double')
    });
    const withRectOut = fill => {
      const out = core._malloc(4 * F64);
      try { fill(out); return readRect(out); } finally { core._free(out); }
    };

    // {x,y}[] → a malloc'd flat f64 buffer. Caller frees.
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

    // A NUL-terminated UTF-8 copy of `str` on the HEAP (no ~64KB stack limit), so an
    // adversarial expression is bounded by memory, not the wasm stack. HEAPU8 is re-read
    // after _malloc since ALLOW_MEMORY_GROWTH can detach the old view.
    const utf8 = new TextEncoder();
    const withCString = (str, fn) => {
      const bytes = utf8.encode(str ?? '');
      const ptr = core._malloc(bytes.length + 1);
      try {
        core.HEAPU8.set(bytes, ptr);
        core.HEAPU8[ptr + bytes.length] = 0;
        return fn(ptr, bytes.length);
      } finally {
        core._free(ptr);
      }
    };

    // ONE image-sized scratch buffer, grown on demand: a _malloc/_free per filter call churned
    // the heap on every repaint. HEAPU8 is re-read at each use (memory growth detaches it).
    let scratch = { ptr: 0, bytes: 0 };
    const pixelScratch = (bytes) => {
      if (bytes > scratch.bytes) { if (scratch.ptr) core._free(scratch.ptr); scratch = { ptr: core._malloc(bytes), bytes }; }
      return scratch.ptr;
    };

    // Must match core/imageFilter.hpp.
    const FILTER_MODE = { none: 0, bw: 1, sepia: 2, custom: 3, invert: 4, contour: 5 };

    return {
      // Shared with scriptHandles.js: a script is far too long for cwrap's stack marshal.
      withCString,
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

      pageDimensions(name, cw, ch, customW, customH) {
        const out = core._malloc(2 * F64);
        try {
          cPageDims(name, cw, ch, customW, customH, out);
          return { width: core.getValue(out, 'double'), height: core.getValue(out + F64, 'double') };
        } finally {
          core._free(out);
        }
      },

      // "A0 … C10" (no "custom") — the wasm twin of PAGE_SIZES in config/constants.json.
      pageFormats() {
        return cPageFormats();
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

      // `horizontal` crosses the ABI as an int (1/0), like the other flag args.
      flipPoints(points, horizontal, cx, cy) {
        if (points.length === 0) return;
        const { ptr, n } = allocPoints(points);
        try {
          cFlip(ptr, n, horizontal ? 1 : 0, cx, cy);
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
        const bytes = pixelCount * 4;
        const ptr = pixelScratch(bytes);
        core.HEAPU8.set(data, ptr);
        cFilter(FILTER_MODE[mode] ?? FILTER_MODE.custom, ptr, pixelCount, r, g, b);
        data.set(core.HEAPU8.subarray(ptr, ptr + bytes));
      },

      // Contour needs the pixel neighborhood, so it crosses with width/height.
      applyContourRGBA(data, width, height) {
        const bytes = width * height * 4;
        const ptr = pixelScratch(bytes);
        core.HEAPU8.set(data, ptr);
        cContour(ptr, width, height);
        data.set(core.HEAPU8.subarray(ptr, ptr + bytes));
      },

      isAlbumOrientation(w, h) {
        return cIsAlbum(w, h) === 1;
      },

      cropAspect(pageWidth, pageHeight, album) {
        return cCropAspect(pageWidth, pageHeight, album ? 1 : 0);
      },

      centeredCrop(imageW, imageH, aspectWoverH) {
        return withRectOut(out => cCenteredCrop(imageW, imageH, aspectWoverH, out));
      },

      resizeCropFromCorner(cur, corner, cursorX, cursorY, aspectWoverH, imageW, imageH, minSize = 16) {
        return withRectOut(out => cResizeCorner(cur.x, cur.y, cur.width, cur.height, corner, cursorX, cursorY, aspectWoverH, imageW, imageH, minSize, out));
      },

      moveCropClamped(cur, dx, dy, imageW, imageH) {
        return withRectOut(out => cMoveCrop(cur.x, cur.y, cur.width, cur.height, dx, dy, imageW, imageH, out));
      },

      scaleCropCentered(cur, factor, aspectWoverH, imageW, imageH) {
        return withRectOut(out => cScaleCrop(cur.x, cur.y, cur.width, cur.height, factor, aspectWoverH, imageW, imageH, out));
      },

      cropResizeScale(oldWidth, newWidth) {
        return cCropResizeScale(oldWidth, newWidth);
      },

      cropChange(oldRect, newRect) {
        const out = core._malloc(2 * F64);
        try {
          cCropChange(oldRect.x, oldRect.y, oldRect.width, oldRect.height, newRect.x, newRect.y, newRect.width, newRect.height, out);
          return { orientationChanged: core.getValue(out, 'double') === 1, scale: core.getValue(out + F64, 'double') };
        } finally {
          core._free(out);
        }
      },

      rotateCropRectQuarter(r, imageW, imageH, clockwise) {
        return withRectOut(out => cRotateCrop(r.x, r.y, r.width, r.height, imageW, imageH, clockwise ? 1 : 0, out));
      },
    };
  }
}

// Constructed at import time (no wasm load until init()), so module-eval-time consumers
// (FormulaEngine fields, core.bind consts) can reference it immediately.
export const core = new StencilCore();
