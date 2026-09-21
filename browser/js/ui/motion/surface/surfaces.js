import { motionReduced } from '../motionPrefs.js';
import { disintegrate } from '../disintegrate.js';
import { speckPainter } from './painters.js';
import { SURFACE_COLS, SURFACE_DRIVEN_CLASS, SURFACE_FORMING_CLASS, SURFACE_IN_MS, SURFACE_LEAVING_CLASS, SURFACE_MOTE_PX, SURFACE_OUT_MS, SURFACE_ROWS } from './motion.js';
import { cancelDust, reshapeGrid } from './tiles.js';

// A surface NEVER dusts as clones of itself: a cloud on <body> of copies of a menu is
// hundreds more elements answering to `.accent-dd-menu`, `.ctx-sub`, `#chat-…` for every
// query on the page. Flat specks carry no identity.
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

// Drop whatever a surface has in flight, leaving the end state; every open/close begins here.
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
// No point to fly at ⇒ exactly settleSurface (already ran).
  if (!(Number.isFinite(point?.x) && Number.isFinite(point?.y))) return false;
// The marker goes on BEFORE the measure: the element's own CSS entrance fills an
// icon-sized from-state (the `modal-measuring` trap in ui/base.js). Off again if the dust declines.
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

export const surfaceIn = (el, point, { ms = SURFACE_IN_MS, box = null, delayScale = null } = {}) =>
  playSurface(el, point, { ms, gather: true, box, delayScale });
// The caller still owns the hide/remove: the end state never depends on the animation.
// `delayScale` compresses the stagger for a small surface.
export const surfaceOut = (el, point, { ms = SURFACE_OUT_MS, box = null, delayScale = null } = {}) =>
  playSurface(el, point, { ms, gather: false, box, delayScale });
