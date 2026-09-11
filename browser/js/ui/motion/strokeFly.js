import { tileNoise } from './tiles.js';
import { TUNE } from './tune.js';
// ── Drawing a stroke: the new vertex FLIES to where you put it ──────────────
// Every route that adds a point ends in one motion: the vertex leaves where it came from
// — the point it extends, or its projection on the segment it splits — and travels to the
// click on a bowed path, overshooting before it settles. The renderer paints the flown
// position, so the segments hanging off it bend and join by themselves.
// C++ mirror: desktop/src/canvas/strokeGrowth.hpp — keep the two in step.

// The flight's own length: a short hop is nearly instant, a reach across the page
// still lands promptly. Lengths are IMAGE pixels on both sides, so zoom does not
// change the timing. Pure.
export const STROKE_FLY_MIN_MS = TUNE.STROKE_FLY_MIN_MS;
export const STROKE_FLY_MAX_MS = TUNE.STROKE_FLY_MAX_MS;
export const STROKE_FLY_PX_PER_MS = TUNE.STROKE_FLY_PX_PER_MS;
export const strokeFlyMs = (len) => Math.min(
  STROKE_FLY_MAX_MS, STROKE_FLY_MIN_MS + Math.max(0, len) / STROKE_FLY_PX_PER_MS);

// Ease-out-back: the vertex shoots a little past its target and comes back, which is
// what makes the segment read as REACHING for the point rather than being switched on.
// Weaker than the textbook 1.70158 — on a 3px stroke a big overshoot reads as a glitch.
const STROKE_FLY_BACK = TUNE.STROKE_FLY_BACK;
export const strokeFlyEase = (t) => {
  if (t <= 0) return 0;
  if (t >= 1) return 1;
  const u = t - 1;
  return 1 + (STROKE_FLY_BACK + 1) * u * u * u + STROKE_FLY_BACK * u * u;
};

// No vertex flies a straight line (the same rule the dust follows — tileWaypoint): it
// is pushed off its path by a share of the trip, capped, and back by the time it
// lands. `bow` is the signed side, -1..1. Pure.
const STROKE_BOW_SHARE = TUNE.STROKE_BOW_SHARE;
export const STROKE_BOW_MAX = TUNE.STROKE_BOW_MAX;
export const strokeBow = (len) => Math.min(len * STROKE_BOW_SHARE, STROKE_BOW_MAX);

// Which side, and how far off, THIS vertex swings — a hash of where it landed, so the
// bend is varied but reproducible (and unit-testable). Pure.
export const strokeBowSign = (x, y) => (tileNoise(Math.round(x), Math.round(y)) - 0.5) * 2;

// The envelope every mid-flight flourish rides: nothing at either end, everything at the
// half-way mark. A parabola, not a sine, because it is exactly zero at both ends —
// sin(pi) is not, and a vertex landing a hair off the point it became has not landed.
export const strokeArc = (t) => {
  const k = Math.min(1, Math.max(0, t));
  return 4 * k * (1 - k);
};

// Where the vertex is `t` through its flight. Pure.
export const strokeFlyPoint = (from, to, t, bow = 0) => {
  const k = strokeFlyEase(t);
  const dx = to.x - from.x;
  const dy = to.y - from.y;
  const x = from.x + dx * k;
  const y = from.y + dy * k;
  const len = Math.hypot(dx, dy);
  if (!bow || len < 0.5) return { x, y };
  const s = strokeArc(t) * strokeBow(len) * bow;
  return { x: x - (dy / len) * s, y: y + (dx / len) * s };
};

// The landing: the vertex arrives half again its size and settles. POP is the settle;
// the swell itself happens in flight (strokeFlyRadius), so there is no jump between
// the two — a size that snaps on arrival reads as a redraw, not a landing.
const STROKE_POP_MS = TUNE.STROKE_POP_MS;
export const STROKE_POP_PEAK = TUNE.STROKE_POP_PEAK;
export const STROKE_FLY_R0 = TUNE.STROKE_FLY_R0;
export const strokeFlyRadius = (t) =>
  STROKE_FLY_R0 + (STROKE_POP_PEAK - STROKE_FLY_R0) * Math.min(1, Math.max(0, t)) ** 2;
export const strokePopScale = (u) => {
  if (u >= 1) return 1;
  const k = 1 - Math.min(1, Math.max(0, u));
  return 1 + (STROKE_POP_PEAK - 1) * k * k;
};

// The ring the landing pushes out — the one part of this that is not the line itself,
// so it stays faint and brief.
export const STROKE_RIPPLE_MS = TUNE.STROKE_RIPPLE_MS;
export const STROKE_RIPPLE_REACH = TUNE.STROKE_RIPPLE_REACH;
export const strokeRipple = (u) => {
  const k = Math.min(1, Math.max(0, u));
  return {
    scale: 1 + (STROKE_RIPPLE_REACH - 1) * (1 - (1 - k) ** 2),
    alpha: TUNE.STROKE_RIPPLE_ALPHA * (1 - k) ** 1.6,
  };
};

// The glow riding the vertex in flight: nothing at either end (it must not smudge the
// anchor it left or the point it became), brightest mid-trip.
const STROKE_SPARK_REACH = TUNE.STROKE_SPARK_REACH;
export const strokeSpark = (t) => {
  const k = strokeArc(t);
  return { scale: 1 + (STROKE_SPARK_REACH - 1) * k, alpha: TUNE.STROKE_SPARK_ALPHA * k ** 0.7 };
};

// How hot the segments the vertex is dragging burn, over the whole flight + settle:
// full as it leaves, out by the time it has landed.
export const STROKE_WAKE_ALPHA = TUNE.STROKE_WAKE_ALPHA;
export const strokeWake = (t) => STROKE_WAKE_ALPHA * (1 - Math.min(1, Math.max(0, t))) ** 1.3;

// The whole timeline of one vertex, from an elapsed time. `land` drives the settle,
// `ripple` the ring; both start the moment the flight ends. Pure.
export const strokePhase = (elapsed, flyMs) => {
  const fly = flyMs > 0 ? Math.min(1, Math.max(0, elapsed / flyMs)) : 1;
  const after = Math.max(0, elapsed - flyMs);
  return {
    fly,
    land: Math.min(1, after / STROKE_POP_MS),
    ripple: Math.min(1, after / STROKE_RIPPLE_MS),
    span: Math.min(1, elapsed / (flyMs + STROKE_RIPPLE_MS)),
    done: elapsed >= flyMs + STROKE_RIPPLE_MS,
  };
};

// The vertex's radius multiplier at any point in that timeline. Pure.
export const strokeVertexScale = (ph) => (ph.fly < 1 ? strokeFlyRadius(ph.fly) : strokePopScale(ph.land));

// Where a vertex INSERTED into a segment comes from: its own foot on the straight line
// it split, so the bend is pulled out of the stroke instead of appearing beside it.
// Clamped to the segment, so a foot beyond an end is that end. Pure.
export const strokeFoot = (a, b, x, y) => {
  if (!a || !b) return { x, y };
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const lenSq = dx * dx + dy * dy;
  if (lenSq === 0) return { x: a.x, y: a.y };
  const t = Math.min(1, Math.max(0, ((x - a.x) * dx + (y - a.y) * dy) / lenSq));
  return { x: a.x + t * dx, y: a.y + t * dy };
};
