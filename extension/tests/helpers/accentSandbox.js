// Load src/lib/accent.js — a CLASSIC <script> IIFE, not an ES module — into a fabricated
// page scope so its behaviour can be asserted from Node.
//
// The file can't be imported: MV3 forbids inline page scripts, so accent.js is loaded as a
// plain <script> in each extension page's <head> and publishes itself on `window`. It also
// runs its work at load time (that is the point — the accent must be on <html> before first
// paint), so "load it" and "exercise it" are the same act. This module builds just enough
// of a page — documentElement, a <link rel="icon">, localStorage, chrome.storage, matchMedia
// and the storage event — to run it honestly, and hands back the levers a test needs.

import vm from 'node:vm';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const SRC = readFileSync(
  fileURLToPath(new URL('../../src/lib/accent.js', import.meta.url)),
  'utf8',
);

/**
 * Run accent.js against a fresh stub page.
 *
 * @param {object} [opts]
 * @param {Record<string,string>} [opts.stored]   - Pre-seeded localStorage entries.
 * @param {boolean} [opts.prefersDark]            - What the OS media query reports.
 * @param {boolean} [opts.storageThrows]          - Simulate private mode: every
 *   localStorage access throws, as Safari/Chrome do with cookies blocked.
 * @param {boolean} [opts.withChrome]             - false drops `chrome` entirely, the way a
 *   plain page (not an extension page) would see it.
 * @param {boolean} [opts.withMatchMedia]         - false makes matchMedia throw, the way a
 *   non-page context would.
 * @param {boolean} [opts.withViewTransitions]    - true adds document.startViewTransition
 *   (and the classList/style <html> carries), so the palette-swap WIPE runs and its origin
 *   can be read back from the --swap-* custom properties.
 * @param {Array<{id:string,rect:object,visible?:boolean}>} [opts.controls] - Elements the
 *   page owns, matched by `[id="…"]`. `rect` is a getBoundingClientRect() result; `visible`
 *   false models a laid-out-but-invisible copy (checkVisibility === false).
 * @param {{width:number,height:number}} [opts.viewport]
 */
// A '12.5%' custom property back to pixels of the box it was measured against.
const toPx = (pct, basis) => Math.round(((parseFloat(pct) / 100) * basis) * 100) / 100;

export const loadAccent = ({
  stored = {},
  prefersDark = false,
  storageThrows = false,
  withChrome = true,
  withMatchMedia = true,
  withViewTransitions = false,
  controls = [],
  viewport = { width: 480, height: 700 },
} = {}) => {
  const store = new Map(Object.entries(stored));

  const localStorage = {
    getItem(key) {
      if (storageThrows) throw new Error('private mode');
      return store.has(key) ? store.get(key) : null;
    },
    setItem(key, value) {
      if (storageThrows) throw new Error('private mode');
      store.set(key, String(value));
    },
  };

  // ── A minimal <html> + <head> ──
  const attributes = new Map();
  // Only the wipe touches these two; a page without view transitions never reaches them.
  const props = new Map();
  const classes = new Set();
  const documentElement = {
    getAttribute: (k) => (attributes.has(k) ? attributes.get(k) : null),
    setAttribute: (k, v) => attributes.set(k, v),
    // data-accent-light is a bare presence flag (the glyph-shadow switch).
    hasAttribute: (k) => attributes.has(k),
    removeAttribute: (k) => attributes.delete(k),
  };
  if (withViewTransitions) {
    documentElement.style = { setProperty: (k, v) => props.set(k, v) };
    documentElement.classList = { add: (c) => classes.add(c), remove: (c) => classes.delete(c) };
  }

  // Elements the fabricated page owns, in document order — what querySelectorAll answers.
  const elements = controls.map(({ id, rect, visible = true }) => ({
    id,
    getBoundingClientRect: () => rect,
    checkVisibility: () => visible,
  }));
  const headChildren = [];
  const bodyChildren = [];
  const pointerListeners = [];
  // Rich enough for BOTH creations accent.js does: the favicon <link> (bare property
  // writes) and the swap-dust stage — a canvas whose 2d context records what was painted,
  // so a test can read back the colour, alpha and grain count of every fill.
  const makeElement = (tag) => {
    const style = { setProperty: (k, v) => { style[k] = v; } };
    const children = [];
    const el = {
      tagName: tag.toUpperCase(),
      className: '',
      style,
      children,
      appendChild: (c) => children.push(c),
      remove: () => {
        el.removed = true;
        const i = bodyChildren.indexOf(el);
        if (i >= 0) bodyChildren.splice(i, 1);
      },
    };
    if (tag === 'canvas') {
      let arcs = 0;
      el.fills = [];
      const ctx = {
        fillStyle: '#000', globalAlpha: 1,
        scale() {}, clearRect() {}, beginPath() { arcs = 0; },
        moveTo() {}, arc() { arcs++; },
        fill() { el.fills.push({ colour: ctx.fillStyle, alpha: ctx.globalAlpha, arcs }); },
      };
      el.getContext = () => ctx;
    }
    return el;
  };
  const document = {
    documentElement,
    head: { appendChild: (el) => headChildren.push(el) },
    body: { appendChild: (el) => bodyChildren.push(el) },
    createElement: makeElement,
    // accent.js asks only for link[rel="icon"]; once it has created one, the next apply
    // must find that same element and update it in place rather than appending another.
    querySelector: (sel) =>
      sel === 'link[rel="icon"]'
        ? headChildren.find((el) => el.tagName === 'LINK' && el.rel === 'icon') || null
        : null,
    // The swap origin lookup: every element carrying the id, not just the first.
    querySelectorAll: (sel) => {
      const m = /^\[id="(.*)"\]$/.exec(sel);
      return m ? elements.filter((el) => el.id === m[1]) : [];
    },
    addEventListener: (type, fn) => { if (type === 'pointerdown') pointerListeners.push(fn); },
  };
  if (withViewTransitions) {
    // `ready` resolves like the real API's: the wake (swap dust) spawns off it.
    document.startViewTransition = (cb) => { cb(); return { ready: Promise.resolve(), finished: Promise.resolve() }; };
  }

  // ── chrome.storage.local mirror ──
  const mirrored = [];
  const chrome = { storage: { local: { set: (obj) => mirrored.push(obj) } } };

  // ── matchMedia, with a capturable change listener ──
  const mediaListeners = [];
  const mediaQuery = {
    get matches() {
      return sandboxState.prefersDark;
    },
    addEventListener: (type, fn) => {
      if (type === 'change') mediaListeners.push(fn);
    },
  };
  const sandboxState = { prefersDark };

  const windowListeners = [];
  const window = {
    innerWidth: viewport.width,
    innerHeight: viewport.height,
    matchMedia: (query) => {
      if (!withMatchMedia) throw new Error('no matchMedia here');
      return query === '(prefers-color-scheme: dark)' ? mediaQuery : { matches: false, addEventListener() {} };
    },
    addEventListener: (type, fn) => windowListeners.push({ type, fn }),
  };

  const sandbox = { window, document, localStorage };
  // A vm context has the ECMAScript built-ins only — the timer the non-wipe path uses to
  // drop .theme-swapping is a host API, so it has to be supplied.
  sandbox.setTimeout = () => 0;
  sandbox.clearTimeout = () => {};
  // The wake's own clock. Frames are handed out by the test (`page.frame(ms)`), never by
  // a real vsync, so a wake can be inspected at any instant of its life.
  let pendingFrame = null;
  sandbox.requestAnimationFrame = (cb) => { pendingFrame = cb; return 1; };
  sandbox.cancelAnimationFrame = () => { pendingFrame = null; };
  sandbox.performance = { now: () => 0 };
  sandbox.Float32Array = Float32Array;
  sandbox.Int32Array = Int32Array;
  // What dustPaint reads for the wake's colours — the page's palette vars, as they stand
  // BEFORE the swap applies (the values below stand in for lib/theme.css's light set).
  sandbox.getComputedStyle = () => ({
    getPropertyValue: (name) =>
      ({ '--bg': '#f4f5f7', '--text': '#1d2230', '--accent': '#7c3aed' }[name] || ''),
  });
  if (withChrome) sandbox.chrome = chrome;

  vm.createContext(sandbox);
  vm.runInContext(SRC, sandbox, { filename: 'accent.js' });

  return {
    /** The two APIs accent.js publishes. */
    get accent() {
      return window.StencilAccent;
    },
    get theme() {
      return window.StencilTheme;
    },
    /** `<html data-accent="…">` / `<html data-theme="…">` as currently stamped. */
    dataAccent: () => documentElement.getAttribute('data-accent'),
    dataTheme: () => documentElement.getAttribute('data-theme'),
    /** Whether `<html data-accent-light>` is set — the glyph-shadow switch. */
    isAccentLight: () => documentElement.hasAttribute('data-accent-light'),
    /** The favicon <link>, if one was created/updated. */
    faviconLink: () => headChildren.find((el) => el.tagName === 'LINK' && el.rel === 'icon') || null,
    headChildren,
    /** Layers appended to <body> — the swap-dust stage lands here (after `ready`). */
    bodyChildren,
    /** Drive the wake's rAF loop to `ms` into the swap; false once it has reaped itself. */
    frame(ms) {
      if (!pendingFrame) return false;
      const cb = pendingFrame;
      pendingFrame = null;
      cb(ms);
      return true;
    },
    /** Raw localStorage contents. */
    store,
    /** Every object handed to chrome.storage.local.set, in order. */
    mirrored,
    /** Flip the OS preference and fire the media query's change event. */
    setPrefersDark(value) {
      sandboxState.prefersDark = value;
      for (const fn of mediaListeners) fn({ matches: value });
    },
    /** Pointerdown listeners accent.js registered — there must be none: a remembered
     *  press is what used to drag the wipe off into a window corner. */
    pointerListeners,
    /** Where the last wipe started, from the custom properties handed to the keyframes.
     *  They ride as percentages of the viewport (so the circle survives a pseudo-element
     *  box measured in device pixels), and come back out here as page pixels. */
    swapOrigin: () =>
      (props.has('--swap-x')
        ? { x: toPx(props.get('--swap-x'), viewport.width),
            y: toPx(props.get('--swap-y'), viewport.height) }
        : null),
    swapRadius: () => (props.has('--swap-r')
      ? toPx(props.get('--swap-r'), Math.hypot(viewport.width, viewport.height) / Math.SQRT2)
      : null),
    /** The ragged clip the reveal actually plays: the polygon() end state, or null. */
    swapClip: (which = 'to') => props.get(`--swap-clip-${which}`) || null,
    /** Fire a cross-page `storage` event (key === null models localStorage.clear()). */
    fireStorage(key) {
      for (const { type, fn } of windowListeners) if (type === 'storage') fn({ key });
    },
    windowListeners,
  };
};
