// The real context menu (js/ui/contextMenu/contextMenu.js), mounted from its own template on the
// menuDom tree and wired against a stub app. The saved LLM settings, the touch rule, the
// resize observers and the window's resize are the test's to read and drive.
import { installMenuDom, descendants } from './menuDom.js';
import { TOUCH_MEDIA } from '../../js/utils.js';

const swapGlobals = (t, globals) => {
  const saved = Object.keys(globals).map((k) => [k, Object.getOwnPropertyDescriptor(globalThis, k)]);
  for (const [k, value] of Object.entries(globals)) {
    Object.defineProperty(globalThis, k, { value, configurable: true, writable: true });
  }
  t.after(() => {
    for (const [k, d] of saved) if (d) Object.defineProperty(globalThis, k, d); else delete globalThis[k];
  });
};

// `before(doc)` edits the mounted markup before wire() runs; `env.touch` answers the touch rule.
export const mountContextMenu = async (t, { settings = { provider: 'none' }, app = {}, before } = {}) => {
  const doc = installMenuDom();
  t.after(() => doc.restore());
  const store = new Map([['drawingApp_llmSettings', JSON.stringify(settings)]]);
  const observed = [];
  const on = {};
  const env = { touch: false };
  // The app bus delivers on window, so a fresh window per mount keeps mounts apart.
  const win = { innerWidth: 1024, innerHeight: 768,
    addEventListener: (type, fn) => { (on[type] ||= []).push(fn); },
    removeEventListener: (type, fn) => { on[type] = (on[type] || []).filter((f) => f !== fn); },
    dispatchEvent: (ev) => { (on[ev.type] || []).forEach((fn) => fn(ev)); return true; } };
  swapGlobals(t, {
    window: win,
    ResizeObserver: class { constructor(cb) { this.cb = cb; } observe(target) { observed.push({ target, fire: this.cb }); } disconnect() {} },
    localStorage: { getItem: (k) => store.get(k) ?? null, setItem: (k, v) => store.set(k, String(v)), removeItem: (k) => store.delete(k) },
    // Reduced motion: the menu's dust is decoration, and its timers would outlive the test.
    matchMedia: (q) => ({ matches: q.includes('reduced-motion') || (env.touch && q === TOUCH_MEDIA) }),
  });
  const { StencilContextMenu } = await import('../../js/ui/contextMenu/contextMenu.js');
  doc.body.insertAdjacentHTML('beforeend', `${StencilContextMenu.template()}<canvas id="canvas"></canvas>`
    + '<div id="canvas-viewport"></div><button id="chat-btn"></button><div id="chat-settings-overlay"></div>');
  before?.(doc);
  // The caller's own app object, so a controller memoized on it (sharedChatController) is the one used.
  const fullApp = Object.assign(app, { image: {}, export: {}, settings: { wireFormulaInputs() {} }, voice: { supported: false }, ...app });
  new StencilContextMenu().wire(fullApp);
  const menu = doc.getElementById('ctx-menu');
  const $ = (id) => doc.getElementById(id);
  return {
    doc, menu, $, app: fullApp, observed, store, env,
    open: (x = 50, y = 60) => $('canvas').dispatch('contextmenu', { clientX: x, clientY: y, altKey: false }),
    isOpen: () => menu.classList.contains('ctx-open'),
    kb: () => descendants(menu).filter((e) => e.classList.contains('ctx-kb')),
    // A real pointer move (or a still one) as the capture listeners sample it.
    pointer: (x, y) => doc.listeners('mousemove').forEach((l) => l.fn({ clientX: x, clientY: y })),
    resize: () => (on.resize || []).forEach((fn) => fn()),
    visible: (id) => $(id).classList.contains('ctx-sub-visible'),
  };
};
