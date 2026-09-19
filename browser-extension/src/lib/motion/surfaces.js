// ── A surface plays the same scatter, aimed at the control that owns it ─────
// A surface NEVER dusts as clones of itself: its cloud lands on <body>, so a few hundred
// copies of a menu is a few hundred more elements answering to `.action-menu` and kin, and
// everything that queries the page would have to know about a decoration. Flat specks
// carry no identity, and at a 6px grain show all a clone would.
import { motionReduced } from '../motionPrefs.js';
import { disintegrate } from './disintegrate.js';
import { speckPainter } from './painters.js';
import { SURFACE_COLS, SURFACE_DRIVEN_CLASS, SURFACE_FORMING_CLASS, SURFACE_IN_MS,
         SURFACE_LEAVING_CLASS, SURFACE_MOTE_PX, SURFACE_OUT_MS, SURFACE_ROWS } from './surfaceMotion.js';
import { cancelDust, reshapeGrid } from './tiles.js';
const surfaceDust = (el, point, { ms, gather }) => {
  if (typeof document === 'undefined' || !el?.getBoundingClientRect) return false;
  if (!(Number.isFinite(point?.x) && Number.isFinite(point?.y))) return false;
  const r = el.getBoundingClientRect();
  if (!(r.width >= 8 && r.height >= 8)) return false;
  const grid = reshapeGrid(SURFACE_COLS, SURFACE_ROWS, r.width, r.height, SURFACE_MOTE_PX);
  return disintegrate(el, {
    ...grid, gather, toward: point, ms, px: SURFACE_MOTE_PX, toBody: true,
    hostClass: gather ? 'dust-forming' : 'dust-leaving',
    paintTile: speckPainter(el),
  });
};

// Every open/close begins here, so a double-clicked menu converges on the true state.
export function settleSurface(el) {
  if (!el?.classList) return;
  clearTimeout(el.__surfaceTimer);
  el.__surfaceTimer = null;
  el.classList.remove(SURFACE_FORMING_CLASS, SURFACE_LEAVING_CLASS);
  el.style?.removeProperty?.('--dust-ms');
  cancelDust(el);
}

const playSurface = (el, point, { ms, gather }) => {
  if (!el?.classList) return false;
  settleSurface(el);
  if (motionReduced()) return false;
  // The marker goes on BEFORE the measure: the element's own entrance keyframe holds an
  // icon-sized from-state, so a box measured under it is the ICON's.
  el.classList.add(SURFACE_DRIVEN_CLASS);
  if (!surfaceDust(el, point, { ms, gather })) {
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
export const surfaceIn = (el, point, { ms = SURFACE_IN_MS } = {}) =>
  playSurface(el, point, { ms, gather: true });
// …and hands over to it at once on the way out. The caller still owns the real hide/remove
// — the end state never depends on the animation.
export const surfaceOut = (el, point, { ms = SURFACE_OUT_MS } = {}) =>
  playSurface(el, point, { ms, gather: false });
