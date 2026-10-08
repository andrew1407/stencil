// The WebAssembly build of the shared C++ core behind the `core` singleton. The artifact is
// generated (core/WASM.md) and may be absent, so it is imported dynamically inside init(),
// degrading to the JS fallback; the dynamic-only import keeps Node from ever loading wasm.
import { buildStateOps } from './coreHandles.js';
import { buildScriptOps } from '../scriptHandles.js';
import { RUNTIME_EXPORTS, missingExports, droppedParityOps } from './coreExports.js';
import { createMarshal } from './coreMarshal.js';
import { buildScalarOps } from './coreScalarOps.js';
import { buildPageOps } from './corePageOps.js';
import { buildImageOps } from './coreImageOps.js';
import { buildCropOps } from './coreCropOps.js';
import { buildLineOps } from './coreLineOps.js';

// A named constant: native ESM accepts a variable import() specifier, so no build step needs a literal.
const WASM_MODULE_PATH = '../../wasm/stencilCore.js';

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
        const missing = missingExports(core, RUNTIME_EXPORTS);
        if (missing.length) {
          console.warn(`[stencil] wasm core is stale (missing ${missing.length} export(s), e.g. ${missing[0]}) — rebuild per core/WASM.md; using JS fallback.`);
          return false;
        }
        const { withCString, ...wrappers } = this.#buildWrappers(core);
        const dropped = droppedParityOps(core);
        for (const op of dropped) delete wrappers[op];
        if (dropped.length) console.warn(`[stencil] wasm core lacks parity-only op(s) ${dropped.join(', ')} — rebuild per core/WASM.md.`);
        const state = dropped.includes('state') ? {} : buildStateOps(core);
        this.#installWrappers({ ...wrappers, ...state, ...buildScriptOps(core, { withCString }) });
        return true;
      })
      .catch(err => {
        console.warn('[stencil] wasm core unavailable — using JS fallback:', err);
        return false;
      });
    return this.#initPromise;
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
      'parseHex', 'distToSegment', 'formulaValidate', 'formulaApply',
      'formulaValidateCtx', 'formulaApplyCtx', 'parseDuration',
      'pageDimensions', 'pageFormats', 'pixelToPageRaw', 'rotatePoints', 'flipPoints', 'boundingBoxCenter',
      'clampScale', 'zoomMin', 'zoomMax', 'rectZoom', 'shouldCloseShape', 'applyFilterRGBA', 'applyContourRGBA',
      'isAlbumOrientation', 'cropAspect', 'centeredCrop', 'resizeCropFromCorner',
      'moveCropClamped', 'scaleCropCentered', 'swapCropOrientation', 'cropResizeScale', 'cropChange',
      'snapCropRect', 'rotateEditQuarter', 'mirrorEdit', 'mergeLinesKeep', 'unchainLine', 'pullOutPoint',
      'HoldDrawController', 'HistoryStack',
      'projectPeriodMs', 'projectAddPeriod', 'projectShouldPersist', 'projectIsExpired', 'projectIsExpiringSoon',
    ];
  }

  #installWrappers(wrappers) {
    this.#ops = wrappers;
    this.#ready = true;
  }

  // Each group names its own cwrap signatures; the marshalling is shared (coreMarshal.js).
  #buildWrappers(core) {
    const m = createMarshal(core);
    return {
      // Shared with scriptHandles.js: a script is far too long for cwrap's stack marshal.
      withCString: m.withCString,
      ...buildScalarOps(core, m),
      ...buildPageOps(core, m),
      ...buildImageOps(core, m),
      ...buildCropOps(core, m),
      ...buildLineOps(core, m),
    };
  }
}

// Constructed at import time (no wasm load until init()), so module-eval-time consumers
// (FormulaEngine fields, core.bind consts) can reference it immediately.
export const core = new StencilCore();
