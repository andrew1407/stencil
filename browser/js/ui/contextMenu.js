import { StencilElement, hostTag, define } from './base.js';
import { hotkeys } from '../core/hotkeys.js';
import { chatRowMenuOpen } from './chatView.js';
import { menuPopOrigin, surfaceIn, surfaceOut, settleSurface, motionReduced,
         SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS } from './motion.js';
import { hideExportPreview, clearAltPreviewHover } from './exportPreview.js';
import { assistantEnabled, assistantItemHtml } from './ctxAssistantItem.js';
import { wireCtxAssistant } from './ctxAssistant.js';
import { wireCtxScript } from './ctxScript.js';
export { assistantEnabled, assistantItemHtml };
export { ctxKeyStep, CTX_NAV_KEYS, ctxFocusables } from './ctxKeyboard.js';
import { wireCtxKeyboard } from './ctxKeyboard.js';
import { contextMenuInner } from './contextMenuMarkup.js';
import { createCtxNav } from './contextMenuNav.js';
import { ctxSyncState } from './ctxState.js';
import { wireCtxActions } from './ctxActions.js';
import { wireCtxStyleActions } from './ctxStyleActions.js';

// The custom right-click context menu.
const SUBMENU_HIDE_DELAY_MS = 180; // grace period before a submenu closes on mouseleave
const LIVE_SYNC_INTERVAL_MS = 120;
const TINT_DEBOUNCE_MS = 80;
const ASSIST_SCROLL_GRACE_MS = 900; // window in which an assistant-caused scroll can't close the menu

export class StencilContextMenu extends StencilElement {
  static inner() { return contextMenuInner(); }
  static template() { return hostTag('stencil-context-menu', 'id="ctx-menu"', StencilContextMenu.inner()); }

  wire(app) {
    const menu = document.getElementById('ctx-menu');
    const canvas = document.getElementById('canvas');
    const viewport = document.getElementById('canvas-viewport');

    // Submenu navigation — ui/contextMenuNav.js.
    const {
      closeSub, closeAllSubs, positionSub, repositionActiveSub, hideSub, closeActiveSub,
      wireSubmenu, wirePlainItem, setPointerTracking, activeSub, setActiveSub,
      bindKbItem, setKbItem, setLastPointer,
    } = createCtxNav({ menu });

    // Every callback is a thunk: menuIsOpen and friends are `const`s declared further down wire()
    // (temporal dead zone).
    bindKbItem(wireCtxKeyboard({
      menu,
      menuIsOpen: () => menuIsOpen(),
      chatRowMenuOpen: () => chatRowMenuOpen(),
      closeSub: (sub) => closeSub(sub),
      positionSub: (item, sub) => positionSub(item, sub),
      activeSub,
      setActiveSub,
    }).setKbItem);

    // Top-level items only, so nested ones cannot close their parent submenu on hover.
    menu.querySelectorAll(':scope > .ctx-item').forEach(item => {
      const sub = item.querySelector(':scope > .ctx-sub');
      if (sub) wireSubmenu(item, sub);
      // #ctx-script's flyout is hung off it later (ctxScript.js) and wires itself as a
      // submenu; wiring it as a plain item too would shut its own flyout on re-entry.
      else if (item.id !== 'ctx-script') wirePlainItem(item);
    });

    // Live-sync keeps the menu current while open (hotkeys may change state).
    let syncInterval = null;
    // Where the menu grew from, so the close pours back into it.
    let openPoint = null;

    let onMenuClose = () => {};
    const closeMenu = () => {
      onMenuClose();
      // Measured while still on screen, and only if it is: closeMenu is also the idle teardown.
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

    // While the chat is answering (and a grace window after), viewport scrolls caused by the
    // executing plan must not dismiss the menu.
    let assistSending = false;
    let assistBusyUntil = 0;
    let syncAssistant = () => {};
    let syncScript = () => {};
    const assistantBusy = () => assistSending || Date.now() < assistBusyUntil;

    // Clamped once per open: re-placing the root under a stationary cursor fires mouseleave
    // on the hovered item and closes its flyout (the chat's own observer grows it in place).
    const placeMenu = (x, y) => {
      const mw = menu.offsetWidth;
      const mh = menu.offsetHeight;
      const vw = window.innerWidth;
      const vh = window.innerHeight;
      const left = Math.max(4, Math.min(x, vw - mw - 6));
      const top = Math.max(4, Math.min(y, vh - mh - 6));
      menu.style.left = left + 'px';
      menu.style.top = top + 'px';
      menu.style.transformOrigin = menuPopOrigin(x, y, { left, top, width: mw, height: mh });
    };

    const openAt = (x, y) => {
      openPoint = { x, y };
      closeAllSubs();
      setKbItem(null);
      // Seeded with the click point, so the idle checks never see a stale position.
      setPointerTracking(true);
      setLastPointer(x, y);
      // The Assistant and Stencil Script entries are re-evaluated per open, before measuring.
      syncAssistant();
      syncScript();
      menu.style.left = '-9999px'; menu.style.top = '-9999px';
      menu.classList.add('ctx-open');
      placeMenu(x, y);
      // Opacity only: a live transform would make the menu the containing block for its
      // position:fixed flyouts.
      if (!motionReduced()) surfaceIn(menu, openPoint, { ms: SURFACE_MENU_IN_MS });
      clearInterval(syncInterval);
      syncInterval = setInterval(() => {
        if (menu.classList.contains('ctx-open')) syncState();
        else { clearInterval(syncInterval); syncInterval = null; }
      }, LIVE_SYNC_INTERVAL_MS);
    };

    [canvas, viewport].forEach(el => {
      el.addEventListener('contextmenu', e => {
        if (!app.image) return;
        // On macOS Ctrl+click is the secondary click, so the Alt+Ctrl pull-out drag fires this
        // too; Alt with it means the gesture, not a menu.
        e.preventDefault();
        if (e.altKey) return;
        syncState();
        openAt(e.clientX, e.clientY);
      });
    });

    // Everything inside the menu keeps it open, the assistant chat included.
    document.addEventListener('mousedown', e => {
      if (!menu.contains(e.target)) closeMenu();
    });
    // Capture phase, while open: Escape reaches no other handler. A chat row menu on top
    // goes first (its own capture closer swallows the key).
    document.addEventListener('keydown', e => {
      if (e.code !== 'Escape' || !menuIsOpen()) return;
      if (chatRowMenuOpen()) return;
      e.stopPropagation();
      closeMenu();
    }, true);
    document.addEventListener('scroll', e => {
      // Scrolling the transcript (or the menu itself) is not a page scroll.
      if (e.target && e.target.nodeType && menu.contains(e.target)) return;
      // Nor is the viewport re-scrolling because a plan just replaced the image.
      if (assistantBusy()) return;
      closeMenu();
    }, true);

    wireCtxActions(app, { menu, closeMenu, wireSubmenu });
    wireCtxStyleActions(app, { menu, closeMenu });

    // The two entries whose flyout is a live surface — a chat, an editor — drive the menu's
    // state through this host rather than closing it the way an .ctx-item does.
    const flyoutHost = {
      menu,
      menuIsOpen, closeMenu, closeAllSubs, positionSub, wireSubmenu,
      sending: () => assistSending,
      setSending: (v) => { assistSending = v; },
      bumpBusy: () => { assistBusyUntil = Date.now() + ASSIST_SCROLL_GRACE_MS; },
      setActiveSub,
      setOnMenuClose: (fn) => { onMenuClose = fn; },
    };
    ({ syncAssistant } = wireCtxAssistant(app, flyoutHost));
    ({ syncScript } = wireCtxScript(app, flyoutHost));

    // Format the data-hk spans now, so macOS shows ⌘/⌥ from the first paint.
    hotkeys.updateCtxHints();
  }
}
define('stencil-context-menu', StencilContextMenu);
