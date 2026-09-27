// The control tooltip (js/ui/tip/controlTooltip.js) wired on a stub page: every timer held for the
// test to fire, a tooltip box that reads its keycaps back out of the markup renderTip wrote, and
// controls that answer closest()/contains() like real ones. The module is a singleton, so one
// page per test file.
import { installDom, createStubElement } from './dom.js';

// One stub node per <kbd class="tip-key">, grouped under the .tip-combo it sits in.
const capsOf = (html) => html.split('<span class="tip-combo">').slice(1).map((part) => {
  const caps = (part.match(/class="tip-key"/g) || []).map(() => createStubElement('kbd'));
  return createStubElement('span', { querySelectorAll: (sel) => (sel === '.tip-key' ? caps : []) });
});

const tipBox = (layout, painted) => {
  let html = '';
  let combos = [];
  const tip = createStubElement('div', {
    offsetWidth: layout.width, offsetHeight: layout.height,
    getBoundingClientRect: () => ({ left: 0, top: 0, ...painted }),
    querySelectorAll: (sel) => (sel === '.tip-combo' ? combos
      : sel === '.tip-key' ? combos.flatMap((c) => c.querySelectorAll('.tip-key')) : []),
  });
  return Object.defineProperty(tip, 'innerHTML', {
    get: () => html, set: (v) => { html = String(v); combos = capsOf(html); },
  });
};

export const installTooltipPage = async ({ layout = { width: 120, height: 40 },
  painted = { width: 116, height: 38 }, innerWidth = 1000, innerHeight = 800 } = {}) => {
  let seq = 0;
  const timers = new Map();
  const winListeners = {};
  const page = { reduced: false, tip: null };
  const doc = installDom({}, {
    window: { innerWidth, innerHeight, addEventListener: (t, fn) => { (winListeners[t] ||= []).push(fn); } },
    setTimeout: (fn, ms) => { timers.set(++seq, { fn, ms }); return seq; },
    clearTimeout: (id) => { timers.delete(id); },
    requestAnimationFrame: () => ++seq, cancelAnimationFrame() {},
    matchMedia: (q) => ({ matches: page.reduced && /reduce/.test(q) }),
  });
  const createElement = doc.createElement;
  doc.createElement = (tag) => {
    if (tag !== 'div' || page.tip) return createElement(tag);
    return (page.tip = tipBox(layout, painted));
  };
  const tooltip = await import('../../js/ui/tip/controlTooltip.js');
  tooltip.initTooltips();
  // A control with its own text, laid out at `rect`; `parent` makes it a descendant.
  const control = (text, { rect = { left: 100, top: 100, width: 30, height: 30 }, parent = null } = {}) => {
    const el = createStubElement('button', {
      isConnected: true, getBoundingClientRect: () => rect,
      closest: (sel) => (text && sel.includes('[data-title]') ? el : parent ? parent.closest(sel) : null),
      contains: (n) => n === el || el.children.some((c) => c.contains?.(n)),
    });
    if (text) el.dataset.title = text;
    parent?.appendChild(el);
    return el;
  };
  const fire = (type, ev) => doc.dispatch(type, ev);
  return {
    page, doc, tooltip, control, timers,
    get tip() { return page.tip; },
    visible: () => !!page.tip?.classList.contains('visible'),
    delays: () => [...timers.values()].map((t) => t.ms),
    // Fires every held timer of that delay; returns how many ran.
    run: (ms) => {
      const due = [...timers].filter(([, t]) => t.ms === ms);
      due.forEach(([id, t]) => { timers.delete(id); t.fn(); });
      return due.length;
    },
    over: (el, at = { clientX: 110, clientY: 110 }) => fire('pointerover', { target: el, ...at }),
    move: (at) => fire('pointermove', at),
    key: (ev) => fire('keydown', ev),
    blur: () => (winListeners.blur || []).forEach((fn) => fn()),
    fire,
    // Back to no tooltip, nothing pending, full motion.
    reset: () => { fire('pointerdown', {}); timers.clear(); page.reduced = false; },
  };
};
