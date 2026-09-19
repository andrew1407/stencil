// Scrollbar hover for every scrollable that keeps its NATIVE bars (the canvas has its own
// overlay ones — canvasScrollbars.js): `.sb-hover` on the element whose bar is under the
// pointer, so the thumb takes the accent only then (css/layout/scrollbars.css). A pure hit-test plus
// one document listener. The extension carries a rule-for-rule port
// (src/lib/scrollbarHover.js; its tests/portParity.test.js pins the two bodies identical).

// Edges inclusive; takes anything with left/right/top/bottom (a DOMRect in practice).
const inRect = (x, y, rect) =>
  x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;

// On the scrollbar strip = within the last `strip` px of the right edge (vertical) or bottom
// (horizontal). Overlay bars take no layout space, so it is measured off the element's own box.
export const SCROLLBAR_STRIP_PX = 14;
export const scrollbarHit = (box, x, y, { canX = false, canY = false, strip = SCROLLBAR_STRIP_PX } = {}) => {
  if (!inRect(x, y, box)) return false;
  if (canY && x >= box.right - strip) return true;
  if (canX && y >= box.bottom - strip) return true;
  return false;
};

// The scrolling element whose own bar is under (x, y), walking up from `target`: it must overflow
// AND show a bar (a viewport hiding native bars behind overlay ones is skipped). null when none.
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

// The thumb takes the accent ONLY while the pointer rests on a bar, not whenever the panel is
// hovered: scrollbar-color has no thumb-hover, so panel-wide hover accented every thumb (user report).
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
