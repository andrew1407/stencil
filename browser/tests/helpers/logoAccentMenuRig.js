// Shared rig for the logoAccentMenu.test.js family: the stub element factory (contains()
// always hits, so containment guards see dispatched targets as inside) and a wired logo.
import { wireLogoColorPicker } from '../../js/ui/toolbar/toolbar.js';
import { createStubElement } from './dom.js';

// ── Stubs ── the shared element factory; contains() must always hit, so containment
// guards see dispatched targets as "inside".
export const makeEl = () => createStubElement('div', { contains: () => true });

// A wired logo + wrap + menu + stub document; returns every handle a test needs.
export const rig = ({ accent = 'violet', customAccent = null } = {}) => {
  const logo = makeEl();
  const wrap = makeEl();
  const menu = makeEl();
  menu.hidden = true;
  logo.closest = () => wrap;
  wrap.querySelector = (sel) => (sel === '.logo-accent-menu' ? menu : null);
  wrap.getBoundingClientRect = () => ({ bottom: 64 });

  const htmlEl = makeEl();
  const doc = makeEl();       // reuse the stub for add/removeEventListener bookkeeping
  doc.documentElement = htmlEl;
  doc.createElement = () => makeEl();
  // Handlers read the global `document`/`window` at dispatch time, so the stubs stay
  // installed for the whole file (each rig() replaces them; node isolates test files).
  globalThis.document = doc;
  const win = makeEl();
  win.innerHeight = 800;
  globalThis.window = win;

  const calls = [];
  const app = {
    accent, customAccent,
    setAccent: (k, origin) => calls.push(['setAccent', k, origin]),
    setCustomAccent: (h, origin) => calls.push(['setCustomAccent', h, origin]),
  };
  wireLogoColorPicker(logo, app);
  // Live :hover is what gates the Alt routes (like every icon); helpers flip it and
  // keep the animation latch in step.
  const hover = (on) => {
    wrap.matches = (sel) => on && sel === ':hover';
    wrap.dispatch(on ? 'pointerenter' : 'pointerleave');
  };
  const pressAlt = () => doc.dispatch('keydown', { key: 'Alt', preventDefault: () => {} });
  const releaseAlt = () => doc.dispatch('keyup', { key: 'Alt' });
  return { logo, wrap, menu, doc, htmlEl, win, app, calls, hover, pressAlt, releaseAlt };
};

// Rows carrying the ✓ — aria-selected drives it (accentPicker.test.js pins the CSS).
export const marked = (menu) => menu.children
  .filter((li) => li.getAttribute('aria-selected') === 'true').map((li) => li.dataset.key);
