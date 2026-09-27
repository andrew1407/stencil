// The projects modal (js/ui/projects/window/projectsModal.js) mounted from its own template on the
// menuDom tree and wired over a store double holding `metas` (whose expiry answers "never").
// Timers are held for the test to fire; `mode` is the motion mode the page runs under.
import { installMenuDom } from './menuDom.js';
import { setMotionOverride } from '../../js/ui/motion/motionPrefs.js';

const swap = (t, globals) => {
  const saved = Object.keys(globals).map((k) => [k, Object.getOwnPropertyDescriptor(globalThis, k)]);
  for (const [k, value] of Object.entries(globals)) Object.defineProperty(globalThis, k, { value, configurable: true, writable: true });
  t.after(() => { for (const [k, d] of saved) if (d) Object.defineProperty(globalThis, k, d); else delete globalThis[k]; });
};

export const mountProjectsModal = async (t, { metas = [], temporary = false, mode = 'slide', app: extra = {} } = {}) => {
  const doc = installMenuDom();
  t.after(() => doc.restore());
  const session = new Map();
  const timers = [];
  const observers = [];
  swap(t, {
    window: { innerWidth: 1024, innerHeight: 768, addEventListener() {}, removeEventListener() {}, dispatchEvent: () => true,
      sessionStorage: { getItem: (k) => session.get(k) ?? null, setItem: (k, v) => session.set(k, String(v)) } },
    ResizeObserver: class { observe() {} disconnect() {} },
    MutationObserver: class { constructor(cb) { observers.push(cb); } observe() {} disconnect() {} },
    requestAnimationFrame: () => 0,
    matchMedia: () => ({ matches: false }),
    setTimeout: (fn, ms) => timers.push({ fn, ms }),
    clearTimeout: () => {},
  });
  setMotionOverride({ mode });
  t.after(() => setMotionOverride(null));
  const store = { list: () => metas, getMeta: (id) => metas.find((m) => m.id === id) ?? null,
    isExpired: () => false, expiresAt: () => null, isExpiringSoon: () => false };
  const app = Object.assign(extra, {
    storage: { activeId: null, incognito: false, temporary, store }, connections: { urls: [] },
    tabs: { onProjectsChanged() {}, onPeers() {}, onIncognitoPeers() {}, whenReady: () => new Promise(() => {}) },
    ...extra,
  });
  const { StencilProjectsModal } = await import('../../js/ui/projects/window/projectsModal.js');
  doc.body.insertAdjacentHTML('beforeend', `${StencilProjectsModal.template()}<button id="projects-btn"></button>`);
  new StencilProjectsModal().wire(app);
  const $ = (id) => doc.getElementById(id);
  const list = $('projects-list');
  return {
    doc, $, app, list, timers, session,
    rows: () => list.children.filter((r) => r.classList.contains('project-row')),
    keys: () => list.children.filter((r) => r.classList.contains('project-row')).map((r) => r.dataset.filterKey),
    change: (id, value) => { $(id).value = value; $(id).dispatch('change'); },
    type: (q) => { $('projects-search').value = q; $('projects-search').dispatch('input'); },
    scan: () => observers.forEach((cb) => cb()),
  };
};
