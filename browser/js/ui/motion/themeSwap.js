import { edgeJitter, edgeBaseOf } from '../dustCloud.js';
import { TUNE, styleCode } from './tune.js';
// ── Theme / accent swap ─────────────────────────────────────────────────────
// The new palette floods out of the control that changed it, as a growing circle —
// the native View Transitions API where it exists, a plain cross-fade elsewhere.
// Either way `apply` runs exactly once and synchronously.
// Drives both the wipe (as --swap-ms) and the colour cross-fade. One length across
// all three surfaces (extension accent.js SWAP_MS, desktop themeSwapOverlay.hpp kSwapMs).
export const THEME_SWAP_MS = TUNE.THEME_SWAP_MS;
export const THEME_SWAP_CLASS = 'theme-swapping';
// Held on <html> only while the view transition captures the new state, so the snapshot
// is the FINAL palette rather than one caught mid colour-transition.
export const THEME_INSTANT_CLASS = 'theme-instant';

// Where the swap starts: the CONTROL that owns the change, resolved by the caller or by
// originOfId below — never the last pointerdown, which can be an unrelated press.
// desktop mainWindow.cpp applyTheme() names the same trap ("the cursor is NOT good
// enough"); the two surfaces agree. No control on screen → the viewport centre.

// The radius that reaches the furthest viewport corner — the circle has to cover the
// whole page, and the corner opposite the origin is the last place it gets to. Pure.
export const swapRadius = (x, y, w, h) => Math.hypot(Math.max(x, w - x), Math.max(y, h - y));

// The wipe rides in as PERCENTAGES of the viewport, never pixels: clip-path resolves them
// against the pseudo-element's own box, and an engine that measures that box in DEVICE
// pixels paints a px origin at half its offset — the circle blooming above and to the left
// of the icon. Percent radii resolve against sqrt(w² + h²) / √2, hence the √2. Pure.
export function swapPercent(x, y, w, h) {
  if (!(w > 0 && h > 0)) return { x: 50, y: 50, r: 150 };   // no viewport to measure (a stub)
  const pc = (v) => Math.round(v * 1000) / 1000;
  return { x: pc((100 * x) / w), y: pc((100 * y) / h),
           r: pc((100 * Math.SQRT2 * swapRadius(x, y, w, h)) / Math.hypot(w, h)) };
}

// Where the ring IS at time-fraction t: the Y of the wipe's own cubic-bezier, solved the
// same way the desktop evaluates these control points (themeSwapOverlay.hpp swapEase) —
// bisection on the monotonic X, then read Y. The dust below is seeded off this curve, so
// the motes ride the very ring the clip-path draws. `bezierY` is shared with the grain's
// own curve below. Pure — unit-tested.
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

// ── The front ───────────────────────────────────────────────────────────────
// The wipe's edge wears the particle style (dustCloud.js edgeJitter): a polygon ring whose
// vertices ride the wipe's easing, each pushed off the nominal radius by the style's own
// recipe. Twins: themeSwapOverlay.hpp edgeRadiusAt, extension accent.js edgePolygon.
export const SWAP_EDGE_POINTS = TUNE.SWAP_EDGE_POINTS;

// The per-vertex reach multiplier of vertex k: 1 + the style's jitter. Pure.
const swapEdgeJitter = (k, style = styleCode()) => edgeJitter(style, k, SWAP_EDGE_POINTS);

// One end state of the clip, as a polygon() string in viewport percentages (the same
// device-pixel-engine trap swapPercent dodges). `grow` 0 is the collapsed start —
// every vertex AT the origin — and 1 the full ring; CSS interpolates between the two. The
// base overshoots by the style's deepest dip (edgeBaseOf) so the finished ring still
// clears the furthest corner — coverage is non-negotiable. Pure — unit-tested.
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
