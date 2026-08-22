// ── Alt+hover peek: a collapsed section's body in a floating mini window ─────
// Holding Alt and hovering a folded .fsection header shows the SAME body — the real
// node, moved, wiring intact — in a small anchored panel, without unfolding the
// accordion (popup port of the editor's Alt+hover peek, browser/js/ui/popover.js).
// HOLD-to-peek: the panel lives exactly as long as Alt is down. Pure decision +
// bookkeeping only: every DOM action and both clocks are INJECTED by popup.js, so
// `node --test` drives the whole thing with stubs.

// Leaving the header or the panel closes the peek after this grace — long enough
// to travel from the folded header into the panel without it vanishing.
export const PEEK_CLOSE_GRACE_MS = 250;

// Whether an element is a text-entry control: while one has FOCUS, Alt belongs to the
// typing, so the peek must not fire. Mirrors the browser app's isTypingTarget
// (browser/js/utils.js): checkbox/radio/file/color/button inputs are NOT typing targets.
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

// Where the panel sits, in viewport coordinates: below the anchor with left edges
// aligned, flipped above on bottom overflow, clamped inside the viewport. The
// popoverPosition rules (browser popover.js), kept case-for-case — pure, so placement
// is testable without layout. `anchor`/`viewport` are rect-likes; returns { left, top }.
export const peekPosition = ({ anchor, box, viewport, gap = 6, margin = 8 }) => {
  let top = anchor.bottom + gap;
  if (top + box.height > viewport.height - margin) {
    const above = anchor.top - gap - box.height;
    top = above >= margin ? above : Math.max(margin, viewport.height - margin - box.height);
  }
  const left = Math.max(margin, Math.min(anchor.left, viewport.width - margin - box.width));
  return { left, top };
};

// The peek's open/close machine. Section tokens are OPAQUE (popup.js passes the
// .fsection elements, so id-less sections peek too); only a collapsed section peeks,
// and open/close (show the body in the panel / return it) + timers are injected.
export const createSectionPeek = ({
  isCollapsed,
  open,
  close,
  isEngaged = () => false,
  grace = PEEK_CLOSE_GRACE_MS,
  setTimer = setTimeout,
  clearTimer = clearTimeout,
} = {}) => {
  let current = null;      // the section whose body is out in the panel
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
    if (current === section) return;         // already peeking it — idempotent
    if (!isCollapsed(section)) return;       // an expanded section shows itself
    if (current !== null) { const prev = current; current = null; close(prev); }
    current = section;
    open(section);
  };
  return {
    // Pointer settled on a section header — peek only while Alt is held.
    enterHead(section, altHeld) { if (altHeld && section) openFor(section); },
    // Alt pressed while the pointer already rests on a header.
    altPressed(section) { if (section) openFor(section); },
    // Entering the panel keeps it open (cancels a header-leave in flight).
    enterPeek() { if (current !== null) cancelClose(); },
    // Leaving the header or the panel: close after the grace, so travelling
    // between the two never flickers it shut. Re-checked when the grace lands:
    // an engaged panel (e.g. focus in its composer) stays — the pointer leaving
    // must not yank a window mid-typing.
    leave() {
      if (current === null) return;
      cancelClose();
      closeTimer = setTimer(() => {
        closeTimer = null;
        if (current !== null && isEngaged(current)) return;
        shut();
      }, grace);
    },
    // Alt released (blur too — Alt+Tab eats the keyup): close, unless engaged
    // (pointer inside / typed content) — then the normal close paths take over.
    altReleased() {
      if (current !== null && isEngaged(current)) return;
      shut();
    },
    // Escape / click outside / anything final: close now.
    dismiss() { shut(); },
    // The section was toggled (header click, drag spring): a peeked body must go
    // home BEFORE the accordion class flips, so the caller invokes this first.
    sectionToggled(section) { if (current === section) shut(); },
    isOpen: () => current !== null,
    openSection: () => current,
  };
};
