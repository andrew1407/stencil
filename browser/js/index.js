import { mountHTML } from './utils.js';
import { layout } from './ui/layout.js';
import { DrawingApp } from './core/drawingApp.js';
import { core } from './core/stencilCore.js';
import { hotkeys } from './core/hotkeys.js';
import { registerServiceWorker } from './pwa.js';
import { createStencil } from './console/stencilApi.js';
// ── Application entrypoint ──────────────────────────────────────
// Loaded LAST (importing layout registers every custom element). On load: init the
// shared C++ core (wasm), mount component hosts, construct the app, then dispatch
// `stencil:ready` so components wire — preserving DOM → app → wire order. If wasm
// fails, the core installs no-ops and consumers use their JS fallback.
window.onload = async () => {
  // When framed in the extension's in-page editor modal, signal liveness before
  // the heavy boot so the host keeps the modal up instead of timing out to a tab.
  if (window.parent !== window && (location.hash || '').startsWith('#stencil=')) {
    try {
      window.parent.postMessage({ source: 'stencil-modal', type: 'ready' }, '*');
    } catch {
      /* ignore */
    }
  }
  await core.init();
  console.info(`[stencil] core: ${core.ready ? 'WebAssembly (shared C++)' : 'JavaScript fallback'}`);
  const root = document.getElementById('root');
  mountHTML(root, layout());      // DOM first (custom elements upgrade synchronously)
  const app = new DrawingApp();   // construct AFTER mount
  // The app instance is shared with every component via the stencil:ready
  // detail below — no window global needed.
  document.dispatchEvent(new CustomEvent('stencil:ready', { detail: { app } }));
  // Expose the chainable console control API as window.stencil (see console/stencilApi.js).
  // Locked (non-writable, non-configurable) so page scripts can't reassign or delete it;
  // the instance + its prototypes are frozen too, so its tools can't be overwritten.
  Object.defineProperty(window, 'stencil', {
    value: createStencil(app), writable: false, configurable: false, enumerable: true
  });
  // Platform-format every button tooltip carrying a hotkey hint (⌥R on macOS,
  // Alt+R elsewhere) now that the components have rendered their markup.
  hotkeys.updateHotkeyTitles();
  // If the Stencil browser extension launched us with an image (URL fragment),
  // import it now that every component is wired. No-op for normal sessions.
  app.applyExternalLaunch();
  // If launched via the projects modal's "open in new tab" action (?open=<id>),
  // load that project now. No-op for normal sessions.
  app.applyProjectDeepLink();
  registerServiceWorker();        // enable offline + installable PWA (best-effort)
};
