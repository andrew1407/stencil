// Alt+hover on a dropdown trigger peeks its list, the way a modal icon's mini window peeks
// (popover.js createModalOpenGesture): Alt released over the list lingers until the pointer
// leaves it, released anywhere else closes it. A click-opened list ignores Alt entirely.

import { createModalOpenGesture } from './popover.js';

// A theme or accent flood snapshots the page: it owns the hit test and drops real :hover,
// and fires synthetic enter/leave while it plays.
const swapping = () => {
  const c = document.documentElement?.classList;
  return !!c && (c.contains('theme-instant') || c.contains('theme-swapping'));
};
// :hover trails the pointer by a rendered frame and a flood restores a stale one, so the
// pointer's last position is hit-tested instead. Null once it leaves the window.
let lastPt;
export const trackPointer = () => {
  if (lastPt !== undefined || typeof document === 'undefined') return;
  lastPt = null;
  const at = (e) => { lastPt = [e.clientX, e.clientY]; };
  // Chrome raises the boundary events off its own hover updates too, with no pointermove.
  for (const t of ['pointermove', 'mousemove', 'mouseover']) document.addEventListener(t, at, true);
  // A flood's synthetic out has no relatedTarget either; only a real exit empties it.
  document.addEventListener('mouseout', (e) => {
    if (e.relatedTarget) at(e);
    else if (!swapping()) lastPt = null;
  }, true);
};
export const pointerIn = (el) => {
  if (!el) return false;
  if (!lastPt) return !!el.matches?.(':hover');
  // A snapshot answers <html> for every point, a beat before its class lands: ask the box then.
  const at = swapping() ? null : document.elementFromPoint?.(lastPt[0], lastPt[1]);
  if (at && at !== document.documentElement) return !!el.contains?.(at);
  const r = el.getBoundingClientRect?.();
  return !!r && lastPt[0] >= r.left && lastPt[0] <= r.right && lastPt[1] >= r.top && lastPt[1] <= r.bottom;
};
// A leave counts once the pointer is really out: a flood is waited out, and a hit test still
// reading inside (the two disagree for a frame) waits for the next move.
const watching = new WeakMap();
const confirmLeave = (el, isIn, leave) => {
  if (watching.has(el)) return;
  let settling = false;
  const stop = () => {
    watching.delete(el);
    document.removeEventListener('mousemove', check, true);
    el.removeEventListener('mouseenter', stop);
  };
  const check = () => {
    if (!watching.has(el)) return;
    if (swapping()) { settling = true; setTimeout(check, 60); return; }
    if (settling) { settling = false; setTimeout(check, 90); return; }
    if (isIn()) return;
    stop();
    leave();
  };
  watching.set(el, stop);
  document.addEventListener('mousemove', check, true);
  el.addEventListener('mouseenter', stop);
  check();
};

// A dropdown opened inside `box` lives on <body> (dropdownMenu.js showMenu): the list is
// part of the box all the same.
const ownedList = (box, el) => {
  const list = el?.closest?.('.dd-portal');
  return list && box?.contains?.(list.__ddTrigger) ? list : null;
};

// Pointer inside `box`, or inside a list one of its dropdowns opened.
export const peekEngaged = (box) => pointerIn(box)
  || [...document.querySelectorAll('.dd-portal')].some((l) => ownedList(box, l) && pointerIn(l));

// Drives a window's boxEnter/boxLeave; stepping from the box into its own dropdown's list is
// not a leave, so the window's linger waits for the pointer to leave both.
export const wirePeekBox = (box, g) => {
  trackPointer();
  const out = (e) => {
    const to = e.relatedTarget;
    if (to && box.contains(to)) return;
    const list = ownedList(box, to);
    if (list) list.addEventListener('mouseleave', out, { once: true });
    else confirmLeave(box, () => peekEngaged(box), () => g.boxLeave());
  };
  box.addEventListener('mouseenter', () => g.boxEnter());
  box.addEventListener('mouseleave', out);
};

// A peek released on a row picks it through the row's own click, found before the release: the
// pick's flood takes over the hit test at once. The owner's pick skips its close while picking().
export const wireReleasePick = (menu, g, rows, { isPeek, isShowing }) => {
  trackPointer();
  let picking = false;
  document.addEventListener('keyup', (e) => {
    if (e.key !== 'Alt') return;
    const row = isPeek() && isShowing() ? [...menu.querySelectorAll(rows)].find((li) => pointerIn(li)) : null;
    g.altRelease();
    if (!row || !isShowing()) return;   // only into a menu the release left open
    picking = true;
    row.click();
    picking = false;
  });
  return { picking: () => picking };
};

// `hover` is what the pointer rests on, `menu` the list; `isTyping` says a text field has
// focus. `open`/`close` are the owner's, and its close calls notifyClosed().
export const wireAltPeek = (hover, menu, { open, close, isOpen = () => !menu.hidden,
                                           enabled = () => true, isTyping = () => false }) => {
  trackPointer();
  const g = createModalOpenGesture({
    openFull: () => {},
    openPopover: open,
    closePopover: close,
    isPopoverOpen: isOpen,
    isPeekEngaged: () => pointerIn(menu),
  });
  // The window the trigger sits in holds it (popover.js `holds`), so it is not glided shut.
  const peek = () => { if (enabled()) g.altHover(hover); };
  hover.addEventListener('mouseenter', (e) => { if (e.altKey) peek(); });
  // The pointer on the trigger is the intent, so a focused field does not defer the peek; it
  // only keeps its Alt. preventDefault keeps bare Alt off the browser's menu bar.
  document.addEventListener('keydown', (e) => {
    if (e.key !== 'Alt' || e.repeat || !pointerIn(hover)) return;
    if (!isTyping()) e.preventDefault();
    peek();
  });
  // Blur too — Alt+Tab switches away without delivering the keyup.
  document.addEventListener('keyup', (e) => { if (e.key === 'Alt') g.altRelease(); });
  if (typeof window !== 'undefined') window.addEventListener?.('blur', () => g.altRelease());
  menu.addEventListener?.('mouseenter', () => g.boxEnter());
  menu.addEventListener?.('mouseleave', () => confirmLeave(menu, () => pointerIn(menu), () => g.boxLeave()));
  return g;
};
