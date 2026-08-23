// ── Dropdown menus that escape their container ──────────────────────────────
// An open `.accent-dd` menu is moved to <body> (containers clip overflow) and placed by
// the same rule as the toolbar's mini-modals (popover.js popoverPosition): under the
// trigger, flipped above on overflow, clamped to the viewport, capped to the available
// room. hide() puts it back, so the markup a component owns stays its own.
// Callers: while a menu is open it is NOT inside the component — an outside-press check
// must test the menu as well as the trigger.

import { popoverPosition } from './popover.js';
import { surfaceIn, surfaceOut, settleSurface, motionReduced,
         SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS } from './motion.js';

const GAP = 4;        // between trigger and menu
const MARGIN = 8;     // minimum distance to a viewport edge
const MAX_H = 280;    // the .accent-dd-menu cap, respected while fitting to the window
// The list is sand, like every other surface in the app (js/ui/motion.js): it forms
// from motes streaming out of its trigger and comes apart into motes pouring back in,
// on the shared MENU clock — brisker than a window's, because a list is opened to be
// clicked rather than looked at.
const MENU_IN_MS = SURFACE_MENU_IN_MS;
const MENU_OUT_MS = SURFACE_MENU_OUT_MS;
// Where those motes come from and go back to: the trigger's own centre.
const dustPoint = (trigger) => {
  const r = trigger?.getBoundingClientRect?.();
  if (!r || !(r.width > 0 && r.height > 0)) return null;
  return { x: r.left + r.width / 2, y: r.top + r.height / 2 };
};

// Where each portaled menu came from, so it can be put back exactly there.
const home = new WeakMap();

// Position an already-visible menu (portaled, position: fixed) against its trigger,
// in viewport coordinates.
export const placeMenu = (menu, trigger) => {
  if (!menu || !trigger || !trigger.getBoundingClientRect) return;
  const a = trigger.getBoundingClientRect();
  const vw = window.innerWidth;
  const vh = window.innerHeight;
  // Whichever side has more room decides the cap — popoverPosition then picks that side.
  const room = Math.max(vh - a.bottom, a.top) - GAP - MARGIN;
  menu.style.maxHeight = `${Math.max(120, Math.min(MAX_H, Math.floor(room)))}px`;
  menu.style.minWidth = `${Math.round(a.width)}px`;
  const box = menu.getBoundingClientRect();
  const { left, top } = popoverPosition({
    anchor: { left: a.left, top: a.top, bottom: a.bottom },
    box: { width: box.width, height: box.height },
    viewport: { width: vw, height: vh },
    gap: GAP,
    margin: MARGIN,
  });
  menu.style.left = `${Math.round(left)}px`;
  menu.style.top = `${Math.round(top)}px`;
  // A list that had to flip ABOVE its trigger grows out of its bottom edge instead of its
  // top one, so the open/close animation still comes from the corner nearest the control.
  menu.classList.toggle('dd-above', top < a.top);
};

// Show `menu` under `trigger`: move it to <body>, unhide it, and place it. Re-places on
// resize and on any scroll, so the list follows a trigger that moves under it.
export const showMenu = (menu, trigger) => {
  if (!menu) return;
  if (!home.has(menu)) home.set(menu, { parent: menu.parentElement, next: menu.nextSibling });
  if (typeof document !== 'undefined' && menu.parentElement !== document.body) document.body.appendChild(menu);
  menu.classList.add('dd-portal');
  menu.hidden = false;
  placeMenu(menu, trigger);
  // Kept so hideMenu can send the motes back where they came from, whoever calls it.
  menu.__ddTrigger = trigger;
  // Placed first, so the motes stream at the box the list will actually occupy.
  const point = motionReduced() ? null : dustPoint(trigger);
  if (point) surfaceIn(menu, point, { ms: MENU_IN_MS });
  else settleSurface(menu);
  const reflow = () => placeMenu(menu, trigger);
  menu.__ddReflow = reflow;
  window.addEventListener('resize', reflow);
  window.addEventListener('scroll', reflow, true);
};

// Hide `menu` and put it back where it was built, clearing everything showMenu set.
export const hideMenu = (menu) => {
  if (!menu) return;
  // Measured while it is still up, then hidden at once: the cloud is a copy on <body>
  // with a life of its own, so the end state never waits for the animation.
  const point = !menu.hidden && !motionReduced() ? dustPoint(menu.__ddTrigger) : null;
  if (point) surfaceOut(menu, point, { ms: MENU_OUT_MS });
  else settleSurface(menu);
  menu.__ddTrigger = null;
  menu.hidden = true;
  if (menu.__ddReflow) {
    window.removeEventListener('resize', menu.__ddReflow);
    window.removeEventListener('scroll', menu.__ddReflow, true);
    menu.__ddReflow = null;
  }
  menu.classList.remove('dd-portal');
  for (const prop of ['left', 'top', 'minWidth', 'maxHeight']) menu.style[prop] = '';
  const h = home.get(menu);
  if (!h || !h.parent || menu.parentElement !== document.body) return;
  // The sibling it sat before may itself be gone by now; appending is then correct.
  const before = h.next && h.next.parentElement === h.parent ? h.next : null;
  h.parent.insertBefore(menu, before);
};
