// Loads the extension's pre-paint CLASSIC <script> set — not ES modules — into a fabricated page
// scope so its behaviour can be asserted from Node. They cannot be imported: MV3 forbids inline
// page scripts, so they are plain <script>s that publish themselves on `window` and do their
// work at load time. This builds just enough of a page — documentElement, a <link rel="icon">,
// localStorage, chrome.storage, matchMedia, the storage event — in each host page's <head> order.

import vm from 'node:vm';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { makePage } from './accentDom.js';

const SRC = ['prefs.js', 'swapGeometry.js', 'dustGrains.js', 'dustWake.js', 'themeSwap.js',
  'accent.js', 'shellPrefs.js']
  .map((f) => readFileSync(fileURLToPath(new URL(`../../src/lib/${f}`, import.meta.url)), 'utf8')).join('\n');

/**
 * Run accent.js against a fresh stub page.
 *
 * @param {object} [opts]
 * @param {Record<string,string>} [opts.stored]   - Pre-seeded localStorage entries.
 * @param {boolean} [opts.prefersDark]            - What the OS media query reports.
 * @param {boolean} [opts.prefersReduced]         - …and its reduced-motion preference.
 * @param {boolean} [opts.storageThrows]          - Simulate private mode: every
 *   localStorage access throws, as Safari/Chrome do with cookies blocked.
 * @param {boolean} [opts.withChrome]             - false drops `chrome` entirely, the way a
 *   plain page (not an extension page) would see it.
 * @param {boolean} [opts.withMatchMedia]         - false makes matchMedia throw, the way a
 *   non-page context would.
 * @param {boolean} [opts.withViewTransitions]    - true adds document.startViewTransition
 *   (and the classList/style <html> carries), so the palette-swap WIPE runs and its origin
 *   can be read back from the --swap-* custom properties.
 * @param {boolean} [opts.deferViewTransitions]   - the same, but the callback QUEUES until
 *   runSwaps() — the real API's timing, and the gap a menu's own close runs in.
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
  prefersReduced = false,
  storageThrows = false,
  withChrome = true,
  withMatchMedia = true,
  withViewTransitions = false,
  deferViewTransitions = false,
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

  const { document, documentElement, props, headChildren, bodyChildren, pointerListeners, queuedSwaps } =
    makePage({ controls, withViewTransitions, deferViewTransitions });

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
  const sandboxState = { prefersDark, prefersReduced };

  const windowListeners = [];
  const window = {
    innerWidth: viewport.width,
    innerHeight: viewport.height,
    matchMedia: (query) => {
      if (!withMatchMedia) throw new Error('no matchMedia here');
      if (query === '(prefers-color-scheme: dark)') return mediaQuery;
      return { matches: query === '(prefers-reduced-motion: reduce)' && sandboxState.prefersReduced, addEventListener() {} };
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
  // BEFORE the swap applies (the values below stand in for lib/theme/palette.css's light set).
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
    get motion() {
      return window.StencilMotion;
    },
    /** `<html data-motion="…">` as currently stamped. */
    dataMotion: () => documentElement.getAttribute('data-motion'),
    /** Flip the OS reduced-motion preference (read live, no event). */
    setPrefersReduced(value) { sandboxState.prefersReduced = value; },
    /** `<html data-accent="…">` / `<html data-theme="…">` as currently stamped. */
    dataAccent: () => documentElement.getAttribute('data-accent'),
    dataTheme: () => documentElement.getAttribute('data-theme'),
    /** Whether `<html data-accent-light>` is set — the on-accent ink switch. */
    isAccentLight: () => documentElement.hasAttribute('data-accent-light'),
    /** The inline `--accent` override (a custom accent / preview), '' when none. */
    inlineAccent: () => documentElement.style.getPropertyValue('--accent'),
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
    /** Run (and clear) the view-transition callbacks `deferViewTransitions` queued. */
    runSwaps() { for (const cb of queuedSwaps.splice(0)) cb(); },
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
