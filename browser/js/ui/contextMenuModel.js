// Pure placement + pointer maths for the context menu's flyouts: the caller measures, this decides.

export const SUB_EDGE_PAD = 6;
export const SUB_MIN_INSET = 4;
export const SUB_GAP = 2;

// To the right by default, flipped left when that would cross the viewport's edge, then clamped.
export function submenuPlacement(ir, sw, sh, vw, vh) {
  let left = ir.right + SUB_GAP;
  if (left + sw > vw - SUB_EDGE_PAD) left = ir.left - sw - SUB_GAP;
  if (left < SUB_MIN_INSET) left = SUB_MIN_INSET;
  let top = ir.top;
  if (top + sh > vh - SUB_EDGE_PAD) top = vh - sh - SUB_EDGE_PAD;
  if (top < SUB_MIN_INSET) top = SUB_MIN_INSET;
  return { left, top };
}

export const samePoint = (a, b) => !!a && !!b && a.x === b.x && a.y === b.y;
