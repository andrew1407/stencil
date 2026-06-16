import { mountHTML } from './utils.js';
import { layout } from './ui/layout.js';
import { DrawingApp } from './core/drawingApp.js';
import { core } from './core/stencilCore.js';
import { hotkeys } from './core/hotkeys.js';
import { registerServiceWorker } from './pwa.js';
// ── Application entrypoint ──────────────────────────────────────
// Loaded LAST. Importing layout registers every custom element. On load:
// instantiate the shared C++ core (wasm) so geometry/formula/filter/zoom logic
// runs the same compiled code as the desktop app, then mount the component hosts
// (each renders its markup + subscribes to `stencil:ready`), construct the app,
// then dispatch `stencil:ready` so every component wires its behavior —
// preserving the original DOM → app → wire order. If wasm fails to load, the
// core installs no ops and every consumer falls back to its JS reference.
window.onload = async () => {
  await core.init();
  console.info(`[stencil] core: ${core.ready ? 'WebAssembly (shared C++)' : 'JavaScript fallback'}`);
  const root = document.getElementById('root');
  mountHTML(root, layout());      // DOM first (custom elements upgrade synchronously)
  const app = new DrawingApp();   // construct AFTER mount
  // The app instance is shared with every component via the stencil:ready
  // detail below — no window global needed.
  document.dispatchEvent(new CustomEvent('stencil:ready', { detail: { app } }));
  // Platform-format every button tooltip carrying a hotkey hint (⌥R on macOS,
  // Alt+R elsewhere) now that the components have rendered their markup.
  hotkeys.updateHotkeyTitles();
  // If the Stencil browser extension launched us with an image (URL fragment),
  // import it now that every component is wired. No-op for normal sessions.
  app.applyExternalLaunch();
  registerServiceWorker();        // enable offline + installable PWA (best-effort)
};
