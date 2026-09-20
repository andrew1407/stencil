import { popoverPosition, wireModalOpenGestures } from './popover.js';
import { wireModalDrag } from './modalDrag.js';
import { sweepDust } from './motion.js';
import { isTypingTarget } from '../utils.js';
import { modalShells, wireEscapeOnce, closeOpenModal } from './modalRegistry.js';
import { createModalFlight } from './modalFlight.js';

// Open/close/overlay-mousedown/Escape for every app modal. onOpen/onClose run BEFORE the
// modal-open class toggles. The opener answers the popover gestures (ui/popover.js).
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
  const { reducedMotion, finishClose, playDust } = flight;
  let originEl = openBtn;
  // Where the close flies back to when a context-menu row is gone by then: the "⋯" that
  // opened the menu. An element, measured at close time.
  let closeOriginEl = null;
  // One shell serves two callers — Open-in replaces from the toolbar, stacks from a row.
  let stackedNow = stacked;
  const defaultOrigin = () => (originFor ? originFor() : openBtn);
  // An anchor is an element or a plain client rect (a control about to hide passes the rect).
  const anchorLike = (v) => !!v && (typeof v.getBoundingClientRect === 'function' || Number.isFinite(v.width));
  const rectOf = (el) => (typeof el?.getBoundingClientRect === 'function'
    ? el.getBoundingClientRect()
    : (Number.isFinite(el?.width) ? el : null));
  const setOriginVars = (el = originEl) => flight.setOrigin(rectOf(el));
  const onScreenRect = (r) =>
    !!r && r.width > 0 && r.height > 0 && r.bottom > 0 && r.top < (window.innerHeight || 0);
  // Where a window collapses to when its control is off screen: the canvas. Falling off the
  // top is an entrance; as an exit it leaves towards nothing.
  const CLOSE_HOME_PX = 40;
  const canvasHomeRect = () => {
    const r = document.getElementById('canvas-viewport')?.getBoundingClientRect?.();
    const cx = r && r.width ? r.left + r.width / 2 : (window.innerWidth || 0) / 2;
    const cy = r && r.height ? r.top + r.height / 2 : (window.innerHeight || 0) / 2;
    const half = CLOSE_HOME_PX / 2;
    return { left: cx - half, top: cy - half, right: cx + half, bottom: cy + half,
             width: CLOSE_HOME_PX, height: CLOSE_HOME_PX };
  };

  // `from` null means there is no control: fall from above. Omitted means the shell's opener.
  const open = (from, backTo = null, { stacked: stackThisOpen = stacked } = {}) => {
    originEl = anchorLike(from) ? from
             : (from === null ? null : defaultOrigin());
    closeOriginEl = anchorLike(backTo) ? backTo : null;
    stackedNow = stackThisOpen;
    if (!stackedNow) closeOpenModal(api);
    finishClose();
    drag.reset();   // a window opens where its flight puts it, never where it was dragged
    onOpen?.();
    overlay.classList.add('modal-open');
    // The box has no size while display:none.
    if (!reducedMotion() && setOriginVars()) playDust(true);
    // A window opens ready to be typed into. Deferred past the entrance, which is where the
    // windows that already did this put it (infoModal, scriptModal).
    const focusTarget = typeof focusOnOpen === 'function' ? focusOnOpen() : focusOnOpen;
    if (focusTarget?.focus) setTimeout(() => { if (api.isOpen()) focusTarget.focus(); }, FOCUS_MS);
  };
  let gestures = null;
  const close = () => {
    onClose?.();
    // A removal's motes are on <body> and outlive the window that started them. The window's
    // own close flight starts after this, so the sweep never catches it.
    sweepDust(overlay);
    // `modal-open` is what every caller tests, so it comes off now and the shrink runs under
    // `modal-closing`. Measure first — display:none measures 0.
    const home = closeOriginEl || originEl || defaultOrigin();
    const animate = overlay.classList.contains('modal-open') && !reducedMotion()
      && setOriginVars(onScreenRect(rectOf(home)) ? home : canvasHomeRect());
    overlay.classList.remove('modal-open');
    if (animate) {
      flight.playClosing();
    } else {
      flight.settle();   // nothing plays: drop anything an open left in the air
      finishClose();
    }
    // A leaked 'sticky' mode would let a later Alt glide close a full modal.
    gestures?.notifyClosed();
  };
  const openPopover = (anchorEl) => {
    if (overlay.classList.contains('modal-open')) return;
    if (!stacked) closeOpenModal(api);
    finishClose();
    onOpen?.();
    // Unless the anchor is hidden (a gear inside the menu that just closed).
    const ar = anchorEl?.getBoundingClientRect?.();
    originEl = (ar && ar.width > 0 && ar.height > 0) ? anchorEl : defaultOrigin();
    drag.reset();
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
  // The icon (and its shortcut) toggles: pressing again closes.
  const toggle = (from) => {
    if (overlay.classList.contains('modal-open')) { close(); return; }
    open(from);
  };
  const api = { open, close, openPopover, toggle, isOpen: () => overlay.classList.contains('modal-open'),
                get stacked() { return stackedNow; },
                takesEscape: () => escapeClose || overlay.classList.contains('modal-popover') };
  modalShells.add(api);
  wireEscapeOnce();
  overlay.__stencilModal = api;
  if (openBtn) openBtn.__stencilModal = api;
  if (openBtn) {
    const g = gestures = wireModalOpenGestures(openBtn, {
      openFull: toggle,
      openPopover: () => openPopover(openBtn),
      // The machine only ever closes windows it opened in a popover shape.
      closePopover: close,
      isPopoverOpen: () => overlay.classList.contains('modal-open'),
      // Engaged = the pointer rests inside the box (typed content is holdLinger's job).
      isPeekEngaged: () => !!boxOf()?.matches(':hover'),
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
    if (boxEl) {
      boxEl.addEventListener('mouseenter', () => g.boxEnter());
      boxEl.addEventListener('mouseleave', () => g.boxLeave());
    }
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
  overlay.addEventListener('mousedown', e => { if (e.target === overlay) close(); });
  return api;
};
