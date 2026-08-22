// ── Dropdown menus that escape their container ──────────────────────────────
// PORT of browser/js/ui/dropdownMenu.js (the extension can't import across subprojects) —
// keep the two rule-for-rule; tests/dropdownMenu.test.js carries the browser suite's cases.
//
// An open menu is moved to <body> and placed in viewport coordinates: under the trigger,
// flipped above when it would overflow the bottom, clamped to the window, and capped to
// the room actually available so a long list scrolls instead of running off. That matters
// more here than in the app — the popup window is only ~400x600, and several of these
// selects sit near its bottom edge. hide() puts the menu back where it came from.
//
// Callers only have to remember one thing: while a menu is open it is NOT inside the
// component, so an outside-press check must test the menu as well as the trigger.

import { popoverPosition } from './popover.js';

const GAP = 4;        // between trigger and menu
const MARGIN = 8;     // minimum distance to a viewport edge
const MAX_H = 280;    // the .accent-dd-menu cap, respected while fitting to the window

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
  const reflow = () => placeMenu(menu, trigger);
  menu.__ddReflow = reflow;
  window.addEventListener('resize', reflow);
  window.addEventListener('scroll', reflow, true);
};

// Hide `menu` and put it back where it was built, clearing everything showMenu set.
export const hideMenu = (menu) => {
  if (!menu) return;
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
