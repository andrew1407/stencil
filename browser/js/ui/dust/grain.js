// ── A grain's own shape ──────────────────────────────────────────────────────
// A grain keeps one shape for its flight (picked off its hash) and lies along its heading, plus the
// jittered edge a styled cloud wears. Geometry in radii. Desktop: dustKit.hpp grainShape.
import { PALETTE_STOPS, STYLE_FIRE, STYLE_WATER, TINT_CSS, fract, hashNoise, TUNE } from './flight.js';

// A grain keeps one shape for its flight (picked off its hash) and lies along its heading.
// Geometry in radii, so every shape covers about the disc's area. Desktop: dustKit.hpp grainShape.
export const SHAPE_DISC = 0;
export const SHAPE_OVAL = 1;
export const SHAPE_WAVE = 2;
export const SHAPE_TRIANGLE = 3;
export const SHAPE_STREAK = 4;
// A styled grain is bigger and dearer than a speck, so a screen-sized cloud grids at this
// many times the cell — about half the grains — under water and fire.
export const STYLED_CELL_SCALE = TUNE.STYLED_CELL_SCALE;
export const WATER_WAVE_SHARE = TUNE.WATER_WAVE_SHARE;   // of water grains are wave lines, the rest ovals
export const FIRE_STREAK_SHARE = TUNE.FIRE_STREAK_SHARE; // of fire grains are spark streaks, the rest triangles
export const SHAPES = TUNE.SHAPES;
export const grainShape = (style, w) => {
  const pick = fract((w || 0) * 7.31 + 0.17);
  if (style === STYLE_WATER) return pick < WATER_WAVE_SHARE ? SHAPE_WAVE : SHAPE_OVAL;
  if (style === STYLE_FIRE) return pick < FIRE_STREAK_SHARE ? SHAPE_STREAK : SHAPE_TRIANGLE;
  return SHAPE_DISC;
};
// The heading of a grain that flies `dx, dy` from home: the direction it travels, so a
// gather (flown from the far end home) points the other way.
export const headingOf = (dx, dy, fromFar) => Math.atan2(dy, dx) + (fromFar ? Math.PI : 0);
// A polygon shape at (x, y), radius r, heading a: flat [x0, y0, x1, y1, …]. Pure.
export const shapePolygon = (shape, x, y, r, a, out = []) => {
  out.length = 0;
  const c = Math.cos(a), s = Math.sin(a);
  const put = (u, v) => { out.push(x + u * c - v * s, y + u * s + v * c); };   // u along, v across
  if (shape === SHAPE_TRIANGLE) {
    const t = SHAPES.triangle;
    put(t.tip * r, 0); put(-t.base * r, t.half * r); put(-t.base * r, -t.half * r);
  } else if (shape === SHAPE_STREAK) {
    const t = SHAPES.streak;
    put(t.head * r, t.headHalf * r); put(-t.tail * r, t.tailHalf * r);
    put(-t.tail * r, -t.tailHalf * r); put(t.head * r, -t.headHalf * r);
  } else if (shape === SHAPE_WAVE) {
    const t = SHAPES.wave;
    const rim = (i, side) => {
      const k = i / (t.samples - 1);
      put((k - 0.5) * t.len * r, Math.sin(k * t.waves * 2 * Math.PI) * t.amp * r + side * t.half * r);
    };
    for (let i = 0; i < t.samples; i++) rim(i, 1);
    for (let i = t.samples - 1; i >= 0; i--) rim(i, -1);
  }
  return out;
};
// Add one grain's outline to the path being built on `ctx` (no fill here — the caller
// batches many grains into one fill).
export const addGrainPath = (ctx, shape, x, y, r, a, scratch = []) => {
  if (shape === SHAPE_DISC) { ctx.moveTo(x + r, y); ctx.arc(x, y, r, 0, Math.PI * 2); return; }
  if (shape === SHAPE_OVAL) {
    const rx = SHAPES.oval.rx * r, ry = SHAPES.oval.ry * r;
    ctx.moveTo(x + Math.cos(a) * rx, y + Math.sin(a) * rx);
    ctx.ellipse(x, y, rx, ry, a, 0, Math.PI * 2);
    return;
  }
  const pts = shapePolygon(shape, x, y, r, a, scratch);
  ctx.moveTo(pts[0], pts[1]);
  for (let i = 2; i < pts.length; i += 2) ctx.lineTo(pts[i], pts[i + 1]);
  ctx.closePath();
};

// The palette wipe's ring EDGE wears the style. `edgeJitter` is vertex k's reach off the nominal
// radius, as a share of it; `edgeDipOf` the deepest dip inward.
export const EDGE_POINTS = TUNE.EDGE_POINTS;
export const EDGE = TUNE.EDGE;
export const edgeJitter = (style, k, points = EDGE_POINTS) => {
  const t = k / points;
  if (style === STYLE_WATER) {
    const e = EDGE.water;
    return e.amp * Math.sin(2 * Math.PI * e.waves * t) + e.rippleAmp * Math.sin(2 * Math.PI * e.ripple * t + 1);
  }
  if (style === STYLE_FIRE) {
    const e = EDGE.fire;
    const tongue = Math.floor(t * e.tongues), u = fract(t * e.tongues);
    const h = e.base + e.vary * hashNoise(tongue, 5);
    return h * Math.pow(Math.sin(Math.PI * u), 3) - e.dip + e.jag * (hashNoise(k, 7) * 2 - 1);
  }
  return 0;
};
export const edgeDipOf = (style) => (style === STYLE_WATER ? EDGE.water.amp + EDGE.water.rippleAmp
  : style === STYLE_FIRE ? EDGE.fire.dip + EDGE.fire.jag : 0);
export const edgeReachOf = (style) => (style === STYLE_WATER ? EDGE.water.amp + EDGE.water.rippleAmp
  : style === STYLE_FIRE ? EDGE.fire.base + EDGE.fire.vary + EDGE.fire.jag : 0);
// The ring's base radius, as a share of the corner-reaching one: the deepest dip (plus
// slack) still clears the furthest corner when the wipe ends.
export const edgeBaseOf = (style) => 1 + edgeDipOf(style) + 0.012;

// The palette itself, as CSS: PALETTE_STOPS even mixes of --accent and --accent-2 — so a
// cloud is violet by default and follows the accent — then the TINT_CSS minority.
export const paletteCss = (stops = PALETTE_STOPS) => [
  ...Array.from({ length: stops }, (_, i) =>
    `color-mix(in srgb, var(--accent) ${Math.round(100 - (100 * i) / (stops - 1))}%, var(--accent-2))`),
  ...TINT_CSS,
];
