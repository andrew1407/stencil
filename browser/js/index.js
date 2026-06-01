import { mountHTML } from './utils.js';
import { layout } from './ui/layout.js';
import { DrawingApp } from './core/drawingApp.js';
import { initWasmCore } from './core/wasmCore.js';
import { usingWasm } from './core/wasmBackend.js';
// ── Application entrypoint ──────────────────────────────────────
// Loaded LAST. Importing layout registers every custom element. On load:
// instantiate the shared C++ core (wasm) so geometry/formula/filter/zoom logic
// runs the same compiled code as the desktop app, then mount the component hosts
// (each renders its markup + subscribes to `stencil:ready`), construct the app,
// then dispatch `stencil:ready` so every component wires its behavior —
// preserving the original DOM → app → wire order. If wasm fails to load, the
// backend slots stay null and every consumer falls back to its JS reference.
window.onload = async () => {
  await initWasmCore();
  console.info(`[stencil] core: ${usingWasm() ? 'WebAssembly (shared C++)' : 'JavaScript fallback'}`);
  const root = document.getElementById('root');
  mountHTML(root, layout());      // DOM first (custom elements upgrade synchronously)
  const app = new DrawingApp();   // construct AFTER mount
  window.app = app;
  document.dispatchEvent(new CustomEvent('stencil:ready', { detail: { app } }));
};
