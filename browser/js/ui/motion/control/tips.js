import { motionReduced } from '../motionPrefs.js';
import { SURFACE_IN_MS, dockAwayPoint } from '../surface/motion.js';
import { settleSurface, surfaceIn, surfaceOut } from '../surface/surfaces.js';
import { TUNE } from '../tune.js';
// Every cursor-adjacent popup dusts in and out of the control it describes, fast enough
// to be over before a sweep reaches the next one.
export const TIP_DUST_IN_MS = TUNE.TIP_DUST_IN_MS;
export const TIP_DUST_OUT_MS = TUNE.TIP_DUST_OUT_MS;
// One wake delay across surfaces (desktop SnappyTooltipStyle, main.cpp).
export const TIP_SHOW_DELAY_MS = TUNE.TIP_SHOW_DELAY_MS;

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

export function foldDust(el, scope, cls, hiding, dock, { inMs = SURFACE_IN_MS, toggle = null } = {}) {
  const box = motionReduced() ? null : foldBox(el, scope, cls, false, FOLD_INSTANT_CLASS);
  toggle?.();
  const away = box && dockAwayPoint(box, dock);
  (hiding ? surfaceOut : surfaceIn)(el, away || null, { box, ms: hiding ? FOLD_DUST_OUT_MS : inMs, belowChat: true });
}

// Popups shown by a `:hover` rule alone: by the time pointerleave runs the popup is already
// display:none, so the hold class puts the box back for as long as the motes need.
const HOVER_DUST_HOLD_CLASS = 'dust-hold';
const HOVER_DUST_HOLD_MS = TUNE.HOVER_DUST_HOLD_MS;
export function wireHoverDust(host, popup, { inMs = 300, outMs = 200 } = {}) {
  if (!host?.addEventListener || !popup?.classList) return;
  const point = () => (motionReduced() ? null : rectCenter(host));
  host.addEventListener('pointerenter', () => surfaceIn(popup, point(), { ms: inMs }));
  host.addEventListener('pointerleave', () => {
    const p = point();
    if (!p) { settleSurface(popup); return; }
    popup.classList.add(HOVER_DUST_HOLD_CLASS);
    const played = surfaceOut(popup, p, { ms: outMs });
    setTimeout(() => popup.classList.remove(HOVER_DUST_HOLD_CLASS), played ? HOVER_DUST_HOLD_MS : 0);
  });
}
