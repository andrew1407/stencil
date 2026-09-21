import { tileNoise } from './surface/tiles.js';
import { TUNE } from './tune.js';
// A new vertex leaves the point it extends (or its foot on the segment it splits) and
// flies to the click on a bowed path, overshooting before it settles.
// C++ mirror: desktop/src/canvas/draw/strokeGrowth.hpp — keep the two in step.

// Lengths are IMAGE pixels on both sides, so zoom does not change the timing.
export const STROKE_FLY_MIN_MS = TUNE.STROKE_FLY_MIN_MS;
export const STROKE_FLY_MAX_MS = TUNE.STROKE_FLY_MAX_MS;
export const STROKE_FLY_PX_PER_MS = TUNE.STROKE_FLY_PX_PER_MS;
export const strokeFlyMs = (len) => Math.min(
  STROKE_FLY_MAX_MS, STROKE_FLY_MIN_MS + Math.max(0, len) / STROKE_FLY_PX_PER_MS);

// Ease-out-back, weaker than the textbook 1.70158: on a 3px stroke a big overshoot is a glitch.
const STROKE_FLY_BACK = TUNE.STROKE_FLY_BACK;
export const strokeFlyEase = (t) => {
  if (t <= 0) return 0;
  if (t >= 1) return 1;
  const u = t - 1;
  return 1 + (STROKE_FLY_BACK + 1) * u * u * u + STROKE_FLY_BACK * u * u;
};

// Pushed off its path by a share of the trip, capped, like tileWaypoint. `bow` is the signed side.
const STROKE_BOW_SHARE = TUNE.STROKE_BOW_SHARE;
export const STROKE_BOW_MAX = TUNE.STROKE_BOW_MAX;
export const strokeBow = (len) => Math.min(len * STROKE_BOW_SHARE, STROKE_BOW_MAX);

// A hash of where it landed, so the bend is varied but reproducible.
export const strokeBowSign = (x, y) => (tileNoise(Math.round(x), Math.round(y)) - 0.5) * 2;

// A parabola, not a sine: exactly zero at both ends (sin(pi) is not).
export const strokeArc = (t) => {
  const k = Math.min(1, Math.max(0, t));
  return 4 * k * (1 - k);
};

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

// POP is the settle; the swell happens in flight (strokeFlyRadius), so nothing snaps on arrival.
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

export const STROKE_RIPPLE_MS = TUNE.STROKE_RIPPLE_MS;
export const STROKE_RIPPLE_REACH = TUNE.STROKE_RIPPLE_REACH;
export const strokeRipple = (u) => {
  const k = Math.min(1, Math.max(0, u));
  return {
    scale: 1 + (STROKE_RIPPLE_REACH - 1) * (1 - (1 - k) ** 2),
    alpha: TUNE.STROKE_RIPPLE_ALPHA * (1 - k) ** 1.6,
  };
};

const STROKE_SPARK_REACH = TUNE.STROKE_SPARK_REACH;
export const strokeSpark = (t) => {
  const k = strokeArc(t);
  return { scale: 1 + (STROKE_SPARK_REACH - 1) * k, alpha: TUNE.STROKE_SPARK_ALPHA * k ** 0.7 };
};

export const STROKE_WAKE_ALPHA = TUNE.STROKE_WAKE_ALPHA;
export const strokeWake = (t) => STROKE_WAKE_ALPHA * (1 - Math.min(1, Math.max(0, t))) ** 1.3;

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

export const strokeVertexScale = (ph) => (ph.fly < 1 ? strokeFlyRadius(ph.fly) : strokePopScale(ph.land));

// An inserted vertex comes from its foot on the segment it split, clamped to the segment.
export const strokeFoot = (a, b, x, y) => {
  if (!a || !b) return { x, y };
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const lenSq = dx * dx + dy * dy;
  if (lenSq === 0) return { x: a.x, y: a.y };
  const t = Math.min(1, Math.max(0, ((x - a.x) * dx + (y - a.y) * dy) / lenSq));
  return { x: a.x + t * dx, y: a.y + t * dy };
};
