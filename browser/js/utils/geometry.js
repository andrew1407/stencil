// Stays in JS on purpose: hit tests call it per segment per move, and a wasm crossing costs
// more than the maths. The core's twin is pinned to it by tests/wasm/wasm-parity.test.js.
export const distToSegment = (px, py, a, b) => {
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const lenSq = dx * dx + dy * dy;
  if (lenSq === 0) return Math.hypot(px - a.x, py - a.y);
  const t = Math.max(0, Math.min(1, ((px - a.x) * dx + (py - a.y) * dy) / lenSq));
  return Math.hypot(px - (a.x + t * dx), py - (a.y + t * dy));
};

// Its square, with no root: the hit tests compare it against a squared radius. Twin: core distToSegmentSq.
export const distToSegmentSq = (px, py, a, b) => {
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const lenSq = dx * dx + dy * dy;
  let ex = px - a.x, ey = py - a.y;
  if (lenSq !== 0) {
    const t = Math.max(0, Math.min(1, (ex * dx + ey * dy) / lenSq));
    ex = px - (a.x + t * dx);
    ey = py - (a.y + t * dy);
  }
  return ex * ex + ey * ey;
};

export const pointInRect = (x, y, rect) =>
  x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
