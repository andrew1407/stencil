import { motionReduced } from '../motionPrefs.js';
import { disintegrate } from './disintegrate.js';
import { speckPainter } from './painters.js';
import { SURFACE_COLS, SURFACE_DRIVEN_CLASS, SURFACE_FORMING_CLASS, SURFACE_IN_MS, SURFACE_LEAVING_CLASS, SURFACE_MOTE_PX, SURFACE_OUT_MS, SURFACE_ROWS } from './surfaceMotion.js';
import { cancelDust, reshapeGrid } from './tiles.js';

// A surface NEVER dusts as clones of itself, however small it is. A row scatter can
// afford to (a list row is one element in one place), but a surface's cloud lands on
// <body> — and a cloud of a few hundred copies of a menu is a few hundred more elements
// answering to `.accent-dd-menu`, `.ctx-sub`, `#chat-…`. Everything that queries the
// page — the app's own code, and every test that drives it — would have to know about a
// decoration. Flat specks in the surface's own colours carry no identity at all, and at
// a 6px grain that is very nearly all a clone would have shown anyway.
export const surfaceDust = (el, point, { ms, gather, px = SURFACE_MOTE_PX, paint = null, box = null,
                                 delayScale = null }) => {
  if (typeof document === 'undefined' || !el?.getBoundingClientRect) return false;
  if (!(Number.isFinite(point?.x) && Number.isFinite(point?.y))) return false;
  const r = box || el.getBoundingClientRect();
  if (!(r.width >= 8 && r.height >= 8)) return false;
  const grid = reshapeGrid(SURFACE_COLS, SURFACE_ROWS, r.width, r.height, px);
  return disintegrate(el, {
    ...grid, gather, toward: point, ms, px, toBody: true, box, delayScale,
    hostClass: gather ? 'dust-forming' : 'dust-leaving',
    paintTile: speckPainter(el, paint),
  });
};

// Drop whatever a surface has in flight — the cloud AND the classes driving its own
// opacity — leaving the end state untouched. Every open/close begins here, so a
// double-clicked menu or a swept-past modal always converges on the true state.
export function settleSurface(el) {
  if (!el?.classList) return;
  if (typeof clearTimeout === 'function') clearTimeout(el.__surfaceTimer);
  el.__surfaceTimer = null;
  el.classList.remove(SURFACE_FORMING_CLASS, SURFACE_LEAVING_CLASS);
  el.style?.removeProperty?.('--dust-ms');
  cancelDust(el);
}

const playSurface = (el, point, { ms, gather, box = null, delayScale = null }) => {
  if (!el?.classList) return false;
  settleSurface(el);
  if (motionReduced()) return false;
  // No point to fly at ⇒ exactly settleSurface (already ran): callers never need their
  // own `if (!point) settleSurface(el)` fallback around surfaceIn/surfaceOut.
  if (!(Number.isFinite(point?.x) && Number.isFinite(point?.y))) return false;
  // The marker goes on BEFORE the measure: the element's own entrance (modalFromIcon,
  // chatSlide*, menuPop) FILLS an icon-sized from-state, so a box read under it is the
  // icon's box — the same trap `modal-measuring` dodges in ui/base.js. Off again if the
  // dust declines, so a surface that never plays it keeps its old CSS entrance.
  el.classList.add(SURFACE_DRIVEN_CLASS);
  if (!surfaceDust(el, point, { ms, gather, box, delayScale })) {
    el.classList.remove(SURFACE_DRIVEN_CLASS);
    return false;
  }
  el.style?.setProperty?.('--dust-ms', `${ms}ms`);
  el.classList.add(gather ? SURFACE_FORMING_CLASS : SURFACE_LEAVING_CLASS);
  el.__surfaceTimer = setTimeout(() => {
    el.__surfaceTimer = null;
    el.classList.remove(SURFACE_FORMING_CLASS, SURFACE_LEAVING_CLASS);
    el.style?.removeProperty?.('--dust-ms');
  }, ms + 60);
  return true;
};

// The surface waits behind its own dust and fades up as the last motes land.
export const surfaceIn = (el, point, { ms = SURFACE_IN_MS, box = null, delayScale = null } = {}) =>
  playSurface(el, point, { ms, gather: true, box, delayScale });
// …and hands over to it at once on the way out. The caller still owns the real
// hide/remove: like leaveThenRemove, the end state never depends on the animation.
// `delayScale` compresses the per-mote stagger — the default sweep makes a window come
// apart in a wave, but on a small surface it is just a thin tail of stragglers.
export const surfaceOut = (el, point, { ms = SURFACE_OUT_MS, box = null, delayScale = null } = {}) =>
  playSurface(el, point, { ms, gather: false, box, delayScale });
