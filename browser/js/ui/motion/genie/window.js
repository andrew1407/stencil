// A window's Sliding flight: hidden at its post, photographed (genie/shot.js) and flown out of or into
// its icon (genie/flight.js), shown again as it lands. A newer open or close supersedes a picture
// still being taken. Without a picture the window's own CSS flight (modalFromIcon/modalToIcon) plays.
import { motionMode, motionMs, motionReduced } from '../motionPrefs.js';
import { snapshot } from './shot.js';
import { playGenie, GENIE_OPEN_MS, GENIE_CLOSE_MS } from './flight.js';
import { genieFacing } from './warp.js';
import { cssAway, holdCopy } from '../surface/leaveCopy.js';

const runs = new WeakMap();         // box → its latest flight; a picture taken for an older one is dropped
const shots = new WeakMap();        // box → the picture genieIn took, kept for genieAway
const cancels = new WeakMap();      // box → the cancel of the flight on its canvas
const growTimers = new WeakMap();   // box → the timer ending its Transition grow (menuOpen)

// `genie-hold` stills the box's CSS flight while it is photographed; `genie-driven` also hides it.
export const GENIE_HOLD_CLASS = 'genie-hold';
export const GENIE_DRIVEN_CLASS = 'genie-driven';

// What a box pours out of and back into: the strip of `from` (an element or a client rect) that faces
// it — a selector's underside for a list below it — or `from` whole where the two overlap.
const homeOf = (from, box) => {
  const r = typeof from?.getBoundingClientRect === 'function' ? from.getBoundingClientRect() : from;
  return r && box ? genieFacing(r, box) : r ?? null;
};
// The flight's canvas lives on <body>: it must outrank the box's outermost stacking context (the chat panel's 90000).
const zAbove = (box) => {
  let top = Number.NaN;
  for (let el = box; el?.nodeType === 1; el = el.parentElement) {
    const z = Number.parseInt(globalThis.getComputedStyle?.(el).zIndex, 10);
    if (Number.isFinite(z) && !(z <= top)) top = z;
  }
  return Number.isFinite(top) ? top + 1 : undefined;
};

// Shown as it lands, any fade its fold started run to its end: a fade-in after the picture is gone blinks.
const unveil = (box) => {
  box.classList.remove(GENIE_DRIVEN_CLASS);
  globalThis.getComputedStyle?.(box).opacity;   // the hide's own undoing starts a fade too: flushed, then ended
  for (const a of box.getAnimations?.({ subtree: true }) ?? []) if (a.transitionProperty === 'opacity') a.finish();
};

// Measured at rest: a transform transition still running (a tip's own exit scale) lands the picture mid-way.
const TRANSFORMS = new Set(['transform', 'scale', 'translate', 'rotate']);
export const settleTransforms = (box) => {
  for (const a of box?.getAnimations?.() ?? []) if (TRANSFORMS.has(a.transitionProperty)) a.finish();
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

// Sliding's entrance for a box already placed (a list): photographed, poured out of `from` (an element or a client
// rect) and shown held, so its CSS entrance never replays; `onFly` runs as the picture moves, `landed` once it shows.
// `keep` holds the picture for genieAway's flight home; false where the exit takes its own (a canvas, a strip).
export function genieIn(box, from, { ms = GENIE_OPEN_MS, onFly = null, clip = null, landed = null, keep = true, follow = null } = {}) {
  settleGenie(box);
  if (!genieEnabled() || typeof box?.getBoundingClientRect !== 'function') return false;
  // Its own CSS entrance off BEFORE the measure: a list's starts at a third smaller (menuFromAnchor).
  box.classList.add(GENIE_DRIVEN_CLASS);
  settleTransforms(box);
  const win = box.getBoundingClientRect();
  const icon = homeOf(from, win);
  if (!(win.width > 0 && win.height > 0 && icon)) { box.classList.remove(GENIE_DRIVEN_CLASS); return false; }
  const run = runs.get(box);
  const zIndex = zAbove(box);
  snapshot(box).then((shot) => {
    if (run !== runs.get(box)) return;
    if (!shot) { box.classList.remove(GENIE_DRIVEN_CLASS); onFly?.(); landed?.(); return; }
    if (keep) shots.set(box, shot);   // genieAway flies it home without waiting on a second picture
    cancels.set(box, playGenie(shot, win, icon, {
      opening: true, ms: motionMs(ms), zIndex, clip, follow,
      done: () => {
        cancels.delete(box);
        if (run !== runs.get(box)) return;
        box.classList.add(GENIE_HOLD_CLASS);
        unveil(box);
        landed?.();
      },
    }));
    onFly?.();
  });
  return true;
}

// Sliding's exit for a box still in place: photographed, hidden and poured into `to` (an element, a rect or a
// function of one); `done` runs on landing, or `fallbackMs` later without a picture; `beforeFly` before `to` is read.
export function genieOut(box, to, done, { ms = GENIE_CLOSE_MS, fallbackMs = 0, beforeFly = null } = {}) {
  settleGenie(box);
  if (!genieEnabled() || typeof box?.getBoundingClientRect !== 'function') return false;
  box.classList.add(GENIE_HOLD_CLASS);   // still, before the measure
  const win = box.getBoundingClientRect();
  const target = () => homeOf(typeof to === 'function' ? to() : to, win);
  if (!(win.width > 0 && win.height > 0 && target())) { box.classList.remove(GENIE_HOLD_CLASS); return false; }
  const run = runs.get(box);
  const zIndex = zAbove(box);
  snapshot(box).then((shot) => {
    if (run !== runs.get(box)) return;
    if (!shot) {
      box.classList.remove(GENIE_HOLD_CLASS);
      beforeFly?.();   // the unmount still happens, only unflown
      setTimeout(() => { if (run === runs.get(box)) done?.(); }, fallbackMs);
      return;
    }
    box.classList.add(GENIE_DRIVEN_CLASS);
    box.classList.remove(GENIE_HOLD_CLASS);
    beforeFly?.();
    cancels.set(box, playGenie(shot, win, target(), {
      opening: false, ms: motionMs(ms), zIndex,
      done: () => {
        cancels.delete(box);
        if (run !== runs.get(box)) return;
        unveil(box);
        done?.();
      },
    }));
  });
  return true;
}

// The exit for a box its caller hides at once (a list): the picture genieIn took pours into `to` on its own canvas;
// without one a fresh picture is taken while a copy stands in (motion/surface/leaveCopy.js).
export function genieAway(box, to, { ms = GENIE_CLOSE_MS, clip = null } = {}) {
  if (!genieEnabled() || typeof box?.getBoundingClientRect !== 'function') return false;
  const win = box.getBoundingClientRect();
  const icon = homeOf(to, win);
  if (!(win.width > 0 && win.height > 0 && icon)) return false;
  const zIndex = zAbove(box);
  const fly = (shot) => playGenie(shot, win, icon, { opening: false, ms: motionMs(ms), zIndex, clip });
  const taken = shots.get(box);
  shots.delete(box);   // one flight home: a menu closed without a settle keeps no canvas
  if (taken) { fly(taken); return true; }
  const copy = holdCopy(box);
  copy?.classList.add(GENIE_HOLD_CLASS);
  snapshot(box).then((shot) => { copy?.remove(); if (shot) fly(shot); });
  return true;
}

// A picture already taken (a canvas's own pixels) poured from `win` into `to` on its own canvas; 51 stands over the view.
export function genieShotAway(shot, win, to, { ms = GENIE_CLOSE_MS, clip = null, zIndex = 51, follow = null } = {}) {
  const icon = genieEnabled() && shot && win?.width > 0 && win?.height > 0 ? homeOf(to, win) : null;
  if (icon) playGenie(shot, win, icon, { opening: false, ms: motionMs(ms), zIndex, clip, follow });
  return !!icon;
}

// Drops a box's genie, a picture still being taken included, and shows it as it stands.
export function settleGenie(box) {
  if (!box?.classList) return;
  shots.delete(box);
  runs.set(box, (runs.get(box) ?? 0) + 1);
  cancels.get(box)?.();
  cancels.delete(box);
  box.classList.remove(GENIE_HOLD_CLASS, GENIE_DRIVEN_CLASS);
}

// A list's pour, ms: quicker than a window's 560, slow enough to read as a pour (desktop POPUP_GENIE_MS).
export const MENU_GENIE_MS = 420;
// A point as the short strip a menu pours out of and back into (desktop popupSlide.cpp menuOriginStrip).
export const pointBox = (p) => (p && Number.isFinite(p.x) ? { left: p.x - 8, top: p.y - 1, width: 16, height: 2 } : null);
export const menuIn = (box, from) => genieIn(box, from, { ms: MENU_GENIE_MS });
// A menu with no CSS entrance of its own: Sliding pours it out of `from`, Transition grows it out of that point
// (animations/overlay/overlays.css .menu-grow-in); false leaves it to the dust. It appeared at once (user report).
export const MENU_GROW_CLASS = 'menu-grow-in';
const MENU_GROW_MS = 260;
export const menuOpen = (box, from) => {
  if (menuIn(box, from)) return true;
  if (motionMode() !== 'transition' || motionReduced() || !box?.classList) return false;
  const b = box.getBoundingClientRect?.();
  const f = typeof from?.getBoundingClientRect === 'function' ? from.getBoundingClientRect() : from;
  if (b && f && Number.isFinite(f.left)) {
    const x = Math.min(Math.max(f.left + (f.width || 0) / 2 - b.left, 0), b.width);
    const y = Math.min(Math.max(f.top + (f.height || 0) / 2 - b.top, 0), b.height);
    box.style.transformOrigin = `${x}px ${y}px`;
  }
  clearTimeout(growTimers.get(box));
  box.classList.remove(MENU_GROW_CLASS);
  void box.offsetWidth;   // a re-open restarts the grow
  box.classList.add(MENU_GROW_CLASS);
  growTimers.set(box, setTimeout(() => { box.classList.remove(MENU_GROW_CLASS); box.style.transformOrigin = ''; }, MENU_GROW_MS + 40));
  return true;
};
// A menu hidden at once: Sliding pours its picture into `to`, Transition shrinks a copy of it back; false leaves it to the dust.
export const menuAway = (box, to) => genieAway(box, to, { ms: MENU_GENIE_MS })
  || (motionMode() === 'transition' && !motionReduced() && cssAway(box));
