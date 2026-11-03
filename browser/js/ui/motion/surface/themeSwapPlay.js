import { motionMode, motionMs, motionReduced } from '../motionPrefs.js';
import { SWAP_GENIE_MS, WIPE_CLIP_FROM, WIPE_CLIP_TO, swapGenieKeyframes } from './themeSwapShape.js';
import { spawnSwapDust, swapDustPaint } from '../dust/swapDust.js';
import { THEME_INSTANT_CLASS, THEME_SWAP_CLASS, THEME_SWAP_MS, swapEdgePolygon, swapPercent } from './themeSwap.js';
import { cliModeOn } from '../../../console/shell/gate.js';
import { nameSwapWindows } from './themeSwapWindows.js';
// Transition wipes in from the left; Sliding pours out of the control or down from the top (themeSwapShape.js) on keyframes
// written per swap into a sheet of its own; the particle modes keep their ragged ring.
export const SWAP_GENIE_CLASS = 'theme-swap-genie';
// The ring and the wipe's curve (animations/themeSwap.css themeSwapReveal), for the windows' own reveals.
const SWAP_EASE_CSS = 'cubic-bezier(0.4, 0.25, 0.95, 1)';
const sheets = {};
const writeSheet = (key, text) => {
  if (typeof document.createElement !== 'function' || !document.head) return false;
  if (!sheets[key]?.isConnected) {
    sheets[key] = document.createElement('style');
    document.head.appendChild(sheets[key]);
  }
  sheets[key].textContent = text;
  return true;
};
const writeShape = (root, px, fromTop) => {
  const mode = motionMode();
  if (mode === 'transition') {
    root.style.setProperty('--swap-clip-from', WIPE_CLIP_FROM);
    root.style.setProperty('--swap-clip-to', WIPE_CLIP_TO);
  }
  const pour = mode === 'slide' && !!px && writeSheet('genie', swapGenieKeyframes(px.x, px.y, px.w, px.h, fromTop));
  root.classList?.toggle?.(SWAP_GENIE_CLASS, pour);
  if (pour) root.style.setProperty('--swap-ms', `${motionMs(SWAP_GENIE_MS)}ms`);
};
// Every open window's own reveal (themeSwapWindows.js), named before the old state is captured.
const nameWindows = (page, fromTop) => {
  const mode = motionMode();
  const wins = nameSwapWindows(page, mode, motionMs(mode === 'slide' ? SWAP_GENIE_MS : THEME_SWAP_MS), SWAP_EASE_CSS,
                               undefined, undefined, fromTop);
  writeSheet('windows', wins.css);
  return wins.release;
};

// `fromTop`: an accent change, which Sliding pours down from the top; a theme change pours out of its control.
export function themeSwap(apply, origin = null, { fromTop = false } = {}) {
  if (typeof document === 'undefined') { apply(); return; }
  if (cliModeOn()) { apply(); return; }   // the CLI stage runs the Zig wipe itself; no flood, no dust
  const root = document.documentElement;
  const reduced = motionReduced();

  if (reduced || typeof document.startViewTransition !== 'function') {
// No snapshot to wipe: one beat of colour transition. A document without a classList is
// a test stub — the palette write still happens.
    if (!root?.classList) { apply(); return; }
    root.classList.add(THEME_SWAP_CLASS);
    clearTimeout(root._themeSwapTimer);
    apply();
    root._themeSwapTimer = setTimeout(() => root.classList.remove(THEME_SWAP_CLASS), motionMs(THEME_SWAP_MS));
    return;
  }

// A function `origin` is re-asked after the palette is written (the control can move);
// `last` keeps the answer in PIXELS so the dust is seeded off where the circle was painted.
  let last = null;
  const at = () => {
    const w = window.innerWidth, h = window.innerHeight;
    const p = (typeof origin === 'function' ? origin() : origin) || { x: w / 2, y: h / 2 };
    last = { x: p.x, y: p.y, w, h };
    return swapPercent(p.x, p.y, w, h);
  };
  const paint = swapDustPaint();
// Handed to the declarative keyframes in animations/themeSwap.css: scripting from ready.then()
// races the transition's teardown. --swap-x/y/r stay the geometry record and circle fallback.
  const write = ({ x, y, r }) => {
    root.style.setProperty('--swap-x', `${x}%`);
    root.style.setProperty('--swap-y', `${y}%`);
    root.style.setProperty('--swap-r', `${r}%`);
    root.style.setProperty('--swap-ms', `${motionMs(THEME_SWAP_MS)}ms`);
    const from = last && swapEdgePolygon(last.x, last.y, last.w, last.h, 0);
    if (from) {
      root.style.setProperty('--swap-clip-from', from);
      root.style.setProperty('--swap-clip-to', swapEdgePolygon(last.x, last.y, last.w, last.h, 1));
    }
    writeShape(root, last, fromTop);
  };
  write(at());
// Raised BEFORE startViewTransition: the browser drops :hover as the transition starts,
// and hover-latches (ui/toolbar/toolbar.js logo) tell that synthetic leave from a real one by this.
  root.classList.add(THEME_INSTANT_CLASS);
  const release = nameWindows(last, fromTop);
  const settle = () => { root.classList.remove(THEME_INSTANT_CLASS); release(); };
  let t;
  try {
// The re-ask rides inside the update callback: after the palette is written, before capture.
    t = document.startViewTransition(() => { apply(); write(at()); });
  } catch {
    settle(); apply(); return;   // a sync throw never ran the callback — the write still must happen
  }
  t.finished.then(settle, settle);   // also on a skipped/failed transition
// Dust only once the wipe's animation is running (ready), so both clocks start on one frame.
  t.ready?.then?.(() => spawnSwapDust(last, paint), () => {});
}

// Prefer the element actually ON SCREEN: the fullscreen layer clones the toolbar, ids and all.
export function originOfId(id) {
  if (typeof document?.querySelectorAll !== 'function') return null;
  for (const el of document.querySelectorAll(`[id="${id}"]`)) {
    const o = originOf(el);
    if (o) return o;
  }
  return null;
}

// Clipped by an ancestor's `overflow: hidden`? checkVisibility cannot see that.
function clippedAway(el, r) {
  if (typeof getComputedStyle !== 'function') return false;   // a stub (tests): nothing to clip
  for (let p = el.parentElement; p; p = p.parentElement) {
    const s = getComputedStyle(p);
    if (s.overflow === 'visible' && s.overflowX === 'visible' && s.overflowY === 'visible') continue;
    const b = p.getBoundingClientRect();
    if (r.right <= b.left || r.left >= b.right || r.bottom <= b.top || r.top >= b.bottom) return true;
  }
  return false;
}

export function originOf(el) {
  const r = el?.getBoundingClientRect?.();
  if (!r || (!r.width && !r.height)) return null;
  if (el.parentElement && clippedAway(el, r)) return null;
// `visibility: hidden` and `opacity: 0` leave a good rect behind; checkVisibility is
// Chromium/WebKit-only, so it stays a bonus check.
  if (typeof el.checkVisibility === 'function' &&
      !el.checkVisibility({ visibilityProperty: true, opacityProperty: true, contentVisibilityAuto: true }))
    return null;
  const c = { x: r.left + r.width / 2, y: r.top + r.height / 2 };
  const w = typeof window !== 'undefined' ? window.innerWidth : 0;
  const h = typeof window !== 'undefined' ? window.innerHeight : 0;
  if (w && h && (c.x < 0 || c.y < 0 || c.x > w || c.y > h)) return null;   // scrolled/parked off-screen
  return c;
}
