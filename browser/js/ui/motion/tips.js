import { motionReduced } from '../motionPrefs.js';
import { SURFACE_IN_MS, dockAwayPoint } from './surfaceMotion.js';
import { settleSurface, surfaceIn, surfaceOut } from './surfaces.js';
import { TUNE } from './tune.js';
// ── Hover tips / preview popups: one shared clock and origin ────────────────
// Every cursor-adjacent popup (the control tooltip, the Alt-hover export preview, the
// chat gear tip, the projects thumb zoom) dusts in and out of the control it describes,
// fast enough to be over before a sweep reaches the next one.
export const TIP_DUST_IN_MS = TUNE.TIP_DUST_IN_MS;
export const TIP_DUST_OUT_MS = TUNE.TIP_DUST_OUT_MS;
// …and wakes on one delay across surfaces (desktop SnappyTooltipStyle, main.cpp).
export const TIP_SHOW_DELAY_MS = TUNE.TIP_SHOW_DELAY_MS;

// The centre of an element (or of a rect): the point a popup's dust belongs to.
// Null for a detached/unmeasurable owner — the flight then settles instead.
export const rectCenter = (elOrRect) => {
  const r = typeof elOrRect?.getBoundingClientRect === 'function'
    ? elOrRect.getBoundingClientRect() : elOrRect;
  if (!r || !(r.width > 0 || r.height > 0)) return null;
  return { x: r.left + r.width / 2, y: r.top + r.height / 2 };
};

// ── Folding surfaces: the box one is ABOUT to take ─────────────────────────
// A folding surface (tool rows, points panel) is still at its collapsed box when its
// toggle flips, so this measures the box it will take: run the target state with the
// fold's transitions off (`instant`), read the rect, revert. `scope` carries the state
// class, `el` is measured. Null when there is nothing worth dusting.
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
// The class that switches those transitions off for the read (animations/collapse.css).
export const FOLD_INSTANT_CLASS = 'fold-instant';
// A fold COLLAPSING is the slower half — the opposite of every other surface, and the
// reason it has its own exit clock: with no icon to shrink into, the fold itself is the
// only thing that reads as the menu leaving, so a brisk exit registered as a snap.
// 1.5x SURFACE_OUT_MS, matching --fold-out-ms against --fold-ms in animations/collapse.css.
export const FOLD_DUST_OUT_MS = TUNE.FOLD_DUST_OUT_MS;   // 1.5x SURFACE_OUT_MS, as the CSS fold's --fold-out-ms is of --fold-ms

// The whole fold-with-dust ritual (toolbar rows, points panel): measure the SHOWN box
// before the fold runs (foldBox), let `toggle` flip the fold class, then stream the
// sand past the `dock` edge — collapsing rides the fold's own slower clock.
export function foldDust(el, scope, cls, hiding, dock, { inMs = SURFACE_IN_MS, toggle = null } = {}) {
  const box = motionReduced() ? null : foldBox(el, scope, cls, false, FOLD_INSTANT_CLASS);
  toggle?.();
  const away = box && dockAwayPoint(box, dock);
  (hiding ? surfaceOut : surfaceIn)(el, away || null, { box, ms: hiding ? FOLD_DUST_OUT_MS : inMs });
}

// ── Hover popups: the two whose visibility is pure CSS ─────────────────────
// The toolbar's hints bubble and the install menu are shown by a `:hover` rule alone,
// with no JS open/close to hang a flight off. They are surfaces all the same, so they
// take the same sand — the only difference is the EXIT: by the time pointerleave runs,
// `:hover` is gone and the popup is already display:none, so there is nothing left to
// copy. The hold class puts the box back for exactly as long as the motes need.
const HOVER_DUST_HOLD_CLASS = 'dust-hold';
const HOVER_DUST_HOLD_MS = TUNE.HOVER_DUST_HOLD_MS;
// `anchor` is what the sand flies out of, defaulting to the hover host — a host that WRAPS
// its popup needs to name its trigger instead, or the box it measures covers the open popup
// and the motes form out of the middle of their own cloud (installButton.js).
export function wireHoverDust(host, popup, { inMs = 300, outMs = 200, anchor = host } = {}) {
  if (!host?.addEventListener || !popup?.classList) return;
  const point = () => (motionReduced() ? null : rectCenter(anchor));
  host.addEventListener('pointerenter', () => surfaceIn(popup, point(), { ms: inMs }));
  host.addEventListener('pointerleave', () => {
    const p = point();
    if (!p) { settleSurface(popup); return; }
    popup.classList.add(HOVER_DUST_HOLD_CLASS);
    const played = surfaceOut(popup, p, { ms: outMs });
    setTimeout(() => popup.classList.remove(HOVER_DUST_HOLD_CLASS), played ? HOVER_DUST_HOLD_MS : 0);
  });
}
