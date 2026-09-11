import { surfaceIn, surfaceOut, SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS } from './motion.js';
import { pointInRect } from '../utils.js';
import { submenuPlacement, samePoint } from './contextMenuModel.js';

const SUBMENU_HIDE_DELAY_MS = 180; // grace period before a submenu closes on mouseleave

// The context menu's flyout navigation: opening, placing, hover-grace hiding and closing the
// submenu chain, plus the pointer bookkeeping those decisions rest on. The keyboard walks the
// same state through the accessors returned here (ui/ctxKeyboard.js), never a copy of it.
export function createCtxNav({ menu }) {
  // ── Submenu management ──────────────────────────────────────
  let subHideTimer = null;
  let activeSub = null;
  let activeSubItem = null;

  // ── A submenu is dust too ────────────────────────────────────────────
  // Same flight as the menu it hangs off (js/ui/motion.js surfaceIn/surfaceOut), out
  // of — and back into — the ROW that owns it, which is where it grows from.
  const SUB_IN_MS = SURFACE_MENU_IN_MS;
  const SUB_OUT_MS = SURFACE_MENU_OUT_MS;
  const subPoint = (sub) => {
    const r = sub.__ctxItem?.getBoundingClientRect?.();
    if (!r || !(r.width > 0 && r.height > 0)) return null;
    return { x: r.right, y: r.top + r.height / 2 };
  };
  // Close one submenu, leaving its motes to pour back into the row. The class comes
  // off NOW either way — the cloud owns its own lifetime. (surfaceOut settles the
  // flyout itself whenever it can't fly.)
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
    subShownPointer = null;   // nothing placed ⇒ no "pointer idle since" reference
  };

  const positionSub = (item, sub) => {
    // A re-place of an ALREADY open flyout (the item moved under a still cursor, the
    // chat flyout grew) must not replay the gather — only a genuine open does.
    const wasOpen = sub.classList.contains('ctx-sub-visible');
    sub.__ctxItem = item;
    // The menu's entry pop (animations/overlays.css) keeps a live transform for ~140ms, and a
    // transformed ancestor becomes the containing block for our position:fixed
    // flyouts — one placed during the pop lands off-target. A quick hover beats the
    // animation, so finish the (purely cosmetic) pop first.
    try { for (const a of menu.getAnimations?.() || []) a.finish(); } catch { /* no WAAPI */ }
    // Render off-screen to measure, then place correctly
    sub.style.left = '-9999px';
    sub.style.top = '-9999px';
    sub.classList.add('ctx-sub-visible');
    item.classList.add('ctx-open-sub');

    const { left, top } = submenuPlacement(item.getBoundingClientRect(),
      sub.offsetWidth, sub.offsetHeight, window.innerWidth, window.innerHeight);
    sub.style.left = left + 'px';
    sub.style.top = top  + 'px';
    subShownPointer = { ...lastPointer };
    // Every (re)placement can land a row under a cursor that never moved to reach
    // it — see samplePointer/clearFreshSubs above. Marked fresh again on EVERY call,
    // including a reposition: that is exactly the "item slid under a still cursor"
    // case this exists for.
    sub.classList.add('ctx-sub-fresh');
    anyFreshSub = true;
    // Placed first, so the motes stream at the box the flyout will actually occupy.
    // A re-place of an already-open flyout leaves the flight alone; otherwise
    // surfaceIn flies — or settles the flyout itself when it can't.
    if (!wasOpen) surfaceIn(sub, subPoint(sub), { ms: SUB_IN_MS });
  };

  const repositionActiveSub = () => {
    if (activeSub && activeSubItem && activeSub.classList.contains('ctx-sub-visible'))
      positionSub(activeSubItem, activeSub);
  };

  // A flyout may declare itself "engaged" (a chat mid-typing, a running turn) via a
  // `_keepOpen` predicate: a stray mouseleave must NOT yank it away then. Only the
  // hover-out paths honour it — hovering ANOTHER submenu parent still closes it.
  const keepSubOpen = (sub) => !!sub._keepOpen?.();

  // Pointer bookkeeping for the hide timers: a mouseleave does NOT always mean the
  // user left — an item that MOVES under a stationary cursor (the menu's entry pop)
  // fires one with no user motion at all, which must not shut a just-opened flyout.
  let lastPointer = { x: -1, y: -1 };
  let subShownPointer = null;   // where the pointer was when the flyout was placed
  // The pointer has not MOVED since the open flyout was placed ⇒ hover events arriving
  // now are layout-induced (items sliding under a still cursor), not the user's.
  // Compared by position, not clock: the shift can land in the placement's millisecond.
  const pointerIdle = () => samePoint(subShownPointer, lastPointer);
  // `.ctx-sub-fresh` (set in positionSub) makes a just-placed flyout's rows pointer-events:
  // none, so the :hover the browser re-evaluates when geometry lands under a STILL cursor
  // can't fire the icon motion. Cleared on the first genuine move; the flag gates the query.
  let anyFreshSub = false;
  const clearFreshSubs = () => {
    if (!anyFreshSub) return;
    anyFreshSub = false;
    document.querySelectorAll('#ctx-menu .ctx-sub.ctx-sub-fresh')
      .forEach(s => s.classList.remove('ctx-sub-fresh'));
  };
  // Assigned by wireCtxKeyboard below (the keyboard owns the .ctx-kb highlight); the
  // pointer, open and close paths clear it. Declared here because they come first.
  let setKbItem = () => {};
  const samplePointer = e => {
    const moved = e.clientX !== lastPointer.x || e.clientY !== lastPointer.y;
    lastPointer = { x: e.clientX, y: e.clientY };
    if (subShownPointer && !pointerIdle()) clearFreshSubs();
    if (moved) setKbItem(null);   // the pointer takes over from the keyboard highlight
  };
  // Captured mouseover/mouseout too — they precede the non-bubbling mouseenter/
  // mouseleave the wiring reacts to; tracking moves alone would leave a stale position
  // at decision time. Bound only while the menu is open — its open/close paths toggle it.
  const setPointerTracking = (on) => {
    for (const type of ['mousemove', 'mouseover', 'mouseout']) {
      if (on) document.addEventListener(type, samplePointer, true);
      else document.removeEventListener(type, samplePointer, true);
    }
  };
  const pointerOver = (el) => pointInRect(lastPointer.x, lastPointer.y, el.getBoundingClientRect());

  // Hide a submenu — unless the cursor never moved since it opened (a layout-induced
  // mouseleave: re-place it and keep it), the cursor is still on it or its parent, or
  // it's the assistant flyout the user is busy in.
  const hideSub = (item, sub) => {
    if (keepSubOpen(sub)) return;
    if (pointerIdle() && sub.classList.contains('ctx-sub-visible')) {
      positionSub(item, sub);   // the item moved, not the user — follow it
      return;
    }
    if (pointerOver(item) || pointerOver(sub)) return;
    closeSub(sub);
    item.classList.remove('ctx-open-sub');
    if (activeSub === sub) { activeSub = null; activeSubItem = null; }
  };

  // Closes the WHOLE open chain, not just the deepest flyout — a nested submenu
  // (Copy Image ▸) leaves its own ancestor (Image / Layout) open, and hovering an
  // unrelated top-level item (this function's only caller) has left that branch
  // entirely, so every level of it must go.
  const closeActiveSub = () => {
    if (!activeSub || keepSubOpen(activeSub) || pointerIdle()) return;
    document.querySelectorAll('#ctx-menu .ctx-sub.ctx-sub-visible').forEach(closeSub);
    document.querySelectorAll('#ctx-menu .ctx-item.ctx-open-sub').forEach(i => i.classList.remove('ctx-open-sub'));
    activeSub = null;
    activeSubItem = null;
  };


  // Items WITHOUT a submenu close any open one on hover.
  const wirePlainItem = (item) => {
    item.addEventListener('mouseenter', () => {
      clearTimeout(subHideTimer);
      closeActiveSub();
    });
  };

  // Hover-open + grace-period hide for one submenu parent. Extracted so the
  // assistant entry — which may be built after wire() runs — gets the SAME wiring
  // as the static parents (Image / Layout, Style, Image Filter, …).
  const wireSubmenu = (item, sub) => {
    // Reposition when the flyout's content resizes (the chat grows) — the FLYOUT only.
    // Skipped mid reveal-group transition: that ticks on every frame of its own max-height
    // and snapped the flyout at the viewport clamp; its final class removal resizes once more.
    new ResizeObserver(() => {
      if (sub !== activeSub || !sub.classList.contains('ctx-sub-visible')) return;
      if (sub.querySelector('.reveal-group-transition')) return;
      positionSub(item, sub);
    }).observe(sub);

    item.addEventListener('mouseenter', () => {
      clearTimeout(subHideTimer);
      // A neighbour sliding under a stationary cursor must not steal the open flyout.
      if (pointerIdle() && activeSub && activeSub !== sub) return;
      // Phones/touch: the assistant entry is a plain item there — no flyout.
      if (item.dataset.noSub === '1') { closeActiveSub(); return; }
      // Close other open subs — but NOT an ANCESTOR flyout (the one `item` itself
      // lives inside, e.g. Copy Image ▸ opening from within Image / Layout's own
      // flyout must leave Image / Layout open, not yank it away from under the
      // cursor). This is the one path that also closes the assistant flyout while
      // it's engaged: opening a sibling submenu wins, as everywhere.
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
