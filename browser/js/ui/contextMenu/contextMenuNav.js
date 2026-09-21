import { surfaceIn, surfaceOut, SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS } from '../motion.js';
import { pointInRect } from '../../utils.js';
import { submenuPlacement, samePoint } from './contextMenuModel.js';

const SUBMENU_HIDE_DELAY_MS = 180;

// The context menu's flyout navigation; the keyboard (ui/ctxKeyboard.js) walks the same
// state through the accessors returned here.
export function createCtxNav({ menu }) {
  let subHideTimer = null;
  let activeSub = null;
  let activeSubItem = null;

  // A submenu flies like the menu it hangs off, out of and back into the row that owns it.
  const SUB_IN_MS = SURFACE_MENU_IN_MS;
  const SUB_OUT_MS = SURFACE_MENU_OUT_MS;
  const subPoint = (sub) => {
    const r = sub.__ctxItem?.getBoundingClientRect?.();
    if (!r || !(r.width > 0 && r.height > 0)) return null;
    return { x: r.right, y: r.top + r.height / 2 };
  };
  // The class comes off now either way — the cloud owns its own lifetime.
  const closeSub = (sub) => {
    surfaceOut(sub, sub.classList.contains('ctx-sub-visible') ? subPoint(sub) : null,
      { ms: SUB_OUT_MS });
    sub.classList.remove('ctx-sub-visible');
    sub.classList.remove('ctx-sub-fresh');
  };

  const closeAllSubs = () => {
    clearTimeout(subHideTimer);
    document.querySelectorAll('#ctx-menu .ctx-sub.ctx-sub-visible').forEach(closeSub);
    document.querySelectorAll('#ctx-menu .ctx-item.ctx-open-sub').forEach(i => i.classList.remove('ctx-open-sub'));
    activeSub = null;
    activeSubItem = null;
    subShownPointer = null;
  };

  const positionSub = (item, sub) => {
    // A re-place of an already open flyout must not replay the gather.
    const wasOpen = sub.classList.contains('ctx-sub-visible');
    sub.__ctxItem = item;
    // The entry pop keeps a live transform for ~140ms, and a transformed ancestor becomes the
    // containing block for the position:fixed flyouts, so the (cosmetic) pop is finished first.
    try { for (const a of menu.getAnimations?.() || []) a.finish(); } catch { /* no WAAPI */ }
    sub.style.left = '-9999px';
    sub.style.top = '-9999px';
    sub.classList.add('ctx-sub-visible');
    item.classList.add('ctx-open-sub');

    const { left, top } = submenuPlacement(item.getBoundingClientRect(),
      sub.offsetWidth, sub.offsetHeight, window.innerWidth, window.innerHeight);
    sub.style.left = left + 'px';
    sub.style.top = top  + 'px';
    subShownPointer = { ...lastPointer };
    // Marked fresh on every call, including a reposition: an item sliding under a still
    // cursor is exactly the case (samplePointer/clearFreshSubs).
    sub.classList.add('ctx-sub-fresh');
    anyFreshSub = true;
    // Placed first, so the motes stream at the box the flyout will occupy.
    if (!wasOpen) surfaceIn(sub, subPoint(sub), { ms: SUB_IN_MS });
  };

  const repositionActiveSub = () => {
    if (activeSub && activeSubItem && activeSub.classList.contains('ctx-sub-visible'))
      positionSub(activeSubItem, activeSub);
  };

  // A flyout may declare itself engaged via `_keepOpen` (a chat mid-typing); only the
  // hover-out paths honour it.
  const keepSubOpen = (sub) => !!sub._keepOpen?.();

  // A mouseleave does not always mean the user left: an item moving under a stationary
  // cursor (the entry pop) fires one with no user motion.
  let lastPointer = { x: -1, y: -1 };
  let subShownPointer = null;
  // Compared by position, not clock: the shift can land in the placement's millisecond.
  const pointerIdle = () => samePoint(subShownPointer, lastPointer);
  // `.ctx-sub-fresh` makes a just-placed flyout's rows pointer-events: none, so a :hover
  // re-evaluated under a still cursor cannot fire the icon motion.
  let anyFreshSub = false;
  const clearFreshSubs = () => {
    if (!anyFreshSub) return;
    anyFreshSub = false;
    document.querySelectorAll('#ctx-menu .ctx-sub.ctx-sub-fresh')
      .forEach(s => s.classList.remove('ctx-sub-fresh'));
  };
  // Assigned by wireCtxKeyboard (the keyboard owns the .ctx-kb highlight).
  let setKbItem = () => {};
  const samplePointer = e => {
    const moved = e.clientX !== lastPointer.x || e.clientY !== lastPointer.y;
    lastPointer = { x: e.clientX, y: e.clientY };
    if (subShownPointer && !pointerIdle()) clearFreshSubs();
    if (moved) setKbItem(null);
  };
  // Captured mouseover/mouseout too: they precede the non-bubbling mouseenter/mouseleave
  // the wiring reacts to. Bound only while the menu is open.
  const setPointerTracking = (on) => {
    for (const type of ['mousemove', 'mouseover', 'mouseout']) {
      if (on) document.addEventListener(type, samplePointer, true);
      else document.removeEventListener(type, samplePointer, true);
    }
  };
  const pointerOver = (el) => pointInRect(lastPointer.x, lastPointer.y, el.getBoundingClientRect());

  // Unless the cursor never moved since it opened (re-place and keep it), is still on it
  // or its parent, or it is the engaged assistant flyout.
  const hideSub = (item, sub) => {
    if (keepSubOpen(sub)) return;
    if (pointerIdle() && sub.classList.contains('ctx-sub-visible')) {
      positionSub(item, sub);
      return;
    }
    if (pointerOver(item) || pointerOver(sub)) return;
    closeSub(sub);
    item.classList.remove('ctx-open-sub');
    if (activeSub === sub) { activeSub = null; activeSubItem = null; }
  };

  // Closes the whole open chain: hovering an unrelated top-level item has left the branch entirely.
  const closeActiveSub = () => {
    if (!activeSub || keepSubOpen(activeSub) || pointerIdle()) return;
    document.querySelectorAll('#ctx-menu .ctx-sub.ctx-sub-visible').forEach(closeSub);
    document.querySelectorAll('#ctx-menu .ctx-item.ctx-open-sub').forEach(i => i.classList.remove('ctx-open-sub'));
    activeSub = null;
    activeSubItem = null;
  };


  const wirePlainItem = (item) => {
    item.addEventListener('mouseenter', () => {
      clearTimeout(subHideTimer);
      closeActiveSub();
    });
  };

  // Shared with the assistant entry, which may be built after wire() runs.
  const wireSubmenu = (item, sub) => {
    // Reposition when the flyout's content resizes; skipped mid reveal-group transition,
    // which ticks every frame of its own max-height.
    new ResizeObserver(() => {
      if (sub !== activeSub || !sub.classList.contains('ctx-sub-visible')) return;
      if (sub.querySelector('.reveal-group-transition')) return;
      positionSub(item, sub);   // the item moved, not the user — follow it
    }).observe(sub);

    item.addEventListener('mouseenter', () => {
      clearTimeout(subHideTimer);
      if (pointerIdle() && activeSub && activeSub !== sub) return;
      if (item.dataset.noSub === '1') { closeActiveSub(); return; }
      // Close other open subs, but not an ancestor flyout the item lives inside. This path
      // also closes an engaged assistant flyout: opening a sibling submenu wins.
      document.querySelectorAll('#ctx-menu .ctx-sub.ctx-sub-visible').forEach(s => {
        if (s === sub || s.contains(item)) return;
        closeSub(s);
      });
      document.querySelectorAll('#ctx-menu .ctx-item.ctx-open-sub').forEach(i => {
        if (i === item || i.contains(item)) return;
        i.classList.remove('ctx-open-sub');
      });
      positionSub(item, sub);
      activeSub = sub;
      activeSubItem = item;
    });

    item.addEventListener('mouseleave', e => {
      if (sub.contains(e.relatedTarget)) return;
      subHideTimer = setTimeout(() => {
        if (activeSub === sub) hideSub(item, sub);
      }, SUBMENU_HIDE_DELAY_MS);
    });

    sub.addEventListener('mouseenter', () => clearTimeout(subHideTimer));
    sub.addEventListener('mouseleave', e => {
      if (item.contains(e.relatedTarget)) return;
      subHideTimer = setTimeout(() => hideSub(item, sub), SUBMENU_HIDE_DELAY_MS);
    });
  };
  return {
    closeSub, closeAllSubs, positionSub, repositionActiveSub, hideSub, closeActiveSub,
    wireSubmenu, wirePlainItem, setPointerTracking, pointerIdle,
    activeSub: () => activeSub,
    setActiveSub: (sub, item) => { activeSub = sub; activeSubItem = item; },
    bindKbItem: (fn) => { setKbItem = fn; },
    setKbItem: (item) => setKbItem(item),
    setLastPointer: (x, y) => { lastPointer = { x, y }; },
  };
}
