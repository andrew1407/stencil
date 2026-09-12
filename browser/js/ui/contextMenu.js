import { StencilElement, hostTag, define } from './base.js';
import { hotkeys } from '../core/hotkeys.js';
import { chatRowMenuOpen } from './chatView.js';
import { menuPopOrigin, surfaceIn, surfaceOut, settleSurface, motionReduced,
         SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS } from './motion.js';
import { hideExportPreview, clearAltPreviewHover } from './exportPreview.js';
import { assistantEnabled, assistantItemHtml } from './ctxAssistantItem.js';
import { wireCtxAssistant } from './ctxAssistant.js';
export { assistantEnabled, assistantItemHtml };
// Keyboard navigation lives in ctxKeyboard.js; its three helpers stay reachable here.
export { ctxKeyStep, CTX_NAV_KEYS, ctxFocusables } from './ctxKeyboard.js';
import { wireCtxKeyboard } from './ctxKeyboard.js';
import { contextMenuInner } from './contextMenuMarkup.js';
import { createCtxNav } from './contextMenuNav.js';
import { ctxSyncState } from './ctxState.js';
import { wireCtxActions } from './ctxActions.js';
import { wireCtxStyleActions } from './ctxStyleActions.js';

// ── Component: custom right-click context menu ──────────────────
const SUBMENU_HIDE_DELAY_MS = 180; // grace period before a submenu closes on mouseleave
const LIVE_SYNC_INTERVAL_MS = 120; // poll cadence to reflect external state while the menu is open
const TINT_DEBOUNCE_MS = 80;       // debounce custom-tint recolor+save while dragging the picker
const ASSIST_SCROLL_GRACE_MS = 900; // window in which an assistant-caused scroll can't close the menu

export class StencilContextMenu extends StencilElement {
  static inner() { return contextMenuInner(); }
  static template() { return hostTag('stencil-context-menu', 'id="ctx-menu"', StencilContextMenu.inner()); }

  wire(app) {
    const menu = document.getElementById('ctx-menu');
    const canvas = document.getElementById('canvas');
    const viewport = document.getElementById('canvas-viewport');

    // ── Submenu navigation: opening, placing, hover-grace hiding — ui/contextMenuNav.js ──
    const {
      closeSub, closeAllSubs, positionSub, repositionActiveSub, hideSub, closeActiveSub,
      wireSubmenu, wirePlainItem, setPointerTracking, activeSub, setActiveSub,
      bindKbItem, setKbItem, setLastPointer,
    } = createCtxNav({ menu });

    // Keyboard navigation (desktop QMenu parity) — ui/ctxKeyboard.js. It walks the
    // same open-flyout state the hover path owns, so that is passed in, not copied.
    // Every callback is a thunk: menuIsOpen and friends are `const`s declared further
    // down wire(), so reading them at this call site would hit the temporal dead zone.
    bindKbItem(wireCtxKeyboard({
      menu,
      menuIsOpen: () => menuIsOpen(),
      chatRowMenuOpen: () => chatRowMenuOpen(),
      closeSub: (sub) => closeSub(sub),
      positionSub: (item, sub) => positionSub(item, sub),
      activeSub,
      setActiveSub,
    }).setKbItem);

    // Wire hover handlers — only for TOP-LEVEL items so nested ones inside
    // a submenu don't accidentally close their parent submenu on hover.
    menu.querySelectorAll(':scope > .ctx-item').forEach(item => {
      const sub = item.querySelector(':scope > .ctx-sub');
      if (sub) wireSubmenu(item, sub);
      else wirePlainItem(item);
    });

    // ── Live-sync timer ─────────────────────────────────────────
    // Keeps ctx menu state current while it's open (hotkeys may change state)
    let syncInterval = null;
    // Where the menu grew from — the click. Kept so the close pours back into it.
    let openPoint = null;

    // The assistant flyout's own teardown (its dictation, wired later) rides every close.
    let onMenuClose = () => {};
    const closeMenu = () => {
      onMenuClose();
      // Into the very point it grew out of (js/ui/motion.js) — measured while it is
      // still on screen, and only if it IS: closeMenu is also the idle teardown.
      if (menu.classList.contains('ctx-open') && !motionReduced()) surfaceOut(menu, openPoint, { ms: SURFACE_MENU_OUT_MS });
      else settleSurface(menu);
      menu.classList.remove('ctx-open');
      closeAllSubs();
      setKbItem(null);
      setPointerTracking(false);
      clearInterval(syncInterval);
      syncInterval = null;
      hideExportPreview();
      clearAltPreviewHover();
    };

    const syncState = () => ctxSyncState(app);

    const menuIsOpen = () => menu.classList.contains('ctx-open');

    // Assistant turn state, read by the scroll-close guard below: while the chat is
    // answering (and for a grace window after), the canvas viewport scrolls caused by
    // the executing plan must NOT dismiss the menu the user is chatting in.
    let assistSending = false;
    let assistBusyUntil = 0;
    // Assigned by wireCtxAssistant below; openAt only runs long after wire() has.
    let syncAssistant = () => {};
    const assistantBusy = () => assistSending || Date.now() < assistBusyUntil;

    // Clamp the menu into the viewport, ONCE per open. Deliberately NOT re-run while
    // open: moving the root menu under a stationary cursor fires mouseleave on the
    // hovered item, closing its flyout — the chat lives in a FLYOUT for exactly this
    // reason (its own observer grows it without moving the menu).
    const placeMenu = (x, y) => {
      const mw = menu.offsetWidth;
      const mh = menu.offsetHeight;
      const vw = window.innerWidth;
      const vh = window.innerHeight;
      const left = Math.max(4, Math.min(x, vw - mw - 6));
      const top = Math.max(4, Math.min(y, vh - mh - 6));
      menu.style.left = left + 'px';
      menu.style.top = top + 'px';
      // The entry pop (animations/overlays.css menuPop) grows out of the click point.
      menu.style.transformOrigin = menuPopOrigin(x, y, { left, top, width: mw, height: mh });
    };

    const openAt = (x, y) => {
      openPoint = { x, y };
      closeAllSubs();
      setKbItem(null);
      // Track the pointer only while open; seed it with the click point so the
      // idle checks never compare against a stale position from a previous open.
      setPointerTracking(true);
      setLastPointer(x, y);
      // Build/hide the Assistant entry and pick its mode (flyout vs plain) BEFORE
      // measuring — the menu is re-evaluated per open, so a provider change or a
      // window resize between opens is picked up.
      syncAssistant();
      menu.style.left = '-9999px'; menu.style.top = '-9999px';
      menu.classList.add('ctx-open');
      placeMenu(x, y);
      // …and it forms out of that same point as dust (js/ui/motion.js). Opacity only —
      // a live transform on the menu would make it the containing block for its
      // position:fixed flyouts, which is the very trap menuPop's comment names.
      if (!motionReduced()) surfaceIn(menu, openPoint, { ms: SURFACE_MENU_IN_MS });
      // Start live-sync so hotkey changes reflect immediately
      clearInterval(syncInterval);
      syncInterval = setInterval(() => {
        if (menu.classList.contains('ctx-open')) syncState();
        else { clearInterval(syncInterval); syncInterval = null; }
      }, LIVE_SYNC_INTERVAL_MS);
    };

    [canvas, viewport].forEach(el => {
      el.addEventListener('contextmenu', e => {
        if (!app.image) return;
        // On macOS Ctrl+click IS the secondary click, so the Alt+Ctrl pull-out drag fires
        // this too. Alt with it means the gesture, not a menu: swallow the event, while a
        // plain Ctrl+click still gets one.
        e.preventDefault();
        if (e.altKey) return;
        syncState();
        openAt(e.clientX, e.clientY);
      });
    });

    // Close on outside click / Escape / scroll; everything INSIDE the menu keeps it
    // open, including the assistant chat. Escape always closes, even from the chat
    // input — a chat input you can't Escape out of is the surprising option.
    document.addEventListener('mousedown', e => {
      if (!menu.contains(e.target)) closeMenu();
    });
    // Capture phase, consumed only while open: Escape closes the menu and reaches
    // no other app handler. A chat row menu floating on top goes first (its own
    // capture closer swallows the key).
    document.addEventListener('keydown', e => {
      if (e.code !== 'Escape' || !menuIsOpen()) return;
      if (chatRowMenuOpen()) return;
      e.stopPropagation();
      closeMenu();
    }, true);
    document.addEventListener('scroll', e => {
      // Scrolling the chat transcript (or the menu itself, on a short screen) is not
      // a page scroll — only the latter dismisses the menu.
      if (e.target && e.target.nodeType && menu.contains(e.target)) return;
      // Nor is the canvas viewport re-scrolling because a PLAN just replaced/rotated
      // the image: those scrolls are the assistant's, not the user's.
      if (assistantBusy()) return;
      closeMenu();
    }, true);

    // ── Actions: what each row DOES — ui/ctxActions.js ──
    wireCtxActions(app, { menu, closeMenu, wireSubmenu });
    wireCtxStyleActions(app, { menu, closeMenu });

    // The Assistant entry (a submenu parent whose flyout IS a chat) lives in
    // ctxAssistant.js; it drives the menu's own state through this narrow host.
    ({ syncAssistant } = wireCtxAssistant(app, {
      menu,
      menuIsOpen, closeMenu, closeAllSubs, positionSub, wireSubmenu,
      sending: () => assistSending,
      setSending: (v) => { assistSending = v; },
      bumpBusy: () => { assistBusyUntil = Date.now() + ASSIST_SCROLL_GRACE_MS; },
      setActiveSub,
      setOnMenuClose: (fn) => { onMenuClose = fn; },
    }));

    // Format the data-hk shortcut spans (hardcoded "Ctrl+C"/"Alt+J" in the markup) right away
    // so macOS shows ⌘/⌥ from the very first paint — not only after the menu is first opened
    // (which is when syncState's poll would otherwise be the first to call updateCtxHints).
    hotkeys.updateCtxHints();
  }
}
define('stencil-context-menu', StencilContextMenu);
