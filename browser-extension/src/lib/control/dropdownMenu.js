// ── Dropdown menus that escape their container ──────────────────────────────
// PORT of browser/js/ui/control/dropdownMenu.js — keep the two rule-for-rule; tests/lib/control/dropdownMenu.test.js
// carries the browser suite's cases. An open menu moves to <body> and is placed in viewport
// coordinates, capped to the room available so a long list scrolls; that matters more here, since
// the popup window is only ~400x600. While open the menu is NOT inside the component.
import { PRESS_SLOP_PX } from '../tip/popover.js';
import { surfaceIn, surfaceOut, SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS } from '../motion.js';

const GAP = 4;        // between trigger and menu
const MARGIN = 8;     // minimum distance to a viewport edge
const MAX_H = 280;    // the .accent-dd-menu cap, respected while fitting to the window
// The list is sand like every other surface (js/ui/motion.js), on the shared MENU clock —
// brisker than a window's, because a list is opened to be clicked rather than looked at.
const MENU_IN_MS = SURFACE_MENU_IN_MS;
const MENU_OUT_MS = SURFACE_MENU_OUT_MS;
// The motes come from the CARET at the trigger's right edge, not its horizontal centre (a wide
// select formed out of the middle of its label). Clamped to the centre for a narrow trigger.
const caretPoint = (r) => (r?.width > 0 && r.height > 0
  ? { x: Math.max(r.left + r.width / 2, r.right - 14), y: r.top + r.height / 2 } : null);
export const menuDustPoint = (trigger) => caretPoint(trigger?.getBoundingClientRect?.());

// The slide entrance (menuFromAnchor) grows a list out of the point its motes fly from. `left`
// is its laid-out left edge; measured with the entrance held off when not given.
export const growFrom = (menu, point, { left = null, above = false } = {}) => {
  if (!menu?.style || !Number.isFinite(point?.x)) return;
  if (!Number.isFinite(left)) {
    const run = menu.style.animation;
    menu.style.animation = 'none';
    left = menu.getBoundingClientRect().left;
    menu.style.animation = run;
  }
  menu.style.transformOrigin = `${Math.round(point.x - left)}px ${above ? '100%' : '0px'}`;
};

// Where each portaled menu came from, so it can be put back exactly there.
const home = new WeakMap();

// Position an already-visible menu (portaled, position: fixed) against its trigger,
// in viewport coordinates.
// Below the trigger whenever the list fits there, as a native select opens; above only when it
// does not and above has more room (popoverPosition prefers the roomier side outright). Pure.
export const dropdownPosition = ({ anchor, box, viewport, gap = GAP, margin = MARGIN }) => {
  const below = anchor.bottom + gap;
  const fitsBelow = below + box.height <= viewport.height - margin;
  let top = fitsBelow || viewport.height - anchor.bottom >= anchor.top ? below : anchor.top - gap - box.height;
  top = Math.max(margin, Math.min(top, viewport.height - margin - box.height));
  const left = Math.max(margin, Math.min(anchor.left, viewport.width - margin - box.width));
  return { left, top };
};

export const placeMenu = (menu, trigger) => {
  if (!menu || !trigger || !trigger.getBoundingClientRect) return;
  const a = trigger.getBoundingClientRect();
  menu.__ddAt = a;   // the trigger's box the list was placed against, for the motes' anchor
  const vw = window.innerWidth;
  const vh = window.innerHeight;
  // Whichever side has more room decides the cap; dropdownPosition then picks the side.
  const room = Math.max(vh - a.bottom, a.top) - GAP - MARGIN;
  menu.style.maxHeight = `${Math.max(120, Math.min(MAX_H, Math.floor(room)))}px`;
  menu.style.minWidth = `${Math.round(a.width)}px`;
  // The layout size, not the painted one: the entrance starts at scale(0.66)
  // (menuFromAnchor), which placed a list flipped above its trigger over the trigger.
  const r = menu.getBoundingClientRect();
  const box = { width: menu.offsetWidth || r.width, height: menu.offsetHeight || r.height };
  const { left, top } = dropdownPosition({ anchor: a, box, viewport: { width: vw, height: vh } });
  menu.style.left = `${Math.round(left)}px`;
  menu.style.top = `${Math.round(top)}px`;
  // A list that had to flip ABOVE its trigger grows out of its bottom edge instead of its
  // top one, so the open/close animation still comes from the corner nearest the control.
  menu.classList.toggle('dd-above', top < a.top);
  growFrom(menu, menuDustPoint(trigger), { left, above: top < a.top });
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
  // Placed first, so the motes stream at the box the list will actually occupy; they belong to
  // the trigger, their `anchor`. (surfaceIn settles the menu itself when it can't fly.)
  surfaceIn(menu, menuDustPoint(trigger), { ms: MENU_IN_MS, anchor: trigger, anchorBox: menu.__ddAt });
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
  // Hidden at once, its cloud a copy on <body>; aimed at the box the list was placed against,
  // not the trigger's now, which a pick may already have re-laid (a modal easing its height).
  const at = menu.__ddAt || menu.__ddTrigger?.getBoundingClientRect?.();
  surfaceOut(menu, menu.hidden ? null : caretPoint(at), { ms: MENU_OUT_MS, anchor: menu.__ddTrigger, anchorBox: at });
  menu.__ddTrigger = null;
  menu.__ddAt = null;
  menu.hidden = true;
  if (menu.__ddTrack && typeof cancelAnimationFrame === 'function') cancelAnimationFrame(menu.__ddTrack);
  menu.__ddTrack = 0;
  if (menu.__ddReflow) {
    window.removeEventListener('resize', menu.__ddReflow);
    window.removeEventListener('scroll', menu.__ddReflow, true);
    menu.__ddReflow = null;
  }
  menu.classList.remove('dd-portal');
  for (const prop of ['left', 'top', 'minWidth', 'maxHeight', 'transformOrigin']) menu.style[prop] = '';
  const h = home.get(menu);
  if (!h || !h.parent || menu.parentElement !== document.body) return;
  // The sibling it sat before may itself be gone by now; appending is then correct.
  const before = h.next && h.next.parentElement === h.parent ? h.next : null;
  h.parent.insertBefore(menu, before);
};

// Press-drag-release: past the slop the list opens, and the release picks the row under the pointer or,
// off the list, closes it; a press inside the slop stays a click. Desktop twin: support/menu/SearchCombo.
export const createDragPick = ({ open, close, isOpen, rowAt, inList, pick, slop = PRESS_SLOP_PX }) => {
  let press = null;
  let dragging = false;
  return {
    get active() { return dragging; },
    press(x, y) { press = { x, y }; dragging = false; },
    // The row under the pointer while dragging, else null.
    move(x, y) {
      if (!press) return null;
      if (!dragging) {
        if (Math.hypot(x - press.x, y - press.y) <= slop) return null;
        dragging = true;
        if (!isOpen()) open();
      }
      return rowAt(x, y);
    },
    // True when this release ended a drag, so the click it makes is the drag's, not the trigger's.
    release(x, y) {
      const was = dragging;
      press = null;
      dragging = false;
      const row = was ? rowAt(x, y) : null;
      if (row) pick(row);
      else if (was && isOpen() && !inList(x, y)) close();
      return was;
    },
    abort() { press = null; dragging = false; },
  };
};

export const DRAG_PICK_ROW_CLASS = 'dd-hover';

const swallowNextClick = () => {
  const stop = (e) => { e.stopImmediatePropagation(); e.preventDefault(); off(); };
  const off = () => window.removeEventListener('click', stop, true);
  window.addEventListener('click', stop, true);
  setTimeout(off, 0);
};
const within = (el, x, y) => {
  const r = el.getBoundingClientRect();
  return r.width > 0 && x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
};

// Rows are hit by their boxes, since a theme flood owns the hit test while it plays. Mouse and pen
// only: a touch drag scrolls.
export const wireDragPick = (trigger, menu, { open, close, isOpen = () => !menu.hidden,
                                              enabled = () => true, rows = '.accent-dd-opt' }) => {
  if (!trigger?.addEventListener || !menu) return null;
  const inList = (x, y) => !menu.hidden && within(menu, x, y);
  const rowAt = (x, y) => (inList(x, y) ? [...menu.querySelectorAll(rows)].find((li) => within(li, x, y)) ?? null : null);
  const machine = createDragPick({ open, close, isOpen, rowAt, inList, pick: (row) => row.click() });
  let marked = null;
  const mark = (row) => {
    if (row === marked) return;
    marked?.classList.remove(DRAG_PICK_ROW_CLASS);
    marked = row;
    row?.classList.add(DRAG_PICK_ROW_CLASS);
  };
  let pointerId = null;
  const listen = (on) => {
    const f = on ? 'addEventListener' : 'removeEventListener';
    window[f]('pointermove', onMove, true);
    window[f]('pointerup', onEnd, true);
    window[f]('pointercancel', onEnd, true);
  };
  const onMove = (e) => { if (e.pointerId === pointerId) mark(machine.move(e.clientX, e.clientY)); };
  const onEnd = (e) => {
    if (e.pointerId !== pointerId) return;
    listen(false);
    pointerId = null;
    mark(null);
    if (e.type !== 'pointerup') machine.abort();
    else if (machine.release(e.clientX, e.clientY)) swallowNextClick();
  };
  trigger.addEventListener('pointerdown', (e) => {
    if (e.button !== 0 || !e.isPrimary || e.pointerType === 'touch' || !enabled()) return;
    if (pointerId === null) listen(true);
    pointerId = e.pointerId;
    machine.press(e.clientX, e.clientY);
  });
  return machine;
};
