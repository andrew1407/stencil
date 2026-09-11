import { isTypingTarget } from '../utils.js';
import { fillAccentMenu, markSelected } from './accentPicker.js';
import { createModalOpenGesture } from './popover.js';
import { surfaceIn, surfaceOut, rectCenter, motionReduced, SURFACE_MENU_IN_MS, SURFACE_MENU_OUT_MS } from './motion.js';
import { subscribe, EVENTS } from '../bus/appBus.js';
// ── The logo's accent PRESET menu (right-click / Alt-hover) ─────────────────
// The listbox half of the logo gesture: the same rows the Visuals dialog shows, on the
// shared popup motion. Returns the three things the click handler in logoAccent.js needs.
export function wireLogoAccentMenu(logo, wrap, app) {
  // ── Right-click (or Alt+click) → accent preset menu ── the same listbox the Visuals
  // dialog uses (accentPicker.js; custom colours stay on the double-click picker).
  // Non-modal — it only borrows the shared popup motion. Selecting applies through the
  // same setAccent path as the click-cycle, with the logo as the swap origin.
  const menu = wrap.querySelector?.('.logo-accent-menu');
  let resetRowHover = null;   // fillAccentMenu's reset (see openMenu / closeMenu)
  let menuCloseTimer = null;
  let menuCloseDone = null;
  // How the menu is open right now: 'peek' (Alt-opened) or 'sticky' (right-click).
  // Only the Alt+click no-op below needs the distinction; the LIFETIME rules live in
  // the shared gesture machine.
  let menuKind = null;
  // The accent subscription lives only while the menu is up.
  let offAccentMoved = null;
  let pendingKind = null;   // set around a machine call so openMenu knows who opened it
  const menuShowing = () => !!menu && !menu.hidden && !menu.classList.contains('dd-closing');
  // Mid accent/theme swap the browser force-drops page :hover (`theme-instant` marks
  // the window — see the hover latch). Pointer-driven dismissal must tell those
  // synthetic leaves from a real one: every swap happens with the menu under the pointer.
  const swapping = () => !!document.documentElement?.classList?.contains('theme-instant');
  const reducedMotion = () => motionReduced();
  const onDocDown = (e) => { if (!wrap.contains(e.target)) closeMenu(); };
  const onMenuKey = (e) => { if (e.key === 'Escape') closeMenu(); };
  // The accent moved while the menu shows — a logo click cycles the preset with the list
  // up, and its ✓ used to stay put (user report). The event carries the NEW value
  // (app.accent lands a beat later); a custom hex marks nothing.
  const onAccentMoved = (e) => {
    if (!menu || menu.hidden) return;
    const v = typeof e.detail === 'string' ? e.detail : (app.customAccent ? null : app.accent);
    markSelected(menu, v && /^#/.test(v) ? null : v);
  };
  // The list is sand, like every other surface (js/ui/motion.js): it forms from motes
  // streaming out of the logo and comes apart into motes pouring back into it, on the
  // shared menu clock. The `hidden` / `.dd-closing` hooks are unchanged — the dust
  // simply replaces the scale those two used to drive.
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
      // A pick applies the accent and CLOSES the menu (user decision — hovering already
      // previews, so a click is a commit). Rows are built once, kept across the swap.
      resetRowHover = fillAccentMenu(menu,
                     (key) => { app.setAccent(key, logo); markSelected(menu, key); closeMenu(); },
                     { on: (key) => app.previewAccent?.(key, logo), off: () => app.endAccentPreview?.(logo) });
    }
    markSelected(menu, app.customAccent ? null : app.accent);
    // Size to CONTENT by default (components/accentPicker.css lifts the shared 280px cap for this
    // copy); cap at the viewport space under the logo so only a genuinely too-short
    // window makes the list scroll (overflow-y:auto shows a scrollbar only then).
    const r = wrap.getBoundingClientRect?.();
    if (r && typeof window !== 'undefined' && typeof window.innerHeight === 'number') {
      menu.style.maxHeight = `${Math.max(90, window.innerHeight - r.bottom - 18)}px`;
    }
    // Reopening mid-close: abort the exit (its animationend must not hide the fresh menu).
    clearTimeout(menuCloseTimer);
    if (menuCloseDone) menu.removeEventListener('animationend', menuCloseDone);
    menu.classList.remove('dd-closing');
    menu.hidden = false;
    dustMenu(true);
    // Idempotent (same refs), so a reopen can't double-register.
    document.addEventListener('pointerdown', onDocDown, true);
    document.addEventListener('keydown', onMenuKey);
    offAccentMoved = subscribe(EVENTS.accentChanged, onAccentMoved);
  };
  const closeMenu = () => {
    if (!menu || menu.hidden || menu.classList.contains('dd-closing')) return;
    // Reverts to the committed accent, and drops the row's latched hover with it, so no
    // held slide greets the next open (accentPicker.js).
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
      // animationend BUBBLES: every row shimmers on its ::after and the pointer is always
      // over a row at close, so an unfiltered listener ended the exit on the first shimmer.
      // Only the menu's OWN animation (or the fallback timer / reduced motion) counts.
      if (e && e.target !== menu) return;
      clearTimeout(menuCloseTimer);
      menu.removeEventListener('animationend', menuCloseDone);
      menu.hidden = true;
      menu.classList.remove('dd-closing');
    };
    // Reduced motion: animations/overlays.css neutralises both the rise and the pop-out, so
    // there is no exit to wait for — hide outright rather than sit through the fallback.
    if (reducedMotion()) { menuCloseDone(); return; }
    // Leaves on the shared pop-out; hidden only once the exit has played — with a timer
    // fallback so a missing/neutralised animation can never wedge the menu open.
    menu.classList.add('dd-closing');
    dustMenu(false);
    menuCloseTimer = setTimeout(menuCloseDone, 250);
    menu.addEventListener('animationend', menuCloseDone);
  };
  // ── The shared toolbar peek system (ui/popover.js) ── the accent menu is a MACHINE
  // IN THE GLIDE REGISTRY, exactly like every modal icon's mini window: Alt+hover
  // peeks it (first closing other minis), Alt released over it lingers, elsewhere
  // closes; a right-click open is 'sticky' but a glide still closes it. Modal gating
  // rides the system's own live :hover checks, untouched.
  const g = createModalOpenGesture({
    openFull: () => {},          // the logo opens no full modal — click cycles the accent
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
  // Alt + hover, both orders (mirrors popover.js wireModalOpenGestures): gliding on
  // with Alt held, and pressing Alt while resting on it. Only the KEY route defers to
  // a focused text control; preventDefault keeps bare Alt off the browser's menu bar.
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
  // The pointer crossing the menu edge drives the linger close — but a swap's synthetic
  // leave lands on a LINGERING menu every time a colour is picked. Hold the decision
  // until the swap ends and then trust real :hover, exactly like the hover latch.
  menu?.addEventListener('mouseenter', () => { if (!swapping()) g.boxEnter(); });
  menu?.addEventListener('mouseleave', () => {
    if (!swapping()) { g.boxLeave(); return; }
    const settle = () => {
      if (swapping()) { setTimeout(settle, 60); return; }
      setTimeout(() => { if (!menu.matches?.(':hover')) g.boxLeave(); }, 90);
    };
    settle();
  });

  // menuKind is read by the click handler in logoAccent.js, as a getter — it changes.
  return { menuShowing, closeMenu, altPeek, menuKind: () => menuKind };
}
