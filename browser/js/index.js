import { mountHTML } from './utils.js';
import { layout } from './ui/layout.js';
import { DrawingApp } from './core/drawingApp.js';
import { core } from './core/abi/stencilCore.js';
import { hotkeys } from './core/settings/hotkeys.js';
import { registerServiceWorker } from './pwa.js';
import { createStencil } from './console/stencilApi.js';
import { setScriptText } from './ui/script/buffer.js';
import { initTooltips } from './ui/tip/controlTooltip.js';
import { watchControlLabels } from './ui/ariaLabels.js';
import { wireChatPersistence } from './llm/chat/persistence.js';
import { initProjectsBackend } from './core/project/store/projectsBackend.js';
import { enhanceNumericInput, enhanceNumericInputs } from './ui/control/numericInput.js';
import { onElementAdded } from './ui/domWatch.js';
import { installControlSwap } from './ui/control/swap.js';
import { installVoiceModes } from './llm/voice/modes.js';
import { applyMotionAttr } from './ui/motion/motionPrefs.js';
import EVENTS from '../../common/config/events.json' with { type: 'json' };
import { publishReady } from './eventBus/appBus.js';
import { applyExternalLaunch } from './core/launch/controller.js';
import { flushPendingWrites, installUnloadFlush } from './ui/shell/unloadFlush.js';
// Application entrypoint, loaded LAST (importing layout registers every custom element): DOM → app
// → wire order, then `stencil:ready`. Failed wasm leaves consumers on their JS fallback.
// A handed-over script lands HERE because the layer order forbids core/ importing console/. It
// opens in the Script window, whatever `scriptMode` the link names: the user presses Run.
const openLaunchScript = (app, stencil) => {
  const text = app.pendingLaunchScript;
  if (!text) return;
  app.pendingLaunchScript = '';
  setScriptText(text);
  try { stencil.openWindow('script'); } catch { /* the buffer still holds it */ }
};

window.onload = async () => {
  // In the extension's editor modal: liveness before the heavy boot, to the referrer's origin.
  if (window.parent !== window && (location.hash || '').startsWith('#stencil=')) {
    try {
      const target = document.referrer ? new URL(document.referrer).origin : '*';
      window.parent.postMessage({ source: 'stencil-modal', type: 'ready' }, target);
    } catch {
      /* ignore */
    }
  }
  // Restated for a host that loads the module graph without the classic prePaintTheme.js.
  applyMotionAttr();
  // Both before the app constructs: Storage reads the projects backend synchronously.
  await Promise.all([core.init(), initProjectsBackend()]);
  console.info(`[stencil] core: ${core.ready ? 'WebAssembly (shared C++)' : 'JavaScript fallback'}`);
  const root = document.getElementById('root');
  mountHTML(root, layout());      // DOM first (custom elements upgrade synchronously)
  const app = new DrawingApp();   // construct AFTER mount
  // Before the components wire: the composers and the toolbar read app.voice as they build.
  installVoiceModes(app);
  enhanceNumericInputs(document);
  onElementAdded((node) => (node.matches?.('input[type="number"]') ? enhanceNumericInput(node) : enhanceNumericInputs(node)));
  installControlSwap();
  publishReady(app);
  // The desktop quit dialog's twin; beforeunload is synchronous, so no in-app confirm().
  window.addEventListener('beforeunload', (e) => {
    flushPendingWrites(app);
    if (!app.hasEditingSession()) return;
    e.preventDefault();
    e.returnValue = '';   // Chrome/Firefox require a set returnValue to show the prompt
  });
  installUnloadFlush(app);
  const stencil = createStencil(app);
  Object.defineProperty(window, 'stencil', {
    value: stencil, writable: false, configurable: false, enumerable: true
  });
  // Wired after the facade exists (a restore forces the shared chat controller, which captures
  // window.stencil on first use) and before the launch/deep-link project loads.
  wireChatPersistence(app);
  hotkeys.updateHotkeyTitles();
  initTooltips();
  watchControlLabels();
  applyExternalLaunch(app).then(() => openLaunchScript(app, stencil));
  app.applyProjectDeepLink();
  registerServiceWorker();
};
