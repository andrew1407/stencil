import { motionReduced } from '../motionPrefs.js';
import { spawnSwapDust, swapDustPaint } from './swapDust.js';
import { THEME_INSTANT_CLASS, THEME_SWAP_CLASS, THEME_SWAP_MS, swapEdgePolygon, swapPercent } from './themeSwap.js';
export function themeSwap(apply, origin = null) {
  if (typeof document === 'undefined') { apply(); return; }
  const root = document.documentElement;
  const reduced = motionReduced();

  if (reduced || typeof document.startViewTransition !== 'function') {
    // No snapshot to wipe: one beat of colour transition instead (a no-op under reduced
    // motion). A document without a classList is a test stub — the palette write still
    // happens; only the decoration is skipped.
    if (!root?.classList) { apply(); return; }
    root.classList.add(THEME_SWAP_CLASS);
    clearTimeout(root._themeSwapTimer);
    apply();
    root._themeSwapTimer = setTimeout(() => root.classList.remove(THEME_SWAP_CLASS), THEME_SWAP_MS);
    return;
  }

  // `origin` may be a POINT or a function that resolves one. A function is re-asked after
  // the palette is written: the control can move between the two (label width changes,
  // toolbar reflow, scroll), and the circle is painted against the NEW page.
  // `last` keeps the same answer in PIXELS — the dust is seeded off wherever the circle
  // was really painted from, so the wake and the ring can never disagree.
  let last = null;
  const at = () => {
    const w = window.innerWidth, h = window.innerHeight;
    const p = (typeof origin === 'function' ? origin() : origin) || { x: w / 2, y: h / 2 };
    last = { x: p.x, y: p.y, w, h };
    return swapPercent(p.x, p.y, w, h);
  };
  // The OLD palette, read before `apply` flips it — the wake is the paint coming off.
  const paint = swapDustPaint();
  // Handed to the DECLARATIVE keyframes in animations/themeSwap.css. Scripting it from
  // ready.then() instead races the transition's own teardown — it ends as soon as its
  // pseudo-elements have no animations, so the wipe stopped half way.
  // --swap-x/y/r stay the wipe's authoritative geometry record (and the keyframes'
  // circle fallback); the clip the reveal actually plays is the ragged polygon pair.
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
  // Raised BEFORE startViewTransition: the browser drops :hover (synthetic
  // pointerleaves) the moment the transition starts, and hover-latches (ui/toolbar.js
  // logo) tell that synthetic leave from a real one by this class.
  root.classList.add(THEME_INSTANT_CLASS);
  const settle = () => root.classList.remove(THEME_INSTANT_CLASS);
  let t;
  try {
    // The re-ask rides INSIDE the update callback: it is the one place that runs after the
    // palette is written and before the new state is captured, so a control that moved is
    // measured where the wipe will actually be seen.
    t = document.startViewTransition(() => { apply(); write(at()); });
  } catch {
    settle(); apply(); return;   // a sync throw never ran the callback — the write still must happen
  }
  t.finished.then(settle, settle);   // also on a skipped/failed transition
  // Dust rides in only once the wipe's own animation is running (ready), so a mote's
  // delay and the ring's clock start on the same frame. An engine without `ready`
  // (or a skipped transition) simply gets no dust — the wipe never depends on it.
  t.ready?.then?.(() => spawnSwapDust(last, paint), () => {});
}

// Resolve an id to a swap origin, preferring the element that is actually ON SCREEN:
// the fullscreen layer CLONES the whole toolbar, duplicate ids and all, so
// getElementById can hand back a hidden copy.
export function originOfId(id) {
  if (typeof document?.querySelectorAll !== 'function') return null;
  for (const el of document.querySelectorAll(`[id="${id}"]`)) {
    const o = originOf(el);
    if (o) return o;
  }
  return null;
}

// Clipped out of sight by an ANCESTOR? checkVisibility below cannot see that: a control
// inside a collapsed panel keeps a perfectly good rect and its own `visibility: visible`;
// it is the ancestor's `overflow: hidden` that hides it.
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
  // `visibility: hidden` and `opacity: 0` both leave a perfectly good rect behind, and
  // an off-screen clone keeps its size too — never bloom from one. checkVisibility is
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
