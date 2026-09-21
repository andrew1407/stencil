import { motionReduced } from '../motionPrefs.js';
import { disintegrate } from '../disintegrate.js';
import { markPaint, speckPainter } from './painters.js';
import { cancelDust, reshapeGrid } from './tiles.js';
import { TUNE } from '../tune.js';
// A control's own mark (a tick, a select's word) comes and goes inside a box that stays
// put: a row's fall-and-fan flight at control scale (desktop controlSwap.hpp parity),
// with no default veil since a checkbox keeps its outline.
export const MARK_IN_MS = TUNE.MARK_IN_MS;
export const MARK_OUT_MS = TUNE.MARK_OUT_MS;
export const MARK_MOTE_PX = TUNE.MARK_MOTE_PX;
export const MARK_DRIFT = TUNE.MARK_DRIFT;        // desktop CHECK_SWAP_SPREAD
// A ceiling well below a window's: a 380px row gridded at 3px would be 1300 motes for a
// 320ms decoration; past it the cell and the speck grow together.
export const MARK_COLS = TUNE.MARK_COLS;
export const MARK_ROWS = TUNE.MARK_ROWS;          // 600 motes
export const MARK_FORMING_CLASS = 'mark-forming';
// The veil a GROUP goes behind while its dust flies and its slot closes.
export const MARK_LEAVING_CLASS = 'mark-leaving';

const markDust = (el, { gather, ms, paint, px, drift, own = true, painter = null }) => {
  if (typeof document === 'undefined' || !el?.getBoundingClientRect) return false;
  if (motionReduced()) return false;
  const r = el.getBoundingClientRect();
  if (!(r.width >= 8 && r.height >= 8)) return false;
  const grid = reshapeGrid(MARK_COLS, MARK_ROWS, r.width, r.height, px);
  return disintegrate(el, {
    ...grid, gather, ms, px, drift, toBody: true, own,
// Arrival is the surface gather, departure its own fall (cloud.js FLIGHTS.fall; the
// desktop's Sweep::Fall) — visible from the first frame either way.
    hostClass: gather ? 'dust-forming' : 'dust-falling',
    flight: gather ? 'surfaceGather' : 'fall',
    paintTile: painter || speckPainter(el, paint || markPaint(el)),
  });
};

const holdMarkVeil = (el, veil, ms) => {
  el.__markVeil = veil;
  el.classList.add(veil);
  el.style?.setProperty?.('--mark-ms', `${ms}ms`);
  if (typeof setTimeout === 'function')
    el.__markTimer = setTimeout(() => {
      el.__markTimer = null;
      el.classList.remove(veil);
      el.__markVeil = null;
      el.style?.removeProperty?.('--mark-ms');
    }, ms + 40);
};

export function settleMark(el) {
  if (!el?.classList) return;
  if (typeof clearTimeout === 'function') clearTimeout(el.__markTimer);
  el.__markTimer = null;
  if (el.__markVeil) el.classList.remove(el.__markVeil);
  el.__markVeil = null;
  el.style?.removeProperty?.('--mark-ms');
  cancelDust(el);
}

// The caller still owns the state change: the cloud is a copy on <body> and the end state
// never waits for it. `veil` (a group under revealControls) hides the real thing at once.
export function markOut(el, { ms = MARK_OUT_MS, paint = null, px = MARK_MOTE_PX,
                              drift = MARK_DRIFT, own = true, painter = null, veil = null } = {}) {
  if (own) settleMark(el);
  const played = markDust(el, { gather: false, ms, paint, px, drift, own, painter });
  if (played && veil && el.classList) holdMarkVeil(el, veil, ms);
  return played;
}

// `veil` holds the real mark back while the motes gather; null when it is invisible anyway.
export function markIn(el, { ms = MARK_IN_MS, paint = null, px = MARK_MOTE_PX,
                             drift = MARK_DRIFT, veil = MARK_FORMING_CLASS, painter = null } = {}) {
  settleMark(el);
  const played = markDust(el, { gather: true, ms, paint, px, drift, painter });
  if (!played || !veil || !el.classList) return played;
  holdMarkVeil(el, veil, ms);
  return true;
}

