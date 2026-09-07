// Rule-for-rule PORT of browser/js/ui/scrollbarHover.js (tests/portParity.test.js pins
// the two bodies identical): `.sb-hover` on the scrollable whose native bar is under the
// pointer, so the thumb takes the accent only then (lib/theme.css). Every extension page
// (popup / side panel / devtools panel via popup.js, crop, options) calls
// wireScrollbarHover() once.

// Edges inclusive; takes anything with left/right/top/bottom (a DOMRect in practice).
const inRect = (x, y, rect) =>
  x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;

// Whether a pointer at (x, y) is on a scrollable element's scrollbar strip: the last
// `strip` px along its right edge (vertical bar, when `canY`) or bottom edge (horizontal,
// when `canX`). Overlay bars take no layout space, so the strip is measured off the
// element's own box. Pure, so the geometry is unit-testable.
export const SCROLLBAR_STRIP_PX = 14;
export const scrollbarHit = (box, x, y, { canX = false, canY = false, strip = SCROLLBAR_STRIP_PX } = {}) => {
  if (!inRect(x, y, box)) return false;
  if (canY && x >= box.right - strip) return true;
  if (canX && y >= box.bottom - strip) return true;
  return false;
};

// The scrolling element whose own scrollbar is under (x, y), walking up from `target`:
// one that overflows AND shows a bar for it (overflow auto/scroll; a viewport that hides
// its native bars behind overlay ones — scrollbar-width: none — is skipped). null when none.
export const scrollbarOwnerAt = (target, x, y) => {
  for (let el = target; el && el.nodeType === 1; el = el.parentElement) {
    const overY = el.scrollHeight > el.clientHeight;
    const overX = el.scrollWidth > el.clientWidth;
    if (!overY && !overX) continue;
    const st = getComputedStyle(el);
    if (st.scrollbarWidth === 'none') continue;
    const canY = overY && /auto|scroll/.test(st.overflowY);
    const canX = overX && /auto|scroll/.test(st.overflowX);
    if ((canY || canX) && scrollbarHit(el.getBoundingClientRect(), x, y, { canX, canY })) return el;
  }
  return null;
};

// Marks the scrolling element under the pointer with `sb-hover` while the pointer rests
// on one of ITS scrollbars — the thumb takes the accent ONLY then, not whenever the
// panel is hovered: the standard scrollbar-color property Chrome/Firefox read has no
// thumb-hover of its own, and a panel-wide hover made every scroll paint the thumb
// accent (user report). One document listener covers every scrollable there is or will
// be (dialogs, menus, the chat) — nothing to wire per panel.
export const wireScrollbarHover = (root = document) => {
  let current = null;
  const set = (el) => {
    if (el === current) return;
    if (current) current.classList.remove('sb-hover');
    current = el;
    if (el) el.classList.add('sb-hover');
  };
  root.addEventListener('mousemove', (e) => set(scrollbarOwnerAt(e.target, e.clientX, e.clientY)), { passive: true });
  // Out of the document (relatedTarget null): nothing is hovered any more.
  root.addEventListener('mouseout', (e) => { if (!e.relatedTarget) set(null); });
};
