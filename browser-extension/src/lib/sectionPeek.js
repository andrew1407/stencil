// Alt+hover peek: a folded section's real body, moved into a small anchored panel for
// exactly as long as Alt is down (port of browser/js/ui/popover.js). Pure bookkeeping —
// every DOM action and both clocks are injected.
import { popoverPosition } from './popover.js';

// Long enough to travel from the folded header into the panel.
export const PEEK_CLOSE_GRACE_MS = 250;

// While a text control has focus, Alt belongs to the typing. Mirrors browser/js/utils.js.
export const isTypingTarget = (t) => {
  if (!t) return false;
  const tag = (t.tagName || '').toLowerCase();
  if (tag === 'textarea' || tag === 'select') return true;
  if (tag === 'input') {
    const ty = (t.type || '').toLowerCase();
    return !(ty === 'checkbox' || ty === 'radio' || ty === 'file' || ty === 'color' || ty === 'button');
  }
  return t.isContentEditable === true;
};

// A peek IS a popover, with a tighter gap: it defers to popover.js rather than restating
// the rules, which is how this copy came to keep flipping on overflow alone after the
// shared one moved to "whichever side has more room".
export const peekPosition = ({ anchor, box, viewport, gap = 6, margin = 8 }) =>
  popoverPosition({ anchor, box, viewport, gap, margin });

// Section tokens are opaque, so id-less sections peek too.
export const createSectionPeek = ({
  isCollapsed,
  open,
  close,
  isEngaged = () => false,
  grace = PEEK_CLOSE_GRACE_MS,
  setTimer = setTimeout,
  clearTimer = clearTimeout,
} = {}) => {
  let current = null;
  let closeTimer = null;
  const cancelClose = () => { if (closeTimer !== null) { clearTimer(closeTimer); closeTimer = null; } };
  const shut = () => {
    cancelClose();
    if (current === null) return;
    const s = current;
    current = null;        // cleared FIRST — close() may relayout and re-fire hover
    close(s);
  };
  const openFor = (section) => {
    cancelClose();
    if (current === section) return;
    if (!isCollapsed(section)) return;
    if (current !== null) { const prev = current; current = null; close(prev); }
    current = section;
    open(section);
  };
  return {
    enterHead(section, altHeld) { if (altHeld && section) openFor(section); },
    altPressed(section) { if (section) openFor(section); },
    enterPeek() { if (current !== null) cancelClose(); },
    // Re-checked when the grace lands: an engaged panel (focus in its composer) stays.
    leave() {
      if (current === null) return;
      cancelClose();
      closeTimer = setTimer(() => {
        closeTimer = null;
        if (current !== null && isEngaged(current)) return;
        shut();
      }, grace);
    },
    // Blur too — Alt+Tab eats the keyup.
    altReleased() {
      if (current !== null && isEngaged(current)) return;
      shut();
    },
    dismiss() { shut(); },
    // A peeked body must go home BEFORE the accordion class flips.
    sectionToggled(section) { if (current === section) shut(); },
    isOpen: () => current !== null,
    openSection: () => current,
  };
};
