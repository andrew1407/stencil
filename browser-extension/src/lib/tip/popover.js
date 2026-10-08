// ── Popover placement (panel dialogs) ───────────────────────────────────────
// Below the anchor, flipped above when above has more room, clamped to the viewport. Port of
// browser/js/ui/tip/popover.js popoverPosition and createModalOpenGesture (portParity.test.js).

import constants from './constants.json' with { type: 'json' };

const { POPOVER } = constants;
export const DOUBLE_CLICK_MS = POPOVER.doubleClickMs;
export const LONG_PRESS_MS = POPOVER.longPressMs;
export const PRESS_SLOP_PX = POPOVER.pressSlopPx;

// `{anchor, box, viewport}` rects; `gap` = anchor↔box space, `margin` = least viewport inset.
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
    // A drag took the press and owns what it opens: nothing armed before it fires — a click still
    // waiting out the double-click interval, the long press, a peek's close on Alt release.
    dragged() { cancelClick(); cancelHold(); cancelLinger(); mode = null; press = null; },
    // The owner closed the window through its own means; without this 'sticky' leaks and a
    // later glide would close a window the user opened deliberately.
    notifyClosed() { mode = null; cancelLinger(); },
  };
};
