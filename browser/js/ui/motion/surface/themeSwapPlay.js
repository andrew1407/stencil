import { motionReduced } from '../motionPrefs.js';
import { spawnSwapDust, swapDustPaint } from '../dust/swapDust.js';
import { THEME_INSTANT_CLASS, THEME_SWAP_CLASS, THEME_SWAP_MS, swapEdgePolygon, swapPercent } from './themeSwap.js';
export function themeSwap(apply, origin = null) {
  if (typeof document === 'undefined') { apply(); return; }
  const root = document.documentElement;
  const reduced = motionReduced();

  if (reduced || typeof document.startViewTransition !== 'function') {
// No snapshot to wipe: one beat of colour transition. A document without a classList is
// a test stub — the palette write still happens.
    if (!root?.classList) { apply(); return; }
    root.classList.add(THEME_SWAP_CLASS);
    clearTimeout(root._themeSwapTimer);
    apply();
    root._themeSwapTimer = setTimeout(() => root.classList.remove(THEME_SWAP_CLASS), THEME_SWAP_MS);
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
    root.style.setProperty('--swap-ms', `${THEME_SWAP_MS}ms`);
    const from = last && swapEdgePolygon(last.x, last.y, last.w, last.h, 0);
    if (from) {
      root.style.setProperty('--swap-clip-from', from);
      root.style.setProperty('--swap-clip-to', swapEdgePolygon(last.x, last.y, last.w, last.h, 1));
    }
  };
  write(at());
// Raised BEFORE startViewTransition: the browser drops :hover as the transition starts,
// and hover-latches (ui/toolbar.js logo) tell that synthetic leave from a real one by this.
  root.classList.add(THEME_INSTANT_CLASS);
  const settle = () => root.classList.remove(THEME_INSTANT_CLASS);
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
