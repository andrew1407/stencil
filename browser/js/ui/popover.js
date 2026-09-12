// ── Modal popovers: the compact, anchored variant of an app modal ───────────
// Every toolbar icon that opens a covering modal also answers dblclick, right-click
// and (touch) long press with a SMALL version of the same modal pinned next to the
// icon. Same DOM, same wiring: base.js wireModalShell adds `.modal-popover` and
// positions the box; click-outside and Escape close it exactly like the full modal.
//
// The click/dblclick split reuses the projects-list rule (projectsModal.js
// DOUBLE_CLICK_MS): a plain click defers one interval, a dblclick cancels it. Touch
// pays no wait — tap opens the full modal, long press is the popover route. Timers
// injected for DOM-free tests (tests/popover.test.js).

import { isTypingTarget } from '../utils.js';

export const DOUBLE_CLICK_MS = 250;   // same interval as the projects list's deferred click
export const LONG_PRESS_MS = 500;     // touch hold that opens the popover (matches touchDrag's feel)
export const PRESS_SLOP_PX = 10;      // travel that turns a hold into a scroll — no popover

// Where the popover box sits: below the anchor icon, left edges aligned, flipped above
// when the bottom would overflow, and clamped inside the viewport on both axes. Pure —
// rects in, {left, top} out — so the placement rules are testable without layout.
export const popoverPosition = ({ anchor, box, viewport, gap = 8, margin = 8 }) => {
  let top = anchor.bottom + gap;
  if (top + box.height > viewport.height - margin) {
    const above = anchor.top - gap - box.height;
    top = above >= margin ? above : Math.max(margin, viewport.height - margin - box.height);
  }
  const left = Math.max(margin, Math.min(anchor.left, viewport.width - margin - box.width));
  return { left, top };
};

// The gesture machine behind one modal-opening icon. `openFull` is the ordinary modal,
// `openPopover` the anchored compact one. Rules:
//   mouse   click        → openFull, DEFERRED one double-click interval
//           dblclick     → openPopover, STICKY (closes by the normal means)
//           right-click  → openPopover, sticky
//           Alt + hover  → openPopover as a HOLD-to-peek: lives while Alt is down.
//                          Released while ENGAGED (clicked inside / pointer inside)
//                          it LINGERS — and closes once the pointer leaves the box.
//                          Gliding (Alt held) onto ANOTHER icon closes whatever
//                          mini window is showing — peek, linger, or sticky — and
//                          opens the new icon's peek. Full modals are never touched.
//   touch   tap          → openFull immediately (no double-tap here, so no wait)
//           long press   → openPopover; the synthetic click that follows is swallowed,
//                          as is Android's synthetic contextmenu (openPopover is
//                          idempotent at the shell — see wireModalShell)
// App-wide registry for the glide: every machine registers a close handle; an
// Alt+hover closes ALL other machines' mini windows before opening its own. A machine
// only closes windows it opened popover-shaped (mode) — full modals are never touched.
const glideRegistry = new Set();

export const LINGER_CLOSE_MS = 250;   // grace after leaving an engaged, released peek

export const createModalOpenGesture = ({
  openFull,
  openPopover,
  closePopover = () => {},
  isPopoverOpen = () => false,
  isPeekEngaged = () => false,
  holdLinger = () => false,
  delay = DOUBLE_CLICK_MS,
  // Open on the FIRST click instead of waiting `delay`: the wait protects a full MODAL
  // from flashing open-and-shut under a dblclick, but a panel whose popover gesture
  // merely RE-SHAPES the same window (the chat) has nothing to flash.
  eagerClick = false,
  holdMs = LONG_PRESS_MS,
  slop = PRESS_SLOP_PX,
  setTimer = setTimeout,
  clearTimer = clearTimeout,
} = {}) => {
  let clickTimer = null;
  let holdTimer = null;
  let press = null;            // { x, y } while a touch/pen is down on the icon
  let lastWasTouch = false;    // what kind of pointer produced the upcoming click
  let swallowClick = false;    // a long press / contextmenu already acted for this gesture
  // How the machine's own popover is open right now (advisory — the shell's
  // isPopoverOpen() is the ground truth for visibility):
  //   null     — not open through a popover gesture (closed, or a full modal)
  //   'peek'   — Alt is held; closes on release unless engaged
  //   'linger' — released while engaged; closes when the pointer leaves the box
  //   'sticky' — deliberate open (dblclick / right-click / long press)
  let mode = null;
  let lingerTimer = null;
  const cancelClick = () => { if (clickTimer !== null) { clearTimer(clickTimer); clickTimer = null; } };
  const cancelHold = () => { if (holdTimer !== null) { clearTimer(holdTimer); holdTimer = null; } };
  const cancelLinger = () => { if (lingerTimer !== null) { clearTimer(lingerTimer); lingerTimer = null; } };
  const closeOwn = () => {
    mode = null;
    cancelLinger();
    if (isPopoverOpen()) closePopover();
  };
  // Registered app-wide so an Alt glide on ANOTHER icon can close this window —
  // but only when it is popover-shaped (mode set); full modals stay.
  const handle = { closeFromGlide: () => { if (mode !== null) closeOwn(); } };
  glideRegistry.add(handle);
  return {
    click() {
      cancelClick();
      if (swallowClick) { swallowClick = false; return; }
      if (lastWasTouch || eagerClick) { openFull(); return; }
      clickTimer = setTimer(() => { clickTimer = null; openFull(); }, delay);
    },
    dblclick() {
      cancelClick();
      if (lastWasTouch) return;   // touch has no double-click gesture
      mode = 'sticky';            // a deliberate open — Alt release keeps it
      openPopover();
    },
    contextmenu() {
      cancelClick();
      cancelHold();
      // Android fires contextmenu for a long press, then sometimes a click — swallow it.
      swallowClick = true;
      mode = 'sticky';
      openPopover();
    },
    // A KEYBOARD shortcut pressed while the pointer rests on the icon: Alt went down
    // first, so the hover-peek already opened the window — toggling would slam it shut.
    // Claim the peek instead: the window stays, and the Alt release no longer owns it.
    // Returns true when it took the press, false to let the caller toggle normally.
    hotkey() {
      if (mode !== 'peek' && mode !== 'linger') return false;
      mode = 'sticky';
      cancelLinger();
      return true;
    },
    // Alt + hover, HOLD-to-peek: leaves this machine's OWN open window alone
    // (never adopts a deliberate open) and closes every other icon's mini window,
    // so a glide walks the toolbar swapping windows as it goes.
    altHover() {
      cancelClick();
      if (isPopoverOpen()) return;
      for (const h of glideRegistry) if (h !== handle) h.closeFromGlide();
      mode = 'peek';
      openPopover();
    },
    // Alt released (blur too — Alt+Tab eats the keyup): close the peek, unless
    // engaged (isPeekEngaged) — an engaged peek lingers until boxLeave closes it.
    altRelease() {
      if (mode !== 'peek') return;
      if (isPeekEngaged()) { mode = 'linger'; return; }
      closeOwn();
    },
    // Pointer crossing the box edge, reported by the owner. Only a LINGERING
    // window hover-binds; peeks (Alt still down) and sticky opens ignore it.
    boxEnter() { cancelLinger(); },
    boxLeave() {
      if (mode !== 'linger' || !isPopoverOpen()) return;
      cancelLinger();
      lingerTimer = setTimer(() => {
        lingerTimer = null;
        // holdLinger: mid-typing (a text field inside, with content) never closes.
        if (mode === 'linger' && !holdLinger()) closeOwn();
      }, LINGER_CLOSE_MS);
    },
    pressStart({ x = 0, y = 0, touch = false } = {}) {
      swallowClick = false;
      lastWasTouch = touch;
      press = { x, y };
      cancelHold();
      if (touch) {
        holdTimer = setTimer(() => {
          holdTimer = null;
          swallowClick = true;   // the click synthesized on release must not also open
          mode = 'sticky';
          openPopover();
        }, holdMs);
      }
    },
    pressMove({ x = 0, y = 0 } = {}) {
      if (!press) return;
      if (Math.abs(x - press.x) > slop || Math.abs(y - press.y) > slop) cancelHold();
    },
    pressEnd() { cancelHold(); press = null; },
    // The owner closed (or re-shaped) the window through its OWN means. Without this
    // the machine still believes a popover shows ('sticky' leaks), and a later Alt
    // glide would closeFromGlide a window the user opened deliberately.
    notifyClosed() { mode = null; cancelLinger(); },
  };
};

// DOM wiring for the machine above. `pointerdown` tells touch from mouse per gesture
// (pen counts as touch — it long-presses the same way).
export const wireModalOpenGestures = (btn, { openFull, openPopover, closePopover, isPopoverOpen,
                                             isPeekEngaged, holdLinger, eagerClick }) => {
  // NB: this list is explicit, so anything added to createModalOpenGesture's options has to
  // be added here too or it is silently dropped — which is exactly how the chat kept
  // waiting 250ms for a double-click that its `eagerClick` had opted out of.
  const g = createModalOpenGesture({ openFull, openPopover, closePopover, isPopoverOpen,
                                     isPeekEngaged, holdLinger, eagerClick });
  // A DISABLED icon opens nothing — mini window included. Checked live on every route:
  // disabled controls here keep pointer events ON (layout/buttonStates.css — the disabled-reason
  // tooltip needs the hover), so hover/contextmenu events still arrive.
  const enabled = () => !btn.disabled;
  btn.__stencilGestures = g;   // the hotkey layer reaches the machine through its icon
  btn.addEventListener('click', () => { if (enabled()) g.click(); });
  btn.addEventListener('dblclick', (e) => { e.preventDefault(); if (enabled()) g.dblclick(); });
  btn.addEventListener('contextmenu', (e) => { e.preventDefault(); if (enabled()) g.contextmenu(); });
  btn.addEventListener('pointerdown', (e) => { if (enabled()) g.pressStart({ x: e.clientX, y: e.clientY, touch: e.pointerType !== 'mouse' }); });
  btn.addEventListener('pointermove', (e) => g.pressMove({ x: e.clientX, y: e.clientY }));
  btn.addEventListener('pointerup', () => g.pressEnd());
  btn.addEventListener('pointercancel', () => g.pressEnd());
  // Alt + hover, both orders: gliding on with Alt held, and pressing Alt while resting
  // on the icon (`:hover` is the live check). preventDefault keeps bare Alt off the
  // browser's menu bar; only the KEY route defers to a focused text control.
  btn.addEventListener('mouseenter', (e) => { if (e.altKey && enabled()) g.altHover(); });
  document.addEventListener('keydown', (e) => {
    if (e.key !== 'Alt' || !btn.matches(':hover') || !enabled()) return;
    if (isTypingTarget(document.activeElement)) return;
    e.preventDefault();
    g.altHover();
  });
  // HOLD-to-peek: what Alt+hover opened lives only while Alt is down. Blur too —
  // Alt+Tab switches away without ever delivering the keyup.
  document.addEventListener('keyup', (e) => { if (e.key === 'Alt') g.altRelease(); });
  window.addEventListener('blur', () => g.altRelease());
  return g;
};
