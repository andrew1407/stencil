// A hover tip's flights: the dust it gathers from, Sliding's pour out of its control and back, Transition's grow
// and shrink, and the fold a toolbar strip pours through. Clocks from common/config/motion.json via tune.js.
// Desktop twin: support/tip/tipFlight.hpp and tipFlight.cpp.
import { motionMode, motionMs, motionReduced } from '../motionPrefs.js';
import { SURFACE_DRIVEN_CLASS, SURFACE_IN_MS, dockAwayPoint, dockEdgeLane } from '../surface/motion.js';
import { settleSurface, surfaceIn, surfaceOut } from '../surface/surfaces.js';
import { TUNE } from '../tune.js';
import { GENIE_DRIVEN_CLASS, genieEnabled, genieIn, genieOut, genieAway, pointBox } from '../genie/window.js';
import { flashLanding } from '../enterLeave.js';
// Every cursor-adjacent popup dusts in and out of the control it describes, fast enough
// to be over before a sweep reaches the next one.
export const TIP_DUST_IN_MS = TUNE.TIP_DUST_IN_MS;
export const TIP_DUST_OUT_MS = TUNE.TIP_DUST_OUT_MS;
// One wake delay across surfaces (desktop SnappyTooltipStyle, main.cpp).
export const TIP_SHOW_DELAY_MS = TUNE.TIP_SHOW_DELAY_MS;

// Where no particles fly: Sliding pours the tip out of (and back into) its point, Transition grows it there
// and shrinks a copy back. In returns its run (ms, 0 = none); out is called while still shown.
export const TIP_GROW_CLASS = 'tip-grow-in';
export const TIP_SHRINK_CLASS = 'tip-shrink-out';
// The tip's clocks (common/config/motion.json): the grow and shrink (animations/overlay/overlays.css tipGrowIn /
// tipShrinkOut), and Sliding's pour, half again as long as the grow (user decision; desktop tipFlight.hpp reads the same).
const TIP_GROW_MS = TUNE.TIP_GROW_MS;
const TIP_SHRINK_MS = TUNE.TIP_SHRINK_MS;
const TIP_SLIDE_IN_MS = TUNE.TIP_SLIDE_IN_MS;
const TIP_SLIDE_OUT_MS = TUNE.TIP_SLIDE_OUT_MS;
const isTransitionTip = () => motionMode() === 'transition' && !motionReduced();
// Transition's grow alone, for a tip that flies no other way (the canvas readout); false in any other mode.
export const tipGrowIn = (el) => {
  if (!el?.classList || !isTransitionTip()) return false;
  el.classList.remove(TIP_SHRINK_CLASS);
  flashLanding(el, TIP_GROW_CLASS, TIP_GROW_MS);
  return true;
};
// `landed` runs once the tip is really seen: the pour's own landing (after its snapshot), or the grow's end.
export const tipFallbackIn = (el, point, landed = null) => {
  if (!el?.classList || !point || motionReduced()) return false;
  // Its own fade steps aside first: the pour photographs the tip at rest.
  el.classList.add(SURFACE_DRIVEN_CLASS);
  // On the tip's own clock, never a menu's 420ms: a tip is over before a sweep reaches the next control.
  if (genieIn(el, pointBox(point), { ms: TIP_SLIDE_IN_MS, landed })) return TIP_SLIDE_IN_MS;
  if (!tipGrowIn(el)) return 0;
  if (landed) setTimeout(landed, motionMs(TIP_GROW_MS));
  return TIP_GROW_MS;
};
// Transition shrinks the tip itself, so it stays laid out while it hides: a copy loses the id its look hangs on.
const TIP_OUT = Object.freeze({
  transition: (el) => { el.classList.remove(TIP_GROW_CLASS); flashLanding(el, TIP_SHRINK_CLASS, TIP_SHRINK_MS); return true; },
});
const pourOut = (el, point) => genieAway(el, pointBox(point), { ms: TIP_SLIDE_OUT_MS });
export const tipFallbackOut = (el, point) =>
  !!el?.classList && !!point && !motionReduced() && (TIP_OUT[motionMode()] ?? pourOut)(el, point);
// For a tip hidden with display:none (the canvas readout): a copy by class and markup stands where it was
// and shrinks away. False outside Transition.
export const tipCopyAway = (el) => {
  const r = el?.getBoundingClientRect?.();
  if (!isTransitionTip() || !r || !(r.width > 0 && r.height > 0) || typeof document === 'undefined') return false;
  const copy = document.createElement('div');
  copy.className = `${el.className} ${TIP_SHRINK_CLASS}`.replace(TIP_GROW_CLASS, '');
  copy.innerHTML = el.innerHTML;
  Object.assign(copy.style, { position: 'fixed', left: `${r.left}px`, top: `${r.top}px`, width: `${r.width}px`,
    height: `${r.height}px`, margin: '0', display: 'block', pointerEvents: 'none' });
  document.body.appendChild(copy);
  setTimeout(() => copy.remove(), motionMs(TIP_SHRINK_MS) + 60);
  return true;
};

export const rectCenter = (elOrRect) => {
  const r = typeof elOrRect?.getBoundingClientRect === 'function'
    ? elOrRect.getBoundingClientRect() : elOrRect;
  if (!r || !(r.width > 0 || r.height > 0)) return null;
  return { x: r.left + r.width / 2, y: r.top + r.height / 2 };
};

// The box a folding surface is ABOUT to take: run the target state with the fold's
// transitions off (`instant`), read the rect, revert. Null when not worth dusting.
export function foldBox(el, scope, cls, on, instant) {
  if (!el?.getBoundingClientRect || !scope?.classList) return null;
  const had = scope.classList.contains(cls);
  scope.classList.add(instant);
  scope.classList.toggle(cls, on);
  const r = el.getBoundingClientRect();
  scope.classList.toggle(cls, had);
  el.getBoundingClientRect();   // flush the revert while the transitions are still off
  scope.classList.remove(instant);
  return r.width >= 8 && r.height >= 8
    ? { left: r.left, top: r.top, width: r.width, height: r.height } : null;
}
export const FOLD_INSTANT_CLASS = 'fold-instant';
// 1.5x SURFACE_OUT_MS, as --fold-out-ms is of --fold-ms in animations/collapse.css: with no
// icon to shrink into, the fold itself is the only thing that reads as leaving.
export const FOLD_DUST_OUT_MS = TUNE.FOLD_DUST_OUT_MS;
// The top toolbar's own fold, 1.3x brisker than those (user request); CSS twin --fold-ms / --fold-out-ms.
export const CONTROLS_DUST_IN_MS = TUNE.CONTROLS_DUST_IN_MS;
export const CONTROLS_DUST_OUT_MS = TUNE.CONTROLS_DUST_OUT_MS;

// What foldDust returns when Sliding poured the fold (no dust flew, and nothing is left to slide).
export const FOLD_POURED = 'poured';
// The box the cloud flew over, or null: `box` one the caller read open, `picture` keeps the surface as the picture
// its grains leave, `edge` streams them through the docked edge, `home` makes Sliding pour it from the toggle.
export function foldDust(el, scope, cls, hiding, dock, { inMs = SURFACE_IN_MS, outMs = FOLD_DUST_OUT_MS,
                                                         toggle = null, picture = false, box: open = null,
                                                         edge = false, home = null, pourMs = 0 } = {}) {
  if (home && genieEnabled() && pourFold(el, scope, cls, hiding, toggle, home, pourMs)) return FOLD_POURED;
  const box = motionReduced() ? null : open || foldBox(el, scope, cls, false, FOLD_INSTANT_CLASS);
  toggle?.();
  const away = box && (edge ? dockEdgeLane : dockAwayPoint)(box, dock);
  const flew = (hiding ? surfaceOut : surfaceIn)(el, away || null,
    { box, ms: hiding ? outMs : inMs, belowChat: true, picture });
  return flew ? box : null;
}

// A fold's space beside its flight: closing, it waits for the picture's far edge to move (the warp's LAG through the
// close's in-out sine, a time share); the flight outlasts the slide to cover the landing (desktop GenieGhost.hpp spaceClock).
export const SPACE_CLOSE_WAIT = 0.4;
export const SPACE_TAIL_MS = 40;
// Every slide the fold just set off, anywhere on the page (still pending: born this frame).
const retimeClose = (ms) => {
  for (const a of globalThis.document?.getAnimations?.() ?? []) {
    if (!a.pending || !('transitionProperty' in a) || a.transitionProperty === 'opacity' || a.transitionProperty === 'visibility') continue;
    a.effect?.updateTiming?.({ delay: ms * SPACE_CLOSE_WAIT, duration: ms * (1 - SPACE_CLOSE_WAIT) });
  }
};

// Hidden while its picture flies, the fold slides its space on its own CSS transition. Opening, it is laid
// out open with that transition off for the picture, put back shut, and only then slides open as it flies.
const pourFold = (el, scope, cls, hiding, toggle, home, ms) => {
  const opts = ms > 0 ? { ms: ms + SPACE_TAIL_MS } : {};
  const at = () => (typeof home === 'function' ? home() : home);
  if (hiding) return genieOut(el, at, null, { ...opts, beforeFly: () => { toggle?.(); if (ms > 0) retimeClose(ms); } });
  scope.classList.add(FOLD_INSTANT_CLASS);
  toggle?.();
  el.getBoundingClientRect();
  let flying = false;
  const fly = () => { flying = true; el.classList.add(GENIE_DRIVEN_CLASS); toggle?.(); };
  const poured = genieIn(el, at(), { ...opts, onFly: fly });
  if (poured && !flying) {   // shut again, and seen shut, until the picture moves
    scope.classList.toggle(cls, !hiding);
    el.classList.remove(GENIE_DRIVEN_CLASS);
  }
  el.getBoundingClientRect();
  scope.classList.remove(FOLD_INSTANT_CLASS);
  return poured;
};

// Popups shown by a `:hover` rule alone: by the time pointerleave runs the popup is already
// display:none, so the hold class puts the box back for as long as the motes need.
const HOVER_DUST_HOLD_CLASS = 'dust-hold';
const HOVER_DUST_HOLD_MS = TUNE.HOVER_DUST_HOLD_MS;
export function wireHoverDust(host, popup, { inMs = 300, outMs = 200 } = {}) {
  if (!host?.addEventListener || !popup?.classList) return;
  const point = () => (motionReduced() ? null : rectCenter(host));
  host.addEventListener('pointerenter', () => surfaceIn(popup, point(), { ms: inMs }) || tipFallbackIn(popup, point()));
  host.addEventListener('pointerleave', () => {
    const p = point();
    if (!p) { settleSurface(popup); return; }
    popup.classList.add(HOVER_DUST_HOLD_CLASS);
    const played = surfaceOut(popup, p, { ms: outMs }) || tipFallbackOut(popup, p);
    setTimeout(() => popup.classList.remove(HOVER_DUST_HOLD_CLASS), played ? motionMs(HOVER_DUST_HOLD_MS) : 0);
  });
}
