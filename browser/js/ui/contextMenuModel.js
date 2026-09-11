// Pure placement + pointer maths for the context menu's flyouts. No DOM, no timers: the
// caller measures, this decides. Kept out of contextMenuNav.js so the clamping rules
// (open right, flip left at the edge, never off the top) are testable on numbers alone.

/** Viewport margin the flyout keeps on the right/bottom edges, and the hard left/top floor. */
export const SUB_EDGE_PAD = 6;
export const SUB_MIN_INSET = 4;
/** Gap between the parent row and its flyout. */
export const SUB_GAP = 2;

/**
 * Where a submenu of size sw×sh belongs beside a parent row: to its right by default,
 * flipped to its left when that would cross the viewport's right edge, then clamped.
 */
export function submenuPlacement(ir, sw, sh, vw, vh) {
  let left = ir.right + SUB_GAP;
  if (left + sw > vw - SUB_EDGE_PAD) left = ir.left - sw - SUB_GAP;
  if (left < SUB_MIN_INSET) left = SUB_MIN_INSET;
  let top = ir.top;
  if (top + sh > vh - SUB_EDGE_PAD) top = vh - sh - SUB_EDGE_PAD;
  if (top < SUB_MIN_INSET) top = SUB_MIN_INSET;
  return { left, top };
}

/** Two pointer samples at the very same pixel — the cursor has not moved. */
export const samePoint = (a, b) => !!a && !!b && a.x === b.x && a.y === b.y;
