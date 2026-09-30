// Modal popovers: every toolbar icon that opens a covering modal also answers dblclick,
// right-click and (touch) long press with a small version of it pinned next to the icon
// (wireModalShell adds `.modal-popover`). Timers are injected for DOM-free tests.

import { isTypingInFocus } from '../../utils.js';
import { onAltKeys } from './altPeek.js';
import constants from '../../../../common/config/constants.json' with { type: 'json' };

const { POPOVER } = constants;
export const DOUBLE_CLICK_MS = POPOVER.doubleClickMs;
export const LONG_PRESS_MS = POPOVER.longPressMs;
export const PRESS_SLOP_PX = POPOVER.pressSlopPx;

// Below the anchor unless above has more room: a short box near the bottom of a modal can "fit"
// below by the viewport's measure while overlapping the modal's own rows (user report). Pure.
export const popoverPosition = ({ anchor, box, viewport, gap = 8, margin = 8 }) => {
  const roomAbove = anchor.top - gap - margin;
  const roomBelow = viewport.height - anchor.bottom - gap - margin;
  let top = roomBelow >= roomAbove ? anchor.bottom + gap : anchor.top - gap - box.height;
  top = Math.max(margin, Math.min(top, viewport.height - margin - box.height));
  const left = Math.max(margin, Math.min(anchor.left, viewport.width - margin - box.width));
  return { left, top };
};

// The gesture machine behind one modal-opening icon: click → openFull after a double-click
// interval; dblclick / right-click / long press → sticky popover; Alt+hover → a peek.
const glideRegistry = new Set();

export const LINGER_CLOSE_MS = POPOVER.lingerCloseMs;

export const createModalOpenGesture = ({
  openFull,
  openPopover,
  closePopover = () => {},
  isPopoverOpen = () => false,
  isPeekEngaged = () => false,
  holdLinger = () => false,
  // An opener inside this window (a dropdown in it): its peek leaves the window open.
  holds = () => false,
  delay = DOUBLE_CLICK_MS,
  // Open on the first click: the wait protects a full modal from flashing under a dblclick,
  // but a panel whose popover gesture merely re-shapes the same window (the chat) has nothing to flash.
  eagerClick = false,
  holdMs = LONG_PRESS_MS,
  slop = PRESS_SLOP_PX,
  setTimer = setTimeout,
  clearTimer = clearTimeout,
} = {}) => {
  let clickTimer = null;
  let holdTimer = null;
  let press = null;
  let lastWasTouch = false;
  let swallowClick = false;
  // Advisory (the shell's isPopoverOpen() is the ground truth): null | 'peek' (Alt held) |
  // 'linger' (released while engaged) | 'sticky' (dblclick / right-click / long press).
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
  // An Alt glide on another icon closes this window only when it is popover-shaped.
  const handle = {
    closeFromGlide: (origin) => { if (mode !== null && !(origin && holds(origin))) closeOwn(); },
  };
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
      if (lastWasTouch) return;
      mode = 'sticky';
      openPopover();
    },
    contextmenu() {
      cancelClick();
      cancelHold();
      // Android fires contextmenu for a long press, then sometimes a click.
      swallowClick = true;
      mode = 'sticky';
      openPopover();
    },
    // A shortcut pressed while the hover-peek is open claims the peek instead of toggling.
    // Returns true when it took the press.
    hotkey() {
      if (mode !== 'peek' && mode !== 'linger') return false;
      mode = 'sticky';
      cancelLinger();
      return true;
    },
    // Leaves this machine's own open window alone and closes every other icon's mini window.
    altHover(origin = null) {
      cancelClick();
      if (isPopoverOpen()) return;
      for (const h of glideRegistry) if (h !== handle) h.closeFromGlide(origin);
      mode = 'peek';
      openPopover();
    },
    // Blur too — Alt+Tab eats the keyup. An engaged peek lingers until boxLeave closes it.
    altRelease() {
      if (mode !== 'peek') return;
      if (isPeekEngaged()) { mode = 'linger'; return; }
      closeOwn();
    },
    // Only a lingering window hover-binds; peeks and sticky opens ignore it.
    boxEnter() { cancelLinger(); },
    boxLeave() {
      if (mode !== 'linger' || !isPopoverOpen()) return;
      cancelLinger();
      lingerTimer = setTimer(() => {
        lingerTimer = null;
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
          swallowClick = true;
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
    // The owner closed the window through its own means; without this 'sticky' leaks and a
    // later glide would close a window the user opened deliberately.
    notifyClosed() { mode = null; cancelLinger(); },
  };
};

// DOM wiring for the machine above (pen counts as touch — it long-presses the same way).
export const wireModalOpenGestures = (btn, { openFull, openPopover, closePopover, isPopoverOpen,
                                             isPeekEngaged, holdLinger, holds, eagerClick }) => {
  // Explicit list: an option added to createModalOpenGesture must be added here too or it
  // is silently dropped (tests/ui/tip/popover.test.js pins it).
  const g = createModalOpenGesture({ openFull, openPopover, closePopover, isPopoverOpen,
                                     isPeekEngaged, holdLinger, holds, eagerClick });
  // A disabled icon opens nothing. Checked live: disabled controls keep pointer events on
  // (layout/buttonStates.css — the disabled-reason tooltip needs the hover).
  const enabled = () => !btn.disabled;
  btn.__stencilGestures = g;
  btn.addEventListener('click', () => { if (enabled()) g.click(); });
  btn.addEventListener('dblclick', (e) => { e.preventDefault(); if (enabled()) g.dblclick(); });
  btn.addEventListener('contextmenu', (e) => { e.preventDefault(); if (enabled()) g.contextmenu(); });
  btn.addEventListener('pointerdown', (e) => { if (enabled()) g.pressStart({ x: e.clientX, y: e.clientY, touch: e.pointerType !== 'mouse' }); });
  btn.addEventListener('pointermove', (e) => g.pressMove({ x: e.clientX, y: e.clientY }));
  btn.addEventListener('pointerup', () => g.pressEnd());
  btn.addEventListener('pointercancel', () => g.pressEnd());
  // Both orders: gliding on with Alt held, and pressing Alt while resting on the icon.
  // preventDefault keeps bare Alt off the browser menu bar; only the key route defers to a field.
  btn.addEventListener('mouseenter', (e) => { if (e.altKey && enabled()) g.altHover(); });
  onAltKeys({
    press: (e) => {
      if (!btn.matches(':hover') || !enabled() || isTypingInFocus()) return;
      e.preventDefault();
      g.altHover();
    },
    release: () => g.altRelease(),
  });
  return g;
};
