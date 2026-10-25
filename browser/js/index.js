import { mountHTML } from './utils.js';
import { layout } from './ui/layout.js';
import { DrawingApp } from './core/drawingApp.js';
import { viewPorts } from './ui/app/viewPorts.js';
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
import { installCssSpeed } from './ui/motion/surface/cssSpeed.js';
import EVENTS from '../../common/config/events.json' with { type: 'json' };
import { publishReady } from './eventBus/appBus.js';
import { applyExternalLaunch } from './core/launch/controller.js';
import { applyLaunchQuery } from './console/launchQuery.js';
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

// Started as the graph lands rather than at onload, so the wasm compile and the projects read overlap
// the page's last loads; both settle before the app constructs (Storage reads the backend synchronously).
performance.mark('boot:graph');
const booting = Promise.all([core.init(), initProjectsBackend()]);

// The extension modal's payload arrives by postMessage (prePaintTheme.js) — almost always long
// before onload; the wait is bounded like the modal's own ready timeout (ms).
const HANDOFF_WAIT_MS = 3000;
const handoffLanded = () => new Promise((resolve) => {
  if (location.hash !== '#stencil-await') return resolve();
  const timer = setTimeout(() => {
    document.documentElement.setAttribute('data-handoff-late', '');   // prePaintTheme.js reloads on it
    resolve();
  }, HANDOFF_WAIT_MS);
  window.addEventListener('message', function onMsg() {
    if (location.hash === '#stencil-await') return;
    window.removeEventListener('message', onMsg);
    clearTimeout(timer);
    resolve();
  });
});

// Restated for a host that loads the module graph without the classic prePaintTheme.js.
applyMotionAttr();
installCssSpeed();
// Mounted (custom elements upgrade synchronously) and laid out while the core and the projects
// settle, but unseen, untouchable and with its reveal held: the first frame shown is the wired app.
const root = document.getElementById('root');
const veil = { opacity: '0', 'pointer-events': 'none' };
for (const [k, v] of Object.entries(veil)) root.style.setProperty(k, v);
mountHTML(root, layout());
const shell = root.querySelector('.container');
shell?.style.setProperty('animation', 'none');   // appReveal starts at the reveal, where viewportSync hears its end
performance.mark('boot:mounted');
const reveal = () => {
  for (const k of Object.keys(veil)) root.style.removeProperty(k);
  shell?.style.removeProperty('animation');
  for (const el of [root, shell]) if (el?.getAttribute('style') === '') el.removeAttribute('style');
};

window.onload = async () => {
  try {
    await Promise.all([booting, handoffLanded()]);
    boot();
  } finally {
    reveal();
  }
};

const boot = () => {
  console.info(`[stencil] core: ${core.ready ? 'WebAssembly (shared C++)' : 'JavaScript fallback'}`);
  const app = new DrawingApp(viewPorts);   // construct AFTER mount
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
  applyExternalLaunch(app)
    .then(() => applyLaunchQuery({ app, stencil }))
    .then(() => openLaunchScript(app, stencil));
  app.applyProjectDeepLink();
  registerServiceWorker();
  performance.mark('boot:ready');
};
