import { isTypingTarget } from '../utils.js';
import { fillAccentMenu, markSelected } from './accentPicker.js';
import { createModalOpenGesture } from './popover.js';
import { surfaceIn, surfaceOut, rectCenter, motionReduced, SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS } from './motion.js';
import { subscribe, EVENTS } from '../eventBus/appBus.js';
// The logo's accent preset menu (right-click / Alt-hover): the Visuals dialog's rows on the
// shared popup motion.
export function wireLogoAccentMenu(logo, wrap, app) {
  // Non-modal — it only borrows the shared popup motion. Selecting applies through the same
  // setAccent path as the click-cycle, with the logo as the swap origin.
  const menu = wrap.querySelector?.('.logo-accent-menu');
  let resetRowHover = null;
  let menuCloseTimer = null;
  let menuCloseDone = null;
  // 'peek' (Alt-opened) or 'sticky' (right-click); only the Alt+click no-op needs it.
  let menuKind = null;
  let offAccentMoved = null;
  let pendingKind = null;
  const menuShowing = () => !!menu && !menu.hidden && !menu.classList.contains('dd-closing');
  // Mid swap the browser drops page :hover (`theme-instant`); pointer-driven dismissal must
  // tell those synthetic leaves from a real one.
  const swapping = () => !!document.documentElement?.classList?.contains('theme-instant');
  const reducedMotion = () => motionReduced();
  const onDocDown = (e) => { if (!wrap.contains(e.target)) closeMenu(); };
  const onMenuKey = (e) => { if (e.key === 'Escape') closeMenu(); };
  // The event carries the NEW value (app.accent lands a beat later); a custom hex marks nothing.
  const onAccentMoved = (e) => {
    if (!menu || menu.hidden) return;
    const v = typeof e.detail === 'string' ? e.detail : (app.customAccent ? null : app.accent);
    markSelected(menu, v && /^#/.test(v) ? null : v);
  };
  // The list is sand: it forms from motes out of the logo and pours back into it on the
  // shared menu clock; the `hidden` / `.dd-closing` hooks are unchanged.
  const MENU_IN_MS = SURFACE_MENU_IN_MS;
  const MENU_OUT_MS = SURFACE_MENU_OUT_MS;
  const logoPoint = () => rectCenter(wrap);
  const dustMenu = (enter) => {
    if (!menu) return;
    const point = reducedMotion() ? null : logoPoint();
    (enter ? surfaceIn : surfaceOut)(menu, point, { ms: enter ? MENU_IN_MS : MENU_OUT_MS });
  };
  const openMenu = () => {
    if (!menu) return;
    if (pendingKind) menuKind = pendingKind;
    if (!menu.childElementCount) {
      // A pick applies and closes (hovering already previews). Rows are built once.
      resetRowHover = fillAccentMenu(menu,
                     (key) => { app.setAccent(key, logo); markSelected(menu, key); closeMenu(); },
                     { on: (key) => app.previewAccent?.(key, logo), off: () => app.endAccentPreview?.(logo) });
    }
    markSelected(menu, app.customAccent ? null : app.accent);
    // Content-sized (components/accentPicker.css lifts the 280px cap), capped at the space
    // under the logo so only a too-short window scrolls.
    const r = wrap.getBoundingClientRect?.();
    if (r && typeof window !== 'undefined' && typeof window.innerHeight === 'number') {
      menu.style.maxHeight = `${Math.max(90, window.innerHeight - r.bottom - 18)}px`;
    }
    // Reopening mid-close: the exit's animationend must not hide the fresh menu.
    clearTimeout(menuCloseTimer);
    if (menuCloseDone) menu.removeEventListener('animationend', menuCloseDone);
    menu.classList.remove('dd-closing');
    menu.hidden = false;
    dustMenu(true);
    // Same refs, so a reopen can't double-register.
    document.addEventListener('pointerdown', onDocDown, true);
    document.addEventListener('keydown', onMenuKey);
    offAccentMoved = subscribe(EVENTS.accentChanged, onAccentMoved);
  };
  const closeMenu = () => {
    if (!menu || menu.hidden || menu.classList.contains('dd-closing')) return;
    // Reverts to the committed accent and drops the row's latched hover (accentPicker.js).
    resetRowHover?.();
    app.endAccentPreview?.();
    menuKind = null;
    // A leaked popover mode would let a later Alt glide "close" a menu already gone.
    g.notifyClosed();
    document.removeEventListener('pointerdown', onDocDown, true);
    document.removeEventListener('keydown', onMenuKey);
    offAccentMoved?.();
    offAccentMoved = null;
    menuCloseDone = (e) => {
      // animationend bubbles from every row's shimmer; only the menu's own animation counts.
      if (e && e.target !== menu) return;
      clearTimeout(menuCloseTimer);
      menu.removeEventListener('animationend', menuCloseDone);
      menu.hidden = true;
      menu.classList.remove('dd-closing');
    };
    // Reduced motion: animations/overlays.css neutralises the exit, so hide outright.
    if (reducedMotion()) { menuCloseDone(); return; }
    // Hidden once the exit has played, with a timer fallback so a neutralised animation
    // can never wedge the menu open.
    menu.classList.add('dd-closing');
    dustMenu(false);
    menuCloseTimer = setTimeout(menuCloseDone, 250);
    menu.addEventListener('animationend', menuCloseDone);
  };
  // A machine in the glide registry (ui/popover.js), like every modal icon's mini window:
  // Alt+hover peeks, Alt released over it lingers; a right-click open is 'sticky'.
  const g = createModalOpenGesture({
    openFull: () => {},
    openPopover: () => openMenu(),
    closePopover: () => closeMenu(),
    isPopoverOpen: menuShowing,
    // Engaged at release time = the pointer rests inside the menu (peek → linger).
    isPeekEngaged: () => !!menu?.matches?.(':hover'),
  });
  const altPeek = () => { pendingKind = 'peek'; g.altHover(); pendingKind = null; };
  wrap.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    pendingKind = 'sticky'; g.contextmenu(); pendingKind = null;
  });
  // Both orders (mirrors popover.js wireModalOpenGestures); only the key route defers to a
  // focused text control, and preventDefault keeps bare Alt off the browser's menu bar.
  wrap.addEventListener('mouseenter', (e) => { if (e.altKey) altPeek(); });
  document.addEventListener('keydown', (e) => {
    if (e.key !== 'Alt' || !wrap.matches?.(':hover')) return;
    if (isTypingTarget(document.activeElement)) return;
    e.preventDefault?.();
    altPeek();
  });
  document.addEventListener('keyup', (e) => { if (e.key === 'Alt') g.altRelease(); });
  if (typeof window !== 'undefined' && window.addEventListener) {
    window.addEventListener('blur', () => g.altRelease());
  }
  // A swap's synthetic leave lands on a lingering menu every time a colour is picked: hold
  // the decision until the swap ends, then trust real :hover.
  menu?.addEventListener('mouseenter', () => { if (!swapping()) g.boxEnter(); });
  menu?.addEventListener('mouseleave', () => {
    if (!swapping()) { g.boxLeave(); return; }
    const settle = () => {
      if (swapping()) { setTimeout(settle, 60); return; }
      setTimeout(() => { if (!menu.matches?.(':hover')) g.boxLeave(); }, 90);
    };
    settle();
  });

  // menuKind is a getter — it changes.
  return { menuShowing, closeMenu, altPeek, menuKind: () => menuKind };
}
