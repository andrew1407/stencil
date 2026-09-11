import { core } from '../core/stencilCore.js';
// ── Geometry helpers (pure) ─────────────────────────────────────
// Distance from point (px,py) to the segment a→b. Delegates to the shared C++
// core (wasm) when loaded; the JS body is the reference + fallback.
export const distToSegment = core.bind('distToSegment', (px, py, a, b) => {
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const lenSq = dx * dx + dy * dy;
  if (lenSq === 0) return Math.hypot(px - a.x, py - a.y);
  const t = Math.max(0, Math.min(1, ((px - a.x) * dx + (py - a.y) * dy) / lenSq));
  return Math.hypot(px - (a.x + t * dx), py - (a.y + t * dy));
});



// Edges inclusive; takes anything with left/right/top/bottom (a DOMRect in practice).
export const pointInRect = (x, y, rect) =>
  x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
