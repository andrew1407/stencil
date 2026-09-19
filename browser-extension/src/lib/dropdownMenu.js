// ── Dropdown menus that escape their container ──────────────────────────────
// PORT of browser/js/ui/dropdownMenu.js — keep the two rule-for-rule; tests/dropdownMenu.test.js
// carries the browser suite's cases. An open menu moves to <body> and is placed in viewport
// coordinates, capped to the room available so a long list scrolls; that matters more here, since
// the popup window is only ~400x600. While open the menu is NOT inside the component.
import { popoverPosition } from './popover.js';
import { surfaceIn, surfaceOut, SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS } from './motion.js';

const GAP = 4;        // between trigger and menu
const MARGIN = 8;     // minimum distance to a viewport edge
const MAX_H = 280;    // the .accent-dd-menu cap, respected while fitting to the window
// The list is sand like every other surface (js/ui/motion.js), on the shared MENU clock —
// brisker than a window's, because a list is opened to be clicked rather than looked at.
const MENU_IN_MS = SURFACE_MENU_IN_MS;
const MENU_OUT_MS = SURFACE_MENU_OUT_MS;
// The motes come from the CARET at the trigger's right edge, not its horizontal centre (a wide
// select formed out of the middle of its label). Clamped to the centre for a narrow trigger.
export const menuDustPoint = (trigger) => {
  const r = trigger?.getBoundingClientRect?.();
  if (!r || !(r.width > 0 && r.height > 0)) return null;
  return { x: Math.max(r.left + r.width / 2, r.right - 14), y: r.top + r.height / 2 };
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
  // (surfaceIn settles the menu itself when it can't fly.)
  surfaceIn(menu, menuDustPoint(trigger), { ms: MENU_IN_MS });
  const reflow = () => placeMenu(menu, trigger);
  menu.__ddReflow = reflow;
  window.addEventListener('resize', reflow);
  window.addEventListener('scroll', reflow, true);
  trackTrigger(menu, trigger);
};

// `resize`/`scroll` miss the moves that happen here — a chat panel sliding in re-lays the page
// under an open list. One rect read per frame, re-placed only when the trigger really moved.
const trackTrigger = (menu, trigger) => {
  if (typeof requestAnimationFrame !== 'function') return;
  let last = null;
  const step = () => {
    if (menu.hidden || menu.__ddTrigger !== trigger) { menu.__ddTrack = 0; return; }
    const r = trigger.getBoundingClientRect?.();
    const key = r && `${Math.round(r.left)},${Math.round(r.top)},${Math.round(r.width)}`;
    if (key && key !== last) {
      if (last !== null) placeMenu(menu, trigger);   // the first frame is showMenu's own
      last = key;
    }
    menu.__ddTrack = requestAnimationFrame(step);
  };
  menu.__ddTrack = requestAnimationFrame(step);
};

// Hide `menu` and put it back where it was built, clearing everything showMenu set.
export const hideMenu = (menu) => {
  if (!menu) return;
  // Measured while it is still up, then hidden at once: the cloud is a copy on <body>
  // with a life of its own, so the end state never waits for the animation.
  surfaceOut(menu, menu.hidden ? null : menuDustPoint(menu.__ddTrigger), { ms: MENU_OUT_MS });
  menu.__ddTrigger = null;
  menu.hidden = true;
  if (menu.__ddTrack && typeof cancelAnimationFrame === 'function') cancelAnimationFrame(menu.__ddTrack);
  menu.__ddTrack = 0;
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
