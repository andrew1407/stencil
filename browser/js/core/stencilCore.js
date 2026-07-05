// ── Shared C++ core singleton ───────────────────────────────────
// Owns the WebAssembly build of the shared C++ core and typed wrappers over its raw
// extern "C" exports, exposing clean JS functions via the `core` singleton. The wasm
// artifact is generated (gitignored, built per core/WASM.md) and may be absent — so it's
// imported dynamically inside init(), degrading to the JS fallback (a static import would
// crash boot). Dynamic-only import keeps this a leaf module, so Node never loads wasm.

// Generated artifact path, relative to this module. A named constant, not inlined into
// import() (native ESM accepts a variable specifier; no build step requires a literal).
const WASM_MODULE_PATH = '../wasm/stencilCore.js';

class StencilCore {
  // Installed wasm wrappers, keyed by op name. Empty until init() succeeds.
  #ops = {};
  // Explicit readiness flag, flipped once wrappers are installed.
  #ready = false;
  // Memoized init() promise (idempotent).
  #initPromise = null;

  // True once the compiled C++ core is live (used for a boot-time status note).
  get ready() {
    return this.#ready;
  }

  // Instantiate the wasm core once and install its wrappers. Resolves to true on
  // success, false if the module could not load (the app then keeps using its JS
  // reference implementations). Idempotent — returns the cached promise.
  init() {
    if (this.#initPromise) return this.#initPromise;
    this.#initPromise = import(WASM_MODULE_PATH)
      .then(({ default: createStencilCore }) => createStencilCore())
      .then(core => {
        // Guard against a stale/incompatible artifact: an older build can load yet lack
        // exports the wrappers cwrap. cwrap'ing a missing export yields a non-callable that
        // throws at call time, so verify all required exports up front and fall back to JS refs.
        const missing = this.#missingExports(core);
        if (missing.length) {
          console.warn(`[stencil] wasm core is stale (missing ${missing.length} export(s), e.g. ${missing[0]}) — rebuild per core/WASM.md; using JS fallback.`);
          return false;
        }
        this.#installWrappers(this.#buildWrappers(core));
        return true;
      })
      .catch(err => {
        console.warn('[stencil] wasm core unavailable — using JS fallback:', err);
        return false;
      });
    return this.#initPromise;
  }

  // C exports the wrappers depend on (emscripten exposes each as `_<symbol>`).
  // Any absent → the artifact predates code that needs it, so we reject the whole
  // core rather than install bindings that throw when called.
  #requiredExports = [
    'stencil_parseHex', 'stencil_distToSegment', 'stencil_formulaValidate',
    'stencil_formulaApply', 'stencil_parseDuration', 'stencil_clampScale', 'stencil_shouldCloseShape',
    'stencil_isAlbumOrientation', 'stencil_cropAspect', 'stencil_cropResizeScale',
    'stencil_pageDimensions', 'stencil_pageFormats', 'stencil_pixelToPageRaw',
    'stencil_rotatePoints', 'stencil_boundingBoxCenter', 'stencil_applyFilterRGBA',
    'stencil_applyContourRGBA', 'stencil_centeredCrop', 'stencil_resizeCropFromCorner',
    'stencil_moveCropClamped', 'stencil_cropChange', 'stencil_rotateCropRectQuarter',
  ];

  // Names of required exports the instantiated module does not expose as callables.
  #missingExports(core) {
    return this.#requiredExports.filter(sym => typeof core[`_${sym}`] !== 'function');
  }

  // Route calls to wasm op `name` when installed, else the JS reference. Read per call so a
  // wrapper built at module-eval time picks up the post-load swap. Symmetric ops only (same
  // args both paths); asymmetric sites use op(name) with their own guard.
  bind(name, jsRef) {
    return (...args) => (this.#ops[name] ?? jsRef)(...args);
  }

  // Return the installed wasm fn for `name`, or null when not installed. For
  // asymmetric consumers that keep their own guard/fallback shape.
  op(name) {
    return this.#ops[name] ?? null;
  }

  // The op names this core installs — a stable list for tests/introspection
  // without exposing the #private ops store.
  get opNames() {
    return [
      'parseHex', 'distToSegment', 'formulaValidate', 'formulaApply', 'parseDuration',
      'pageDimensions', 'pageFormats', 'pixelToPageRaw', 'rotatePoints', 'boundingBoxCenter',
      'clampScale', 'shouldCloseShape', 'applyFilterRGBA', 'applyContourRGBA',
      'isAlbumOrientation', 'cropAspect', 'centeredCrop', 'resizeCropFromCorner',
      'moveCropClamped', 'cropResizeScale', 'cropChange',
    ];
  }

  // Install the built wrappers and flip the readiness flag. Internal only — no
  // external Object.assign reaching in.
  #installWrappers(wrappers) {
    this.#ops = wrappers;
    this.#ready = true;
  }

  // Build the typed wrappers over an instantiated Emscripten module. The raw
  // exports speak only numbers and pointers, so this owns the marshalling.
  #buildWrappers(core) {
    const F64 = 8;
    const I32 = 4;

    // cwrap'd scalar exports (numbers / strings in, number out).
    const cParseHex       = core.cwrap('stencil_parseHex', 'number', ['string', 'number']);
    const cDist           = core.cwrap('stencil_distToSegment', 'number', ['number', 'number', 'number', 'number', 'number', 'number']);
    // The formula expr is passed as a heap pointer, not a cwrap 'string' arg: cwrap
    // marshals a string onto the fixed ~64KB wasm *stack* (stringToUTF8OnStack), which
    // an untrusted, unbounded formula (layout JSON / console / co-edit) would overflow —
    // corrupting the module — before the parser's depth cap can reject it. See writeExpr.
    const cFormulaValid   = core.cwrap('stencil_formulaValidate', 'number', ['number', 'number']);
    const cFormulaApply   = core.cwrap('stencil_formulaApply', 'number', ['number', 'number', 'number', 'number']);
    const cParseDuration  = (spec, out) => core.ccall('stencil_parseDuration', 'number', ['string', 'number'], [spec, out]);
    const cClampScale     = core.cwrap('stencil_clampScale', 'number', ['number']);
    const cShouldClose    = core.cwrap('stencil_shouldCloseShape', 'number', ['number', 'number', 'number', 'number', 'number']);
    const cIsAlbum        = core.cwrap('stencil_isAlbumOrientation', 'number', ['number', 'number']);
    const cCropAspect     = core.cwrap('stencil_cropAspect', 'number', ['number', 'number', 'number']);
    const cCropResizeScale = core.cwrap('stencil_cropResizeScale', 'number', ['number', 'number']);
    const cPageFormats    = core.cwrap('stencil_pageFormats', 'string', []);

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
    const cContour    = (ptr, w, h) =>
      core.ccall('stencil_applyContourRGBA', null, ['number', 'number', 'number'], [ptr, w, h]);
    const cCenteredCrop = (iw, ih, aspect, out) =>
      core.ccall('stencil_centeredCrop', null, ['number', 'number', 'number', 'number'], [iw, ih, aspect, out]);
    const cResizeCorner = (x, y, w, h, corner, cx, cy, aspect, iw, ih, minSize, out) =>
      core.ccall('stencil_resizeCropFromCorner', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'], [x, y, w, h, corner, cx, cy, aspect, iw, ih, minSize, out]);
    const cMoveCrop   = (x, y, w, h, dx, dy, iw, ih, out) =>
      core.ccall('stencil_moveCropClamped', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'], [x, y, w, h, dx, dy, iw, ih, out]);
    const cCropChange = (ox, oy, ow, oh, nx, ny, nw, nh, out) =>
      core.ccall('stencil_cropChange', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'], [ox, oy, ow, oh, nx, ny, nw, nh, out]);
    const cRotateCrop = (x, y, w, h, iw, ih, cw, out) =>
      core.ccall('stencil_rotateCropRectQuarter', null, ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'], [x, y, w, h, iw, ih, cw, out]);

    // Read a CropRect {x,y,width,height} written to a 4-double out pointer.
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

    // Copy a NUL-terminated UTF-8 copy of `str` into a freshly malloc'd heap buffer and
    // run `fn(ptr)`, freeing after. Used to hand the formula parser its `const char*`
    // over the heap (no ~64KB stack limit), so an arbitrarily long/adversarial expression
    // is bounded by memory, not the wasm stack. HEAPU8 is re-read after _malloc since
    // ALLOW_MEMORY_GROWTH can detach the old view.
    const utf8 = new TextEncoder();
    const withCString = (str, fn) => {
      const bytes = utf8.encode(str ?? '');
      const ptr = core._malloc(bytes.length + 1);
      try {
        core.HEAPU8.set(bytes, ptr);
        core.HEAPU8[ptr + bytes.length] = 0;
        return fn(ptr);
      } finally {
        core._free(ptr);
      }
    };

    // FilterMode enum codes (must match core/imageFilter.hpp).
    const FILTER_MODE = { none: 0, bw: 1, sepia: 2, custom: 3, invert: 4, contour: 5 };

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

      // Parse a human duration → milliseconds (0 = keep forever), or null if the
      // spec is invalid. Writes the ms through a scratch out-pointer.
      parseDuration(spec) {
        const out = core._malloc(F64);
        try {
          if (cParseDuration(spec ?? '', out) !== 1) return null;
          return core.getValue(out, 'double');
        } finally {
          core._free(out);
        }
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

      // Space-separated canonical page-format names ("A0 … C10", no "custom") —
      // the wasm twin of the PAGE_SIZES table keys in config/constants.json.
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

      // Contour needs the pixel neighborhood, so it crosses the ABI with
      // width/height instead of applyFilterRGBA's flat pixel count.
      applyContourRGBA(data, width, height) {
        const bytes = width * height * 4;
        const ptr = core._malloc(bytes);
        try {
          core.HEAPU8.set(data, ptr);
          cContour(ptr, width, height);
          data.set(core.HEAPU8.subarray(ptr, ptr + bytes));
        } finally {
          core._free(ptr);
        }
      },

      // ── crop geometry (cropGeometry.js) ──
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

// The single shared-core instance. Constructed at import time (cheap — no wasm
// load happens until init()), so module-eval-time consumers (FormulaEngine
// fields, utils/zoomPan consts via core.bind) can reference it immediately.
export const core = new StencilCore();
