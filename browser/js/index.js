import { mountHTML } from './utils.js';
import { layout } from './ui/layout.js';
import { DrawingApp } from './core/drawingApp.js';
import { core } from './core/abi/stencilCore.js';
import { hotkeys } from './core/settings/hotkeys.js';
import { registerServiceWorker } from './pwa.js';
import { createStencil } from './console/stencilApi.js';
import { runScriptHere } from './console/scriptRunner.js';
import { setScriptText } from './ui/script/buffer.js';
import { initTooltips } from './ui/tip/controlTooltip.js';
import { watchControlLabels } from './ui/ariaLabels.js';
import { wireChatPersistence } from './llm/chat/persistence.js';
import { initProjectsBackend } from './core/project/store/projectsBackend.js';
import { watchNumericInputs } from './ui/control/numericInput.js';
import { installControlSwap } from './ui/control/swap.js';
import { installVoiceModes } from './llm/voice/modes.js';
import { applyMotionAttr } from './ui/motion/motionPrefs.js';
import EVENTS from './config/events.json' with { type: 'json' };
import { publishReady } from './eventBus/appBus.js';
// Application entrypoint. Loaded LAST — importing layout registers every custom element.
// On load: init the shared C++ core (wasm), mount component hosts, construct the app, then
// dispatch `stencil:ready`, preserving DOM → app → wire order. Failed wasm leaves no-ops
// installed and consumers on their JS fallback.
// A handed-over script runs HERE because the layer order forbids core/ importing console/.
const runLaunchScript = async (app) => {
  const text = app.pendingLaunchScript;
  if (!text) return;
  app.pendingLaunchScript = '';
  setScriptText(text);
  await runScriptHere(text).catch(() => {});
};

window.onload = async () => {
  // Framed in the extension's in-page editor modal, signal liveness before the heavy boot so
  // the host keeps the modal up. Addressed at the embedder's origin (the referrer), else '*'.
  if (window.parent !== window && (location.hash || '').startsWith('#stencil=')) {
    try {
      const target = document.referrer ? new URL(document.referrer).origin : '*';
      window.parent.postMessage({ source: 'stencil-modal', type: 'ready' }, target);
    } catch {
      /* ignore */
    }
  }
  // prePaintTheme.js already wrote data-motion before first paint; restating it keeps the
  // attribute right on a host that loads the module graph without that classic script.
  applyMotionAttr();
  // Independent boots, so they run together — but BOTH must finish before the app constructs:
  // Storage reads the projects backend synchronously (core/projectsBackend.js).
  await Promise.all([core.init(), initProjectsBackend()]);
  console.info(`[stencil] core: ${core.ready ? 'WebAssembly (shared C++)' : 'JavaScript fallback'}`);
  const root = document.getElementById('root');
  mountHTML(root, layout());      // DOM first (custom elements upgrade synchronously)
  const app = new DrawingApp();   // construct AFTER mount
  // Voice input (js/llm/modes.js) — installed before the components wire, since the
  // composers and the toolbar read app.voice as they build their controls.
  installVoiceModes(app);
  // Let every numeric field take an expression ("45 + 9", "* 9"). Runtime-only, and
  // the observer catches the inputs that modals/panels render later.
  watchNumericInputs();
  installControlSwap();
  // The app instance is shared with every component via the stencil:ready
  // detail below — no window global needed.
  publishReady(app);
  // Mirrors the desktop quit dialog; beforeunload is synchronous, so it cannot use the
  // in-app confirm() modal.
  window.addEventListener('beforeunload', (e) => {
    app.storage.saveSoon.flush();   // a point committed in the last debounce window still lands
    app.storage.thumbs.flush();     // …with its thumbnail rendered now, not in idle time
    if (!app.hasEditingSession()) return;
    e.preventDefault();
    e.returnValue = '';   // Chrome/Firefox require a set returnValue to show the prompt
  });
  // Locked (non-writable, non-configurable) so page scripts can't reassign or delete it; the
  // instance and its prototypes are frozen too.
  Object.defineProperty(window, 'stencil', {
    value: createStencil(app), writable: false, configurable: false, enumerable: true
  });
  // Wired after the facade exists (a restore forces the shared chat controller, which captures
  // window.stencil on first use) and before the launch/deep-link project loads.
  wireChatPersistence(app);
  // Platform-format every button tooltip carrying a hotkey hint (⌥R on macOS,
  // Alt+R elsewhere) now that the components have rendered their markup.
  hotkeys.updateHotkeyTitles();
  // Instant tooltips everywhere (the native `title` has a ~1s delay and skips disabled controls).
  initTooltips();
  // …and the same text as the accessible name of every icon-only control, here and in
  // whatever a modal renders later (ui/ariaLabels.js).
  watchControlLabels();
  app.applyExternalLaunch().then(() => runLaunchScript(app));
  // If launched via the projects modal's "open in new tab" action (?open=<id>),
  // load that project now. No-op for normal sessions.
  app.applyProjectDeepLink();
  registerServiceWorker();        // enable offline + installable PWA (best-effort)
};
