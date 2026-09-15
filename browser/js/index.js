import { mountHTML } from './utils.js';
import { layout } from './ui/layout.js';
import { DrawingApp } from './core/drawingApp.js';
import { core } from './core/stencilCore.js';
import { hotkeys } from './core/hotkeys.js';
import { registerServiceWorker } from './pwa.js';
import { createStencil } from './console/stencilApi.js';
import { runScriptHere } from './console/scriptRunner.js';
import { setScriptText } from './ui/scriptBuffer.js';
import { initTooltips } from './ui/controlTooltip.js';
import { wireChatPersistence } from './llm/chatPersistence.js';
import { initProjectsBackend } from './core/projectsBackend.js';
import { watchNumericInputs } from './ui/numericInput.js';
import { installControlSwap } from './ui/controlSwap.js';
import { installVoiceModes } from './llm/voiceModes.js';
import { applyMotionAttr } from './ui/motionPrefs.js';
import EVENTS from './config/events.json' with { type: 'json' };
import { publishReady } from './bus/appBus.js';
// ── Application entrypoint ──────────────────────────────────────
// Loaded LAST (importing layout registers every custom element). On load: init the
// shared C++ core (wasm), mount component hosts, construct the app, then dispatch
// `stencil:ready` so components wire — preserving DOM → app → wire order. If wasm
// fails, the core installs no-ops and consumers use their JS fallback.
// A handed-over script (launchController's pendingLaunchScript) runs HERE because the layer
// order forbids core/ importing console/. The text lands in the one shared script buffer
// first, so the window opens on it whenever the user asks — but never by itself: the script
// was written elsewhere, and runScript's toast already names the line that stopped it.
const runLaunchScript = async (app) => {
  const text = app.pendingLaunchScript;
  if (!text) return;
  app.pendingLaunchScript = '';
  setScriptText(text);
  await runScriptHere(text).catch(() => {});
};

window.onload = async () => {
  // When framed in the extension's in-page editor modal, signal liveness before
  // the heavy boot so the host keeps the modal up instead of timing out to a tab.
  // Addressed at the embedder's origin (the referrer) rather than '*'; a page that
  // sends no referrer falls back to '*', which is safe because the ping carries no
  // state — just "this frame booted".
  if (window.parent !== window && (location.hash || '').startsWith('#stencil=')) {
    try {
      const target = document.referrer ? new URL(document.referrer).origin : '*';
      window.parent.postMessage({ source: 'stencil-modal', type: 'ready' }, target);
    } catch {
      /* ignore */
    }
  }
  // The motion mode on <html> for the CSS half. prePaintTheme.js already wrote it before
  // first paint; restating it costs nothing and keeps the attribute right on any host
  // that loads the module graph without that classic script.
  applyMotionAttr();
  // Independent boots, so they run together: the wasm core compiles while the projects
  // backend hydrates (its IndexedDB payload mirror + the one-time localStorage payload
  // migration). BOTH must finish before the app constructs — Storage reads the backend
  // synchronously (see core/projectsBackend.js) and every module asks core for its ops.
  await Promise.all([core.init(), initProjectsBackend()]);
  console.info(`[stencil] core: ${core.ready ? 'WebAssembly (shared C++)' : 'JavaScript fallback'}`);
  const root = document.getElementById('root');
  mountHTML(root, layout());      // DOM first (custom elements upgrade synchronously)
  const app = new DrawingApp();   // construct AFTER mount
  // Voice input (js/llm/voiceModes.js) — installed before the components wire, since the
  // composers and the toolbar read app.voice as they build their controls.
  installVoiceModes(app);
  // Let every numeric field take an expression ("45 + 9", "* 9"). Runtime-only, and
  // the observer catches the inputs that modals/panels render later.
  watchNumericInputs();
  // …and let every checkbox's tick come and go as sand (ui/controlSwap.js). One
  // delegated listener, like the desktop's one application-wide event filter, so a
  // component built later gets the motion without knowing about it.
  installControlSwap();
  // The app instance is shared with every component via the stencil:ready
  // detail below — no window global needed.
  publishReady(app);
  // Confirm before leaving an active editing session (image loaded or unsaved drawing) —
  // the browser shows its native "Leave site?" prompt. Mirrors the desktop quit dialog;
  // beforeunload is synchronous, so it can't use the in-app confirm() modal.
  window.addEventListener('beforeunload', (e) => {
    app.storage.saveSoon.flush();   // a point committed in the last debounce window still lands
    app.storage.thumbs.flush();     // …with its thumbnail rendered now, not in idle time
    if (!app.hasEditingSession()) return;
    e.preventDefault();
    e.returnValue = '';   // Chrome/Firefox require a set returnValue to show the prompt
  });
  // Expose the chainable console control API as window.stencil (see console/stencilApi.js).
  // Locked (non-writable, non-configurable) so page scripts can't reassign or delete it;
  // the instance + its prototypes are frozen too, so its tools can't be overwritten.
  Object.defineProperty(window, 'stencil', {
    value: createStencil(app), writable: false, configurable: false, enumerable: true
  });
  // Opt-in per-project chat persistence (llm-contract.md §12) — wired after
  // the facade exists (restores force the shared chat controller, which captures
  // window.stencil on first use) and before the launch/deep-link project loads.
  wireChatPersistence(app);
  // Platform-format every button tooltip carrying a hotkey hint (⌥R on macOS,
  // Alt+R elsewhere) now that the components have rendered their markup.
  hotkeys.updateHotkeyTitles();
  // Instant tooltips everywhere (the native `title` has a ~1s delay and skips disabled controls).
  initTooltips();
  // If the Stencil browser extension or the VS Code extension launched us with an image
  // and/or a .stc (URL fragment), import it now that every component is wired, then run the
  // script against the picture it just brought. No-op for normal sessions.
  app.applyExternalLaunch().then(() => runLaunchScript(app));
  // If launched via the projects modal's "open in new tab" action (?open=<id>),
  // load that project now. No-op for normal sessions.
  app.applyProjectDeepLink();
  registerServiceWorker();        // enable offline + installable PWA (best-effort)
};
