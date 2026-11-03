// ── A surface plays the same scatter, aimed at the control that owns it ─────
// A surface NEVER dusts as clones of itself: its cloud lands on <body>, so a few hundred
// copies of a menu is a few hundred more elements answering to `.action-menu` and kin, and
// everything that queries the page would have to know about a decoration. Flat specks
// carry no identity, and at a 6px grain show all a clone would.
import { motionMode, motionReduced } from '../prefs/motionPrefs.js';
import { applyGenieVars, clearGenieVars } from './genie/vars.js';
import { snapshot } from './genie/shot.js';
import { genieFacing } from './genie/warp.js';
import { cssAway, holdCopy } from './leaveCopy.js';
import { playGenie, GENIE_OPEN_MS, GENIE_CLOSE_MS } from './genie/flight.js';
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

// ── Sliding: a window is sucked out of its opener and back (genie/), its CSS flight the fallback ──
// The CSS flight's lengths (stGenieIn / stGenieOut, ms).
export const GENIE_IN_MS = 520;
export const GENIE_OUT_MS = 440;
const GENIE_IN_CLASS = 'genie-in';
const GENIE_OUT_CLASS = 'genie-out';
const GENIE_LEAVING_CLASS = 'genie-leaving';
// Stills the box's own flight while it is photographed; `genie-driven` also hides it under the canvas.
const GENIE_HOLD_CLASS = 'genie-hold';
const GENIE_DRIVEN_CLASS = 'genie-driven';
const genieOn = () => motionMode() === 'slide' && !motionReduced();
const rectOf = (from) => (typeof from?.getBoundingClientRect === 'function' ? from.getBoundingClientRect() : from);
// No opener: the box flows into a point at its own centre. An opener above or below it gives its
// facing strip (a selector's underside for its list), as the browser's genie/window.js homeOf.
const iconFor = (box, from) => {
  const r = rectOf(from);
  if (r?.width > 0 && r?.height > 0) return genieFacing(r, box);
  return { left: box.left + box.width / 2 - 12, top: box.top + box.height / 2 - 12, width: 24, height: 24 };
};

// Ends a flight at once: its pending unmount runs now, so a reopen starts from the true state.
export function settleGenie(box) {
  if (!box?.classList) return;
  clearTimeout(box.__genieTimer);
  box.__genieCancel?.();
  const done = box.__genieDone;
  box.__genieTimer = null;
  box.__genieCancel = null;
  box.__genieDone = null;
  box.__genieShot = null;
  box.__genieRun = (box.__genieRun || 0) + 1;
  box.classList.remove(GENIE_IN_CLASS, GENIE_OUT_CLASS, GENIE_HOLD_CLASS, GENIE_DRIVEN_CLASS);
  clearGenieVars(box);
  done?.();
}

// Photographs the box, then flies it; without a picture its CSS flight plays (`cssClass`, `cssMs`).
const flyGenie = (box, from, { opening, cssClass, cssMs, ms }) => {
  // Its own CSS entrance off BEFORE the measure: a list's starts a third smaller (stMenuFromAnchor).
  box.classList.add(opening ? GENIE_DRIVEN_CLASS : GENIE_HOLD_CLASS);
  const rest = box.getBoundingClientRect();
  if (!applyGenieVars(box, rest, rectOf(from) ?? null)) {
    box.classList.remove(GENIE_DRIVEN_CLASS, GENIE_HOLD_CLASS);
    return false;
  }
  const run = box.__genieRun;
  snapshot(box).then((shot) => {
    if (run !== box.__genieRun) return;
    // An opened box stays held: dropping the hold would replay its own CSS entrance (a dropdown's).
    const land = () => {
      box.__genieCancel = null;
      settleGenie(box);
      if (opening && shot) { box.classList.add(GENIE_HOLD_CLASS); box.__genieShot = shot; }
    };
    if (!shot) {
      box.classList.remove(GENIE_HOLD_CLASS, GENIE_DRIVEN_CLASS);
      box.classList.add(cssClass);
      box.__genieTimer = setTimeout(land, cssMs);
      return;
    }
    box.classList.add(GENIE_DRIVEN_CLASS);
    box.classList.remove(GENIE_HOLD_CLASS);
    box.__genieCancel = playGenie(shot, rest, iconFor(rest, from), {
      opening, ms: ms ?? (opening ? GENIE_OPEN_MS : GENIE_CLOSE_MS), done: () => { if (run === box.__genieRun) land(); },
    });
  });
  return true;
};

// The instant exit for a box its caller hides at once (a dropdown list): the picture genieIn took is
// poured into `to` on its own canvas, the box itself already gone. A box opened under another mode has
// none: a fresh one is taken while a copy of it stands in (leaveCopy.js), as the browser's genie/window.js.
export function genieAway(box, to, { ms = GENIE_CLOSE_MS } = {}) {
  if (!genieOn() || !box?.getBoundingClientRect) return false;
  const rest = box.getBoundingClientRect();
  if (!(rest.width > 0 && rest.height > 0)) return false;
  const fly = (shot) => playGenie(shot, rest, iconFor(rest, to), { opening: false, ms });
  const taken = box.__genieShot;
  box.__genieShot = null;   // one flight home: a menu closed without a settle keeps no canvas
  if (taken) { fly(taken); return true; }
  const copy = holdCopy(box);
  copy?.classList.add(GENIE_HOLD_CLASS);
  snapshot(box).then((shot) => { copy?.remove(); if (shot) fly(shot); });
  return true;
}

// `from` is the opener (an element or a client rect); null = the box flows out of its own centre.
// `ms` shortens the flight for a list (dropdownMenu.js MENU_GENIE_MS).
export function genieIn(box, from, { ms } = {}) {
  settleGenie(box);
  if (!genieOn() || !box?.getBoundingClientRect) return false;
  return flyGenie(box, from, { opening: true, cssClass: GENIE_IN_CLASS, cssMs: GENIE_IN_MS + 40, ms });
}

// Flies the box home and runs `done` (the unmount) as it lands; `backdrop` fades and stops taking
// presses meanwhile. False, with `done` not run, when Sliding is off.
export function genieOut(box, from, done, backdrop = null) {
  settleGenie(box);
  if (!genieOn() || !box?.getBoundingClientRect) return false;
  backdrop?.classList?.add(GENIE_LEAVING_CLASS);
  box.__genieDone = () => { backdrop?.classList?.remove(GENIE_LEAVING_CLASS); done?.(); };
  if (!flyGenie(box, from, { opening: false, cssClass: GENIE_OUT_CLASS, cssMs: GENIE_OUT_MS })) settleGenie(box);
  return true;
}

// Transition: the box's own CSS flight out of its opener and back (stGenieIn / stGenieOut), no picture, as
// the browser's modalFromIcon / modalToIcon. False out of Transition; growOut runs `done` as it lands.
const transitionOn = () => motionMode() === 'transition' && !motionReduced();
const grow = (box, from, cssClass, ms, done) => {
  settleGenie(box);
  if (!transitionOn() || !box?.getBoundingClientRect) return false;
  if (!applyGenieVars(box, box.getBoundingClientRect(), rectOf(from) ?? null)) return false;
  box.__genieDone = done ?? null;
  box.classList.add(cssClass);
  box.__genieTimer = setTimeout(() => settleGenie(box), ms);
  return true;
};
export const growIn = (box, from) => grow(box, from, GENIE_IN_CLASS, GENIE_IN_MS + 40);
export const growOut = (box, from, done, backdrop = null) => {
  const flown = grow(box, from, GENIE_OUT_CLASS, GENIE_OUT_MS, () => { backdrop?.classList?.remove(GENIE_LEAVING_CLASS); done?.(); });
  if (flown) backdrop?.classList?.add(GENIE_LEAVING_CLASS);
  return flown;
};

// A menu its caller hides at once: Sliding pours its picture into `to`, Transition shrinks a copy of it back
// (stMenuToAnchor); false leaves it to the dust. Arriving, Sliding pours it out of `from` and Transition keeps
// the menu's own CSS entrance. A point opens out of a short strip at it.
export const MENU_GENIE_MS = 300;
export const pointBox = (p) => (p && Number.isFinite(p.x) ? { left: p.x - 8, top: p.y - 1, width: 16, height: 2 } : null);
export const menuAway = (el, to) => genieAway(el, to, { ms: MENU_GENIE_MS }) || (transitionOn() && cssAway(el));
export const menuIn = (el, from) => genieIn(el, from, { ms: MENU_GENIE_MS });
