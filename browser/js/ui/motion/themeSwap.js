import { edgeJitter, edgeBaseOf } from '../dustCloud.js';
import { TUNE, styleCode } from './tune.js';
// The new palette floods out of the control that changed it: View Transitions where they
// exist, a cross-fade elsewhere; `apply` runs exactly once, synchronously. One length across
// all three surfaces (extension accent.js SWAP_MS, desktop themeSwapOverlay.hpp SWAP_MS).
export const THEME_SWAP_MS = TUNE.THEME_SWAP_MS;
export const THEME_SWAP_CLASS = 'theme-swapping';
// Held on <html> while the view transition captures, so the snapshot is the FINAL palette.
export const THEME_INSTANT_CLASS = 'theme-instant';

// The origin is the control that owns the change (originOfId), never the last pointerdown;
// desktop mainWindow.cpp applyTheme() agrees. No control on screen → the viewport centre.

export const swapRadius = (x, y, w, h) => Math.hypot(Math.max(x, w - x), Math.max(y, h - y));

// Percentages, never pixels: an engine measuring the pseudo-element's box in DEVICE pixels
// paints a px origin at half its offset. Percent radii resolve against sqrt(w² + h²) / √2.
export function swapPercent(x, y, w, h) {
  if (!(w > 0 && h > 0)) return { x: 50, y: 50, r: 150 };   // no viewport to measure (a stub)
  const pc = (v) => Math.round(v * 1000) / 1000;
  return { x: pc((100 * x) / w), y: pc((100 * y) / h),
           r: pc((100 * Math.SQRT2 * swapRadius(x, y, w, h)) / Math.hypot(w, h)) };
}

// The Y of the wipe's cubic-bezier at t, solved as the desktop does (themeSwapOverlay.hpp
// swapEase): bisection on the monotonic X. The dust is seeded off this curve.
export const bezierY = (t, x1, y1, x2, y2) => {
  let lo = 0, hi = 1, u = t;
  for (let i = 0; i < 24; i++) {
    u = 0.5 * (lo + hi);
    const x = 3 * (1 - u) * (1 - u) * u * x1 + 3 * (1 - u) * u * u * x2 + u * u * u;
    if (x < t) lo = u; else hi = u;
  }
  return 3 * (1 - u) * (1 - u) * u * y1 + 3 * (1 - u) * u * u * y2 + u * u * u;
};
export const swapEase = (t) => bezierY(t, 0.4, 0.25, 0.95, 1);

// The wipe's edge wears the particle style (dustCloud.js edgeJitter). Twins:
// themeSwapOverlay.hpp edgeRadiusAt, extension accent.js edgePolygon.
export const SWAP_EDGE_POINTS = TUNE.SWAP_EDGE_POINTS;

const swapEdgeJitter = (k, style = styleCode()) => edgeJitter(style, k, SWAP_EDGE_POINTS);

// One end state of the clip as a polygon() in viewport percentages: `grow` 0 is every
// vertex at the origin, 1 the full ring. The base overshoots by the style's deepest dip
// (edgeBaseOf) so the finished ring still clears the furthest corner.
export function swapEdgePolygon(x, y, w, h, grow, style = styleCode()) {
  if (!(w > 0 && h > 0)) return '';   // no viewport to measure (a stub)
  const pc = (v) => Math.round(v * 1000) / 1000;
  const base = swapRadius(x, y, w, h) * edgeBaseOf(style);
  const pts = [];
  for (let k = 0; k < SWAP_EDGE_POINTS; k++) {
    const a = (k / SWAP_EDGE_POINTS) * 2 * Math.PI;
    const r = grow ? base * (1 + swapEdgeJitter(k, style)) : 0;
    pts.push(`${pc(((x + Math.cos(a) * r) / w) * 100)}% ${pc(((y + Math.sin(a) * r) / h) * 100)}%`);
  }
  return `polygon(${pts.join(', ')})`;
}
