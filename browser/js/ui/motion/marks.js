import { motionReduced } from '../motionPrefs.js';
import { disintegrate } from './disintegrate.js';
import { markPaint, speckPainter } from './painters.js';
import { cancelDust, reshapeGrid } from './tiles.js';
import { TUNE } from './tune.js';
// ── A control's own MARK: the same sand, at control scale ───────────────────
// A tick, a select's chosen word, a toggle-revealed row: marks come and go inside a
// control whose box stays put. A row's fall-and-fan flight (tileMotion; desktop
// controlSwap.hpp parity) with the throw and grain scaled down — and no default veil,
// since a checkbox keeps its outline while only its fill goes.
export const MARK_IN_MS = TUNE.MARK_IN_MS;
export const MARK_OUT_MS = TUNE.MARK_OUT_MS;
export const MARK_MOTE_PX = TUNE.MARK_MOTE_PX;
export const MARK_DRIFT = TUNE.MARK_DRIFT;        // desktop kCheckSwapSpread
// …under a ceiling of its own, well below a window's: a 15px indicator wants every mote
// the grain gives it (25 of them), but the f(x,y) row is 380px wide and gridded at the
// same 3px would have built over 1300 nodes for a 320ms decoration. Past the ceiling the
// cell grows and the speck grows with it, which is what keeps the grain honest.
export const MARK_COLS = TUNE.MARK_COLS;
export const MARK_ROWS = TUNE.MARK_ROWS;          // 600 motes
export const MARK_FORMING_CLASS = 'mark-forming';
// …and the veil a GROUP goes behind: hidden while its dust flies and its slot closes,
// so the buttons are never seen squeezing shut.
export const MARK_LEAVING_CLASS = 'mark-leaving';

// `painter` is a ready per-cell painter (a group's, see groupPainter); `paint` a colour
// recipe for the default speck painter. The painter wins.
const markDust = (el, { gather, ms, paint, px, drift, own = true, painter = null }) => {
  if (typeof document === 'undefined' || !el?.getBoundingClientRect) return false;
  if (motionReduced()) return false;
  const r = el.getBoundingClientRect();
  if (!(r.width >= 8 && r.height >= 8)) return false;
  const grid = reshapeGrid(MARK_COLS, MARK_ROWS, r.width, r.height, px);
  return disintegrate(el, {
    ...grid, gather, ms, px, drift, toBody: true, own,
    // Visible from the first frame either way — a mark's motes ARE the mark. Arrival is
    // the surface gather; departure its own fall (dustCloud.js FLIGHTS.fall — the
    // desktop's Sweep::Fall). The surface leave was tried and read as a flick (user report).
    hostClass: gather ? 'dust-forming' : 'dust-falling',
    flight: gather ? 'surfaceGather' : 'fall',
    paintTile: painter || speckPainter(el, paint || markPaint(el)),
  });
};

// Hold a veil on `el` for the length of a flight, then lift it (markIn / markOut).
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

// Drop whatever `el` has in flight, veil included, leaving the end state untouched.
export function settleMark(el) {
  if (!el?.classList) return;
  if (typeof clearTimeout === 'function') clearTimeout(el.__markTimer);
  el.__markTimer = null;
  if (el.__markVeil) el.classList.remove(el.__markVeil);
  el.__markVeil = null;
  el.style?.removeProperty?.('--mark-ms');
  cancelDust(el);
}

// The mark comes apart. The caller still owns the real state change — like surfaceOut,
// the cloud is a copy on <body> and the end state never waits for it.
// `veil` (opt-in: a group leaving under revealControls) hides the real thing the moment
// its motes set off, so the dust IS it going. A bare mark keeps its box visible.
export function markOut(el, { ms = MARK_OUT_MS, paint = null, px = MARK_MOTE_PX,
                              drift = MARK_DRIFT, own = true, painter = null, veil = null } = {}) {
  if (own) settleMark(el);
  const played = markDust(el, { gather: false, ms, paint, px, drift, own, painter });
  if (played && veil && el.classList) holdMarkVeil(el, veil, ms);
  return played;
}

// …and forms. `veil` is the class that holds the real mark back while the motes gather
// (they ARE the mark forming); null for a mark that is already invisible on its own.
export function markIn(el, { ms = MARK_IN_MS, paint = null, px = MARK_MOTE_PX,
                             drift = MARK_DRIFT, veil = MARK_FORMING_CLASS, painter = null } = {}) {
  settleMark(el);
  const played = markDust(el, { gather: true, ms, paint, px, drift, painter });
  if (!played || !veil || !el.classList) return played;
  holdMarkVeil(el, veil, ms);
  return true;
}

// Show or hide a group of controls another control governs — the f(x,y) inputs, the
// custom page's W/H boxes — with that sand, while the space it takes in the row also
// opens or closes smoothly, so neighbours don't jump the instant it toggles.
// `display: 'block'` collapses HEIGHT instead of width; everything else is a flex row.
// A group's slot is a wider move than a single mark and reads as a snap at the mark's
