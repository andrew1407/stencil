// ── Shared-core backend registry ────────────────────────────────
// Slots for the compiled C++ core (desktop/core, built to WebAssembly). Each is
// null until the browser boots and `wasmCore.js` instantiates the module and
// installs the wasm-backed implementations. Every consumer (utils.js,
// formulaEngine.js, zoomPan.js, renderer.js, drawingApp.js) calls through these
// slots and falls back to its built-in JS when a slot is still null — so:
//   • the browser runs the real shared C++ once wasm is ready;
//   • if wasm fails to load, the app degrades to the JS reference path;
//   • Node test runs never populate the slots, so they exercise the JS path
//     (which the native desktop tests prove equivalent to the same C++).
// This is a leaf module (no imports) to stay free of circular dependencies.
export const backend = {
  parseHex: null,          // (hex) -> {r,g,b} | null
  distToSegment: null,     // (px,py,a,b) -> number
  formulaValidate: null,   // (expr, varName) -> boolean
  formulaApply: null,      // (expr, varName, val, allowFormulas) -> number
  pageDimensions: null,    // (name, cw, ch, customW, customH) -> {width,height}
  pixelToPageRaw: null,    // (x, y, dims, cw, ch) -> {x,y}
  rotatePoints: null,      // (points, cx, cy, angle) -> void (mutates points)
  boundingBoxCenter: null, // (points) -> {x,y}
  clampScale: null,        // (scale) -> number
  shouldCloseShape: null,  // (points, click, markerSize) -> boolean
  applyFilterRGBA: null,   // (mode, data, pixelCount, r, g, b) -> void
};

// True once the compiled C++ core is live (used for a boot-time status note).
export const usingWasm = () => backend.distToSegment !== null;

// Wrap a JS reference implementation so calls route to the wasm-backed slot
// `name` when it is installed, else to the JS reference. The slot is read on
// every call, so a wrapper built once at module-eval time still picks up the
// post-load swap — and degrades to the JS reference while the slot is null (wasm
// failed to load, or Node tests that never call initWasmCore). The fallback is
// passed in by the consumer, so this stays a leaf module (no imports → no
// cycles). Use only for symmetric slots where the wasm and JS paths take the
// same arguments; asymmetric sites (parseHex's null fall-through, the renderer's
// one-pass filter fork, drawingApp's this-bound methods) keep their explicit
// guard on purpose.
export const callCore = (name, jsRef) =>
  (...args) => (backend[name] ?? jsRef)(...args);
