// The chat panel's open/closed life: the dock it wears, the dust flight in and out of the
// toolbar icon, the compact popover, and every gesture that dismisses it.
import { PHONE_MEDIA } from '../../../utils.js';
import { wireModalOpenGestures } from '../../tip/popover.js';
import { subscribe, EVENTS } from '../../../eventBus/appBus.js';
import { modalShells } from '../../modal/registry.js';
import {
  surfaceIn, surfaceOut, settleSurface, sweepDust, dockAwayPoint, motionReduced, rectCenter,
  CHAT_SURFACE_IN_MS, CHAT_SURFACE_OUT_MS,
} from '../../motion.js';
import { compactChatRect } from '../geometry.js';
import { createChatDock } from '../dock.js';

export function wireOpenState(deps) {
  const {
    host, input, openBtn, resizer, header, backdrop, closeBtn, panelIsOpen, refreshStatus, invalidatePillRects,
  } = deps;
  let gestures = null;
// ui/chat/dock.js; playDust and the pill rects are declared below, so they cross as thunks.
  const chatDock = createChatDock({
    host,
    resizer,
    header,
    floatHandles: () => host.querySelectorAll('.chat-float-handle'),
    playDust: (enter) => playDust(enter),
    invalidatePillRects: () => invalidatePillRects(),
    phoneModal: () => phoneModal(),
    onAdopt: () => gestures?.notifyClosed(),
  });
  const { setDock, announceLayout, adoptLayout, restoreFromCompact } = chatDock;
// Closing plays the reverse dust flight: keep .chat-open until it finishes, since
// display:none cannot animate. One clock for every dock.
  const CLOSE_MS = CHAT_SURFACE_OUT_MS;
// Fullscreen shows a clone of the toolbar (ui/fullscreen/layer.js) with the same id, so
// the active state is mirrored onto it via a scoped querySelectorAll.
  const syncFsCloneActive = (on) => {
    for (const el of fsCloneBtns()) el.classList.toggle('active', on);
  };
// No unread dot (the toast already announces a landed turn); a quiet pulse marks work in flight.
  const markChatBusy = (on) => {
    openBtn?.classList.toggle('chat-working', on);
    for (const el of fsCloneBtns()) el.classList.toggle('chat-working', on);
  };
  const fsCloneBtns = () => document.querySelectorAll('#fs-controls-panel #chat-btn');
// The icon to pin the compact popover to: in fullscreen the original measures 0×0.
  const anchorBtn = () => {
    for (const el of fsCloneBtns()) {
      const r = el.getBoundingClientRect();
      if (r.width || r.height) return el;
    }
    return openBtn;
  };
// One entrance's origin in place of the icon: the client rect an icon dropped on the page was released on.
  let entranceFrom = null;
// A float panel flies out of the toolbar icon and back (modalFromIcon/modalToIcon). Fed
// from floatRect: the panel is display:none while closed.
  const setFloatOriginVars = () => {
    if (!host.classList.contains('chat-dock-float')) return;
    const a = entranceFrom ?? anchorBtn()?.getBoundingClientRect?.();
    const onScreen = !!a && a.width > 0 && a.height > 0 && a.bottom > 0 && a.top < window.innerHeight;
    const f = chatDock.rect();
    const cx = onScreen ? a.left + a.width / 2 : f.x + f.w / 2;
    const cy = onScreen ? a.top + a.height / 2 : -Math.max(48, f.h * 0.3);
    host.style.setProperty('--modal-dx', `${Math.round(cx - (f.x + f.w / 2))}px`);
    host.style.setProperty('--modal-dy', `${Math.round(cy - (f.y + f.h / 2))}px`);
    host.style.setProperty('--modal-sx', String(onScreen ? Math.max(a.width / f.w, 0.05) : 0.4));
    host.style.setProperty('--modal-sy', String(onScreen ? Math.max(a.height / f.h, 0.05) : 0.4));
  };
// The panel forms from motes out of where it comes from: a float out of the toolbar
// icon, a docked panel from far past its edge. Measured live.
  const dustPoint = () => {
    if (host.classList.contains('chat-dock-float')) {
      const p = rectCenter(entranceFrom ?? anchorBtn());
      if (p) return p;
    }
    const r = host.getBoundingClientRect();
    return dockAwayPoint(r, chatDock.mode()) || { x: r.left + r.width / 2, y: -Math.max(48, r.height * 0.3) };
  };
  const playDust = (enter) => {
    if (motionReduced()) { settleSurface(host); return; }
    (enter ? surfaceIn : surfaceOut)(host, dustPoint(), { ms: enter ? CHAT_SURFACE_IN_MS : CLOSE_MS });
  };
  let closeTimer = null;
// A sequel to run once the close animation finishes (float → compact). Any later
// setOpen supersedes it.
  let afterClose = null;
  const setOpen = (on) => {
    clearTimeout(closeTimer);
    afterClose = null;
    host.classList.remove('chat-closing');
// Any close resets the gesture machine (popover.js notifyClosed), or a leaked 'sticky'
// mode lets a later Alt glide close a reopened panel.
    if (!on) gestures?.notifyClosed();
    if (!on && host.classList.contains('chat-open')) {
      setFloatOriginVars();
      sweepDust(host);
      playDust(false);        // …measured while it is still on screen
      host.classList.add('chat-closing');
      openBtn?.classList.remove('active');
      syncFsCloneActive(false);
      closeTimer = setTimeout(() => {
        host.classList.remove('chat-open', 'chat-closing');
        host.removeAttribute('data-drop-owner');
        restoreFromCompact();
        const next = afterClose;
        afterClose = null;
        next?.();
      }, CLOSE_MS);
      return;
    }
    if (on) setFloatOriginVars();
    host.classList.toggle('chat-open', on);
// After the class, or the panel is display:none and there is nothing to measure.
    if (on) playDust(true);
    else settleSurface(host);
    announceLayout();
// Declares "drops over my rect are mine" to controlsBinder's overDropOwner.
    host.toggleAttribute('data-drop-owner', on);
// Same `active` class as fullscreen/incognito, so it inherits their styling.
    openBtn?.classList.toggle('active', on);
    syncFsCloneActive(on);
    if (on) {
      refreshStatus();
      input.focus();
    }
  };
  const showCompact = () => {
    const anchor = anchorBtn()?.getBoundingClientRect?.();
// The popover shape displaces the layout; chatDock restores it when the popover goes.
    if (anchor) chatDock.enterCompact(compactChatRect(anchor, window.innerWidth, window.innerHeight));
    setOpen(true);
  };
  const openCompact = (convert = false) => {
    if (panelIsOpen() && !(convert && !chatDock.isCompact())) { input.focus(); return; }
// Converting a live panel: let the outgoing shape close, then open the compact one;
// mid-close, ride it out via afterClose.
    if (host.classList.contains('chat-closing')) { afterClose = showCompact; return; }
    if (panelIsOpen()) {
      setOpen(false);
      afterClose = showCompact;
      return;
    }
    showCompact();
  };
  if (openBtn) {
    gestures = wireModalOpenGestures(openBtn, {
      openFull: () => setOpen(!host.classList.contains('chat-open')),
      openPopover: () => openCompact(true),
// NOT eagerClick: opening docks the panel, which pushes this icon ~350px along the toolbar, so
// a double-click's second press would miss it. The click waits out DOUBLE_CLICK_MS instead.
      closePopover: () => setOpen(false),
      isPopoverOpen: panelIsOpen,
// Engaged = pointer inside the panel or text typed (focus alone must not count).
      isPeekEngaged: () => host.matches(':hover') || input.value.trim() !== '',
      holdLinger: () => input.value.trim() !== '',
    });
    host.addEventListener('mouseenter', () => gestures.boxEnter());
    host.addEventListener('mouseleave', () => gestures.boxLeave());
  }
// Entering or leaving fullscreen swaps which toolbar is on screen.
  subscribe(EVENTS.fullscreenChanged, () => {
    restoreFromCompact();
    syncFsCloneActive(panelIsOpen());
  });
// The compact shape follows the mini-window contract of every toolbar popover
// (wireModalShell): outside press closes and is swallowed, Escape closes, an Alt glide closes.
  const compactShowing = () => chatDock.isCompact() && panelIsOpen();
// A window opened FROM the panel (assistant settings) stacks over it and owns both
// gestures — closing the panel underneath would dismiss the wrong one first.
  const modalUp = () => [...modalShells].some((s) => s.isOpen());
  document.addEventListener('pointerdown', (e) => {
    if (!compactShowing() || host.contains(e.target) || modalUp()) return;
// The chat icon keeps its own gestures: swallowing its press would turn the toggle into a reopen.
    if (openBtn?.contains(e.target)) return;
// The row menu floats on the body — pressing it is chat use.
    if (e.target.closest?.('.chat-row-menu')) return;
    e.preventDefault();
    e.stopPropagation();
    setOpen(false);
  }, true);
// Phones show the panel as a centred modal, so it takes backdrop press + Escape; the
// backdrop is display:none on wider viewports.
  const phoneModal = () => typeof matchMedia !== 'undefined' && matchMedia(PHONE_MEDIA).matches;
  backdrop?.addEventListener('pointerdown', (e) => {
    if (!panelIsOpen()) return;
    e.preventDefault();
    setOpen(false);
  });
  document.addEventListener('keydown', (e) => {
    if (e.key !== 'Escape' || modalUp()) return;
    if (compactShowing() || (phoneModal() && panelIsOpen())) setOpen(false);
  });
  closeBtn.addEventListener('click', () => setOpen(false));
// `run` opens or moves the panel; a float it forms comes out of `from`, and its close goes home.
  const openFrom = (from, run) => {
    entranceFrom = from;
    try { run(); } finally { entranceFrom = null; }
  };
  return { setOpen, markChatBusy, chatDock, setDock, adoptLayout, openFrom };
}
