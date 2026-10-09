import { popoverPosition, wireModalOpenGestures } from '../tip/popover.js';
import { peekEngaged, wirePeekBox } from '../tip/altPeek.js';
import { wireModalDrag } from './drag.js';
import { wireModalResize } from './resize.js';
import { sweepDust } from '../motion.js';
import { isTypingTarget } from '../../utils.js';
import { modalShells, wireEscapeOnce, wireCollapseOnce, closeOpenModal, closeOpenPopovers, raiseWindow } from './registry.js';
import { multiWindow } from '../motion/motionPrefs.js';
import { createModalFlight, shownRect, MODAL_CLOSE_MS } from './flight.js';
import { canvasAnchorRect } from './imageAnchor.js';

// Open/close/overlay-mousedown/Escape for every app modal. onOpen/onClose run BEFORE the
// modal-open class toggles. The opener answers the popover gestures (ui/tip/popover.js).
// `originEl` resolves the control the flight belongs to when it isn't the opener (null: fall
// from above); `stacked` shells open over what is showing instead of replacing it.
const FOCUS_MS = 30;

export const wireModalShell = (overlay, openBtn, closeBtn, { onOpen, onClose, escapeClose = true, originEl: originFor = null, stacked = false, focusOnOpen = null } = {}) => {
  // The shared class, or a shell with its own ids (settingsModal) falls back to the first child.
  const boxOf = () => overlay.querySelector('.app-modal') || overlay.firstElementChild;

  // A caller can pass another origin (the idle canvas's "＋ Blank image" card opens the
  // same dialog and grows out of itself).
  const flight = createModalFlight(overlay, boxOf);
  const drag = wireModalDrag(overlay, boxOf);
  const resize = wireModalResize(overlay, boxOf);
  const { reducedMotion, finishClose, playDust } = flight;
  let originEl = openBtn;
  // Where the close flies back to when a context-menu row is gone by then: the "⋯" that
  // opened the menu. An element (or a function naming one), measured at close time.
  let closeOriginEl = null;
  // One shell serves two callers — Open-in replaces from the toolbar, stacks from a row.
  let stackedNow = stacked;
  const defaultOrigin = () => (originFor ? originFor() : openBtn);
  // An anchor is an element or a plain client rect (a control about to hide passes the rect). A
  // click's PointerEvent has a numeric width too, but no top: it is never a rect.
  const isRect = (v) => Number.isFinite(v?.width) && Number.isFinite(v?.top);
  const anchorLike = (v) => !!v && (typeof v.getBoundingClientRect === 'function' || isRect(v));
  // An element counts only while shown: an opener folded away with the rows falls from above.
  const rectOf = (el) => (typeof el?.getBoundingClientRect === 'function'
    ? shownRect(el)
    : (isRect(el) ? el : null));
  let fromAbove = false;   // this open had no shown control, so its close rises back up
  const setOriginVars = (el = originEl) => flight.setOrigin(rectOf(el));
  const onScreenRect = (r) =>
    !!r && r.width > 0 && r.height > 0 && r.bottom > 0 && r.top < (window.innerHeight || 0);

  // `from` null means there is no control: fall from above. Omitted means the shell's opener.
  // `at`, a client point, takes the window's top-left corner; one already up at full size only moves.
  const open = (from, backTo = null, { stacked: stackThisOpen = stacked, at = null } = {}) => {
    if (at && api.isOpen() && !overlay.classList.contains('modal-popover')) { drag.placeAt(at); return; }
    if (multiWindow() && api.isPopover()) close();   // the popover grows into the window
    originEl = anchorLike(from) ? from
             : (from === null ? null : defaultOrigin());
    closeOriginEl = anchorLike(backTo) || typeof backTo === 'function' ? backTo : null;
    stackedNow = stackThisOpen;
    if (!stackedNow) (multiWindow() ? closeOpenPopovers(api) : closeOpenModal(api));
    finishClose();
    drag.reset();   // a window opens where its flight puts it, never where it was dragged
    resize.reset(); // …and at its own size
    onOpen?.();
    fromAbove = !rectOf(originEl);
    overlay.classList.add('modal-open');
    if (multiWindow()) raiseWindow(api);
    if (at) drag.placeAt(at, true);
    // The box has no size while display:none.
    if (!reducedMotion() && setOriginVars()) playDust(true);
    // A window opens ready to be typed into, deferred past the entrance (infoModal, scriptModal),
    // unless the user picked a field in it meanwhile: that one keeps the caret.
    const focusTarget = typeof focusOnOpen === 'function' ? focusOnOpen() : focusOnOpen;
    const heldAtOpen = document.activeElement;
    if (focusTarget?.focus) setTimeout(() => {
      const now = document.activeElement;
      if (!api.isOpen() || (now !== heldAtOpen && boxOf()?.contains(now))) return;
      focusTarget.focus();
    }, FOCUS_MS);
  };
  let gestures = null;
  // `backTo` overrides where this one close lands (an outcome that opened an image goes into
  // the toolbar's Open control); a click handler's event is not an anchor and is ignored.
  const close = (backTo = null) => {
    onClose?.();
    // A removal's motes are on <body> and outlive the window that started them. The window's
    // own close flight starts after this, so the sweep never catches it.
    sweepDust(overlay);
    // `modal-open` is what every caller tests, so it comes off now and the shrink runs under
    // `modal-closing`. Measure first — display:none measures 0.
    const named = typeof closeOriginEl === 'function' ? closeOriginEl() : closeOriginEl;
    const home = (anchorLike(backTo) ? backTo : null) || named || originEl || defaultOrigin();
    // A named home that is hidden by now (a folded toolbar, a closed list) sends the close up.
    const back = onScreenRect(rectOf(home)) ? home : ((fromAbove || closeOriginEl) ? null : canvasAnchorRect());
    const animate = overlay.classList.contains('modal-open') && !reducedMotion() && setOriginVars(back);
    overlay.classList.remove('modal-open');
    // It flies home at its own level: dropped at once, it would jump over the windows it sat under.
    api.raisedAt = 0;
    setTimeout(() => { if (!api.isOpen()) api.setZ(null); }, MODAL_CLOSE_MS);
    if (animate) {
      flight.playClosing();
    } else {
      flight.settle();   // nothing plays: drop anything an open left in the air
      finishClose();
    }
    // A leaked 'sticky' mode would let a later Alt glide close a full modal.
    gestures?.notifyClosed();
  };
  // Side by side, a popover gesture on a window that is up re-shapes it into the popover.
  const openPopover = (anchorEl) => {
    if (overlay.classList.contains('modal-open')) {
      if (!multiWindow() || api.isPopover()) return;
      close();
    }
    if (!stacked) (multiWindow() ? closeOpenPopovers(api) : closeOpenModal(api));
    finishClose();
    onOpen?.();
    // Unless the anchor is hidden (a gear inside the menu that just closed).
    const ar = anchorEl?.getBoundingClientRect?.();
    originEl = (ar && ar.width > 0 && ar.height > 0) ? anchorEl : defaultOrigin();
    drag.reset();
    resize.reset();
    overlay.classList.add('modal-open', 'modal-popover');
    const box = boxOf();
    if (box && anchorEl?.getBoundingClientRect) {
      // The popover class caps the box's size.
      const p = popoverPosition({
        anchor: anchorEl.getBoundingClientRect(),
        box: box.getBoundingClientRect(),
        viewport: { width: window.innerWidth, height: window.innerHeight },
      });
      box.style.left = `${p.left}px`;
      box.style.top = `${p.top}px`;
    }
    // After pinning, so the popover grows toward where it lands.
    if (!reducedMotion() && setOriginVars()) playDust(true);
  };
  // The icon (and its shortcut) toggles: pressing again closes; side by side, a popover grows into the window.
  const toggle = (from) => {
    if (overlay.classList.contains('modal-open') && !(multiWindow() && api.isPopover())) { close(); return; }
    open(from);
  };
  const isPopover = () => overlay.classList.contains('modal-open') && overlay.classList.contains('modal-popover');
  const api = { open, close, openPopover, toggle, isOpen: () => overlay.classList.contains('modal-open'),
                isPopover, raisedAt: 0,
                setZ: (z) => { if (overlay.style) overlay.style.zIndex = z === null ? '' : String(z); },
                contains: (el) => !!el && !!overlay.contains?.(el),
                // Its dim and blur wait out the windows closing beside it, then fade in.
                holdBackdrop: () => {
                  overlay.classList?.add('backdrop-wait');
                  setTimeout(() => overlay.classList?.remove('backdrop-wait'), MODAL_CLOSE_MS);
                },
                overlayId: overlay?.id || null,
                get stacked() { return stackedNow; },
                takesEscape: () => escapeClose || overlay.classList.contains('modal-popover') };
  modalShells.add(api);
  wireEscapeOnce();
  wireCollapseOnce();
  overlay.__stencilModal = api;
  if (openBtn) openBtn.__stencilModal = api;
  if (openBtn) {
    const g = gestures = wireModalOpenGestures(openBtn, {
      openFull: toggle,
      openPopover: () => openPopover(openBtn),
      // The machine only ever closes windows it opened in a popover shape.
      closePopover: close,
      isPopoverOpen: () => overlay.classList.contains('modal-open'),
      // Engaged = the pointer rests inside the box or a list its dropdowns opened (typed
      // content is holdLinger's job).
      isPeekEngaged: () => peekEngaged(boxOf()),
      holds: (el) => !!boxOf()?.contains(el),
      // Not closed by hover-out while typing in a field WITH CONTENT (several modals
      // auto-focus an empty search on open).
      holdLinger: () => {
        const a = document.activeElement;
        if (!a || !boxOf()?.contains(a) || !isTypingTarget(a)) return false;
        return String(a.value ?? '').trim() !== '';
      },
    });
    // The pointer crossing the box edge drives the linger close.
    const boxEl = boxOf();
    if (boxEl) wirePeekBox(boxEl, g);
    // Everything a window raises stacks over it, so read the z-order, not a stale class list.
    const pressedAboveBox = (target) => {
      const mine = parseInt(getComputedStyle(overlay).zIndex, 10);
      if (!Number.isFinite(mine)) return false;
      for (let el = target; el && el !== document.body; el = el.parentElement) {
        const z = parseInt(getComputedStyle(el).zIndex, 10);
        if (Number.isFinite(z) && z >= mine) return true;
      }
      return false;
    };
    // Popover shape: the overlay is pointer-transparent (CSS) so an Alt glide reaches other icons.
    // A press outside closes, unless it landed in something the popover raised onto <body>.
    document.addEventListener('pointerdown', (e) => {
      if (!overlay.classList.contains('modal-open') || !overlay.classList.contains('modal-popover')) return;
      const box = boxOf();
      if (!box || box.contains(e.target) || pressedAboveBox(e.target)) return;
      e.preventDefault();
      e.stopPropagation();
      close();
    }, true);
  }
  if (closeBtn) closeBtn.addEventListener('click', close);
  // Side by side, a press outside closes nothing, and a press inside brings the window to the top.
  overlay.addEventListener('mousedown', e => { if (e.target === overlay && (!multiWindow() || isPopover())) close(); });
  overlay.addEventListener('pointerdown', () => { if (multiWindow() && api.isOpen() && !isPopover()) raiseWindow(api); }, true);
  return api;
};
