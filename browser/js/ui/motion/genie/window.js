// A window's Sliding flight: hidden at its post, photographed (genie/shot.js) and flown out of or into
// its icon (genie/flight.js), shown again as it lands. A newer open or close supersedes a picture
// still being taken. Without a picture the window's own CSS flight (modalFromIcon/modalToIcon) plays.
import { motionMode, motionMs, motionReduced } from '../motionPrefs.js';
import { snapshot } from './shot.js';
import { playGenie, GENIE_OPEN_MS, GENIE_CLOSE_MS } from './flight.js';
import { genieFacing } from './warp.js';
import { cssAway, holdCopy } from '../surface/leaveCopy.js';

// `genie-hold` stills the box's CSS flight while it is photographed; `genie-driven` also hides it.
export const GENIE_HOLD_CLASS = 'genie-hold';
export const GENIE_DRIVEN_CLASS = 'genie-driven';

// What a box pours out of and back into: the strip of `from` (an element or a client rect) that faces
// it — a selector's underside for a list below it — or `from` whole where the two overlap.
const homeOf = (from, box) => {
  const r = typeof from?.getBoundingClientRect === 'function' ? from.getBoundingClientRect() : from;
  return r && box ? genieFacing(r, box) : r ?? null;
};
// The flight's canvas sits one level over the box it pictures.
const zAbove = (box) => {
  const z = Number.parseInt(globalThis.getComputedStyle?.(box).zIndex, 10);
  return Number.isFinite(z) ? z + 1 : undefined;
};

// Shown as it lands, any fade its fold started run to its end: a fade-in after the picture is gone blinks.
const unveil = (box) => {
  box.classList.remove(GENIE_DRIVEN_CLASS);
  globalThis.getComputedStyle?.(box).opacity;   // the hide's own undoing starts a fade too: flushed, then ended
  for (const a of box.getAnimations?.({ subtree: true }) ?? []) if (a.transitionProperty === 'opacity') a.finish();
};

export const genieEnabled = () => motionMode() === 'slide' && !motionReduced()
  && typeof globalThis.document?.createElement === 'function' && typeof globalThis.Image === 'function';

export function createWindowGenie(boxOf) {
  let generation = 0;
  let cancel = null;
  const unmark = (box) => box?.classList.remove(GENIE_HOLD_CLASS, GENIE_DRIVEN_CLASS);

  const settle = () => {
    generation += 1;
    cancel?.();
    cancel = null;
    unmark(boxOf());
  };

  // `win` is the box's resting client rect, `icon` the opener's. `landed` runs once, as it lands.
  const fly = (win, icon, { opening, zIndex, landed, fallbackMs }) => {
    settle();
    const box = boxOf();
    if (!box) { landed?.(); return; }
    const mine = generation;
    box.classList.add(opening ? GENIE_DRIVEN_CLASS : GENIE_HOLD_CLASS);
    snapshot(box).then((shot) => {
      if (mine !== generation) return;
      if (!shot) {
        unmark(box);
        if (landed) setTimeout(() => { if (mine === generation) landed(); }, fallbackMs);
        return;
      }
      box.classList.add(GENIE_DRIVEN_CLASS);
      box.classList.remove(GENIE_HOLD_CLASS);
      cancel = playGenie(shot, win, icon, {
        opening, zIndex, ms: motionMs(opening ? GENIE_OPEN_MS : GENIE_CLOSE_MS),
        done: () => {
          cancel = null;
          if (mine !== generation) return;
          // Landed, it stays held: dropping the hold would restart its CSS entrance on the spot.
          if (opening) { box.classList.add(GENIE_HOLD_CLASS); unveil(box); }
          landed?.();
        },
      });
    });
  };

  return {
    open: (win, icon, zIndex) => fly(win, icon, { opening: true, zIndex, fallbackMs: 0 }),
    close: (win, icon, zIndex, landed, fallbackMs) => fly(win, icon, { opening: false, zIndex, landed, fallbackMs }),
    settle,
  };
}

// Sliding's entrance for a box already placed (a dropdown list): veiled, photographed, poured out of
// `from` (an element or a client rect) in `ms`, then shown — held, so its own CSS entrance never
// replays. False when Sliding is off; settleGenie() ends it and drops the hold. `onFly` runs as the
// picture starts to move (or at once without one): a fold slides its space open on that beat.
export function genieIn(box, from, { ms = GENIE_OPEN_MS, onFly = null } = {}) {
  settleGenie(box);
  if (!genieEnabled() || typeof box?.getBoundingClientRect !== 'function') return false;
  // Its own CSS entrance off BEFORE the measure: a list's starts at a third smaller (menuFromAnchor).
  box.classList.add(GENIE_DRIVEN_CLASS);
  const win = box.getBoundingClientRect();
  const icon = homeOf(from, win);
  if (!(win.width > 0 && win.height > 0 && icon)) { box.classList.remove(GENIE_DRIVEN_CLASS); return false; }
  const run = box.__genieRun;
  const zIndex = zAbove(box);
  snapshot(box).then((shot) => {
    if (run !== box.__genieRun) return;
    if (!shot) { box.classList.remove(GENIE_DRIVEN_CLASS); onFly?.(); return; }
    box.__genieShot = shot;   // genieAway flies it home without waiting on a second picture
    box.__genieCancel = playGenie(shot, win, icon, {
      opening: true, ms: motionMs(ms), zIndex,
      done: () => {
        box.__genieCancel = null;
        if (run !== box.__genieRun) return;
        box.classList.add(GENIE_HOLD_CLASS);
        unveil(box);
      },
    });
    onFly?.();
  });
  return true;
}

// Sliding's exit for a box still in place: held where it stands while photographed, then hidden and
// poured into `to` (an element, a client rect, or a function giving one) in `ms`; `done` (the caller's
// unmount) runs as it lands, or `fallbackMs` after the box's own CSS exit starts when no picture can be
// made. `beforeFly` runs once the picture is taken, before `to` is read: a caller whose unmount moves
// the target (a docked panel giving its space back) unmounts there, and the box flies to where it lands.
// False, with `done` not run, when Sliding is off.
export function genieOut(box, to, done, { ms = GENIE_CLOSE_MS, fallbackMs = 0, beforeFly = null } = {}) {
  settleGenie(box);
  if (!genieEnabled() || typeof box?.getBoundingClientRect !== 'function') return false;
  box.classList.add(GENIE_HOLD_CLASS);   // still, before the measure
  const win = box.getBoundingClientRect();
  const target = () => homeOf(typeof to === 'function' ? to() : to, win);
  if (!(win.width > 0 && win.height > 0 && target())) { box.classList.remove(GENIE_HOLD_CLASS); return false; }
  const run = box.__genieRun;
  const zIndex = zAbove(box);
  snapshot(box).then((shot) => {
    if (run !== box.__genieRun) return;
    if (!shot) {
      box.classList.remove(GENIE_HOLD_CLASS);
      beforeFly?.();   // the unmount still happens, only unflown
      setTimeout(() => { if (run === box.__genieRun) done?.(); }, fallbackMs);
      return;
    }
    box.classList.add(GENIE_DRIVEN_CLASS);
    box.classList.remove(GENIE_HOLD_CLASS);
    beforeFly?.();
    box.__genieCancel = playGenie(shot, win, target(), {
      opening: false, ms: motionMs(ms), zIndex,
      done: () => {
        box.__genieCancel = null;
        if (run !== box.__genieRun) return;
        unveil(box);
        done?.();
      },
    });
  });
  return true;
}

// The instant exit for a box its caller hides at once (a dropdown list): the picture genieIn took is
// poured into `to` on its own canvas, the box itself already gone. A box opened under another mode has
// none: a fresh one is taken while a copy of it stands in (motion/surface/leaveCopy.js).
export function genieAway(box, to, { ms = GENIE_CLOSE_MS } = {}) {
  if (!genieEnabled() || typeof box?.getBoundingClientRect !== 'function') return false;
  const win = box.getBoundingClientRect();
  const icon = homeOf(to, win);
  if (!(win.width > 0 && win.height > 0 && icon)) return false;
  const zIndex = zAbove(box);
  const fly = (shot) => playGenie(shot, win, icon, { opening: false, ms: motionMs(ms), zIndex });
  const taken = box.__genieShot;
  box.__genieShot = null;   // one flight home: a menu closed without a settle keeps no canvas
  if (taken) { fly(taken); return true; }
  const copy = holdCopy(box);
  copy?.classList.add(GENIE_HOLD_CLASS);
  snapshot(box).then((shot) => { copy?.remove(); if (shot) fly(shot); });
  return true;
}

// Drops a box's genie, a picture still being taken included, and shows it as it stands.
export function settleGenie(box) {
  if (!box?.classList) return;
  box.__genieShot = null;
  box.__genieRun = (box.__genieRun || 0) + 1;
  box.__genieCancel?.();
  box.__genieCancel = null;
  box.classList.remove(GENIE_HOLD_CLASS, GENIE_DRIVEN_CLASS);
}

// A list's pour, quicker than a window's (desktop POPUP_GENIE_MS, ms).
export const MENU_GENIE_MS = 300;
// A point as the short strip a menu pours out of and back into (desktop popupSlide.cpp menuOriginStrip).
export const pointBox = (p) => (p && Number.isFinite(p.x) ? { left: p.x - 8, top: p.y - 1, width: 16, height: 2 } : null);
export const menuIn = (box, from) => genieIn(box, from, { ms: MENU_GENIE_MS });
// A menu hidden at once: Sliding pours its picture into `to`, Transition shrinks a copy of it back; false leaves it to the dust.
export const menuAway = (box, to) => genieAway(box, to, { ms: MENU_GENIE_MS })
  || (motionMode() === 'transition' && !motionReduced() && cssAway(box));
