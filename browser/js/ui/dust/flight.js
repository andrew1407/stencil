// ── A dust grain's flight ────────────────────────────────────────────────────
// The FLIGHTS table every element-sized cloud is evaluated against, the eased legs behind it, and
// the palette/tint ramp a grain is painted from. Pure arithmetic: no canvas, no DOM.
// Desktop twin: DisintegrateOverlay.hpp legAt. Byte-pinned to browser-extension/src/lib.

// The tuned numbers this painter runs on live in the shared asset, not here.
import MOTION from '../../config/motion.json' with { type: 'json' };
// The family's one door to the shared asset: the other two TUs take TUNE from here.
export const TUNE = MOTION.dust;

// cubic-bezier(x1, y1, x2, y2) at time t: solve x(u) = t by bisection (monotonic in
// x), then read y(u).
export const bezierY = (t, x1, y1, x2, y2) => {
  let lo = 0, hi = 1, u = t;
  for (let i = 0; i < 24; i++) {
    u = 0.5 * (lo + hi);
    const x = 3 * (1 - u) * (1 - u) * u * x1 + 3 * (1 - u) * u * u * x2 + u * u * u;
    if (x < t) lo = u; else hi = u;
  }
  return 3 * (1 - u) * (1 - u) * u * y1 + 3 * (1 - u) * u * u * y2 + u * u * u;
};

// Sampled once into 256 steps with both ends pinned exactly: the solver only bisects to within
// a hair of 0 and 1. Thousands of grains read it per frame; solving per read was the frame.
export const EASE_STEPS = TUNE.EASE_STEPS;
export const easeLut = (x1, y1, x2, y2) => {
  const lut = new Float32Array(EASE_STEPS + 1);
  for (let i = 0; i <= EASE_STEPS; i++) lut[i] = bezierY(i / EASE_STEPS, x1, y1, x2, y2);
  lut[0] = 0;
  lut[EASE_STEPS] = 1;
  return (t) => lut[Math.max(0, Math.min(EASE_STEPS, Math.round(t * EASE_STEPS)))];
};

// A grain flies TWO legs, never a straight line (motion.js tileWaypoint): start → bend on the
// first leg's curve, bend → end on the flight's. `split` is where that keyframe sat.
export const leg = easeLut;
export const FLIGHTS = Object.freeze({
  // A removed row: near-linear into the bend at 38%, then the flight's own ease home.
  scatter: { from: 'home', split: 0.38, leg: leg(0.3, 0.4, 0.7, 0.8), rest: leg(0.22, 0.55, 0.3, 1),
             alpha: [[0, 1], [0.38, 0.85], [1, 0]] },
  // …played backwards for an arriving row: from where the scatter would have flung it,
  // fading up as it comes.
  gather: { from: 'far', split: 0.58, leg: leg(0.3, 0.2, 0.7, 0.6), rest: leg(0.7, 0, 0.78, 0.45),
            alpha: [[0, 0], [0.22, 0.75], [0.58, 0.9], [1, 1]] },
  // A surface's motes ARE the window, visible from frame one, and their ease-out covers most of
  // the trip in the first fifth — so the bend sits early enough to be seen. Same curves both ways.
  surfaceGather: { from: 'far', split: 0.16, leg: leg(0.3, 0.3, 0.6, 0.8), rest: leg(0.16, 1, 0.3, 1),
                   alpha: [[0, 0.55], [0.45, 1], [1, 1]] },
  // …fading to nothing by 82% of the trip, not at the end: every mote converges on one icon point,
  // so a tail still at ~0.2 piled into a solid accent blob that blinked out (user report).
  surfaceScatter: { from: 'home', split: 0.18, leg: leg(0.3, 0.3, 0.6, 0.8), rest: leg(0.16, 1, 0.3, 1),
                    alpha: [[0, 1], [0.5, 0.85], [0.82, 0]] },
  // A mark's departure: the desktop's Sweep::Fall — near-still for the first third,
  // then dropping away and accelerating, the bend past halfway where a fall's is.
  fall: { from: 'home', split: 0.6, leg: leg(0.45, 0, 0.8, 0.4), rest: leg(0.45, 0, 0.8, 0.4),
          alpha: [[0, 1], [0.38, 0.85], [1, 0]] },
});

// Opacity at `p` along the stops — linear between them, held at the ends.
export const alphaAt = (stops, p) => {
  if (p <= stops[0][0]) return stops[0][1];
  for (let i = 1; i < stops.length; i++) {
    const [t1, a1] = stops[i];
    if (p <= t1) {
      const [t0, a0] = stops[i - 1];
      return t1 > t0 ? a0 + (a1 - a0) * ((p - t0) / (t1 - t0)) : a1;
    }
  }
  return stops[stops.length - 1][1];
};

// A grain WOBBLES off its rail (strongest mid-flight, zero at both ends, so it still lands where
// the flight says) and a GLINT twinkles. Both off the grain's hash. Desktop: DisintegrateOverlay.
export const TURBULENCE_SHARE = TUNE.TURBULENCE_SHARE;     // of the throw…
export const TURBULENCE_MAX_PX = TUNE.TURBULENCE_MAX_PX;   // …capped, so a window's trip does not swing wide
export const TURBULENCE_WAVES = TUNE.TURBULENCE_WAVES;     // waves per flight, by the grain's hash
export const TWINKLE_DEPTH = TUNE.TWINKLE_DEPTH;           // a glint's brightness swing, as a share
export const TWINKLE_HZ = TUNE.TWINKLE_HZ;                 // …at this many flickers a second, by hash
// The sideways push at progress `p` for a grain with turbulence share `t` (0 = a rail).
export const turbulenceAt = (m, p) => {
  const t = m.t || 0;
  if (!t) return 0;
  const len = Math.hypot(m.dx, m.dy);
  if (len < 0.5) return 0;
  const waves = TURBULENCE_WAVES[0] + (TURBULENCE_WAVES[1] - TURBULENCE_WAVES[0]) * (m.w || 0);
  const env = Math.sin(Math.PI * p);
  return t * Math.min(len * TURBULENCE_SHARE, TURBULENCE_MAX_PX) * env
    * Math.sin(p * waves * 2 * Math.PI + (m.w || 0) * 2 * Math.PI);
};
// A glint's brightness at `tMs` (1 for a plain grain).
export const twinkleAt = (m, tMs) => {
  if (!m.g) return 1;
  const hz = TWINKLE_HZ[0] + (TWINKLE_HZ[1] - TWINKLE_HZ[0]) * (m.w || 0);
  return 1 - TWINKLE_DEPTH * 0.5 * (1 + Math.sin(tMs * hz * 2 * Math.PI / 1000 + (m.w || 0) * 2 * Math.PI));
};

// A style is a touch laid over ANY flight, zero at both ends: offset (px), size multiplier, glow,
// and `mix` between the main colour (0) and its shade (1). Desktop twin: dustKit.hpp styleFrame.
export const STYLE_DUST = 0;
export const STYLE_WATER = 1;
export const STYLE_FIRE = 2;
// The style names prefs.js / the desktop settings speak, to their codes.
export const PARTICLE_STYLES = Object.freeze({ dust: STYLE_DUST, water: STYLE_WATER, fire: STYLE_FIRE });
// A styled cloud's palette: this many even mixes from the main colour to its shade.
export const PALETTE_STOPS = TUNE.PALETTE_STOPS;
export const WATER = TUNE.WATER;
export const FIRE = TUNE.FIRE;
// The style's touch on one grain: `p` its progress, `away` its distance from home (0…1),
// `w` its hash, `len` its throw, `tMs` the clock. Writes sx, sy, scale, glow, mix.
export const styleFrame = (style, p, away, w, len, tMs, out = {}) => {
  out.sx = 0; out.sy = 0; out.scale = 1; out.glow = 1; out.mix = 0;
  if (style !== STYLE_WATER && style !== STYLE_FIRE) return out;
  const env = Math.sin(Math.PI * p);
  const phase = w * 2 * Math.PI;
  const sec = tMs / 1000;
  const wave = (range) => range[0] + (range[1] - range[0]) * w;
  if (style === STYLE_WATER) {
    out.sy = Math.min(len * WATER.sagShare, WATER.sagMaxPx) * (0.6 + 0.4 * w) * env;
    out.sx = Math.min(len * WATER.swayShare, WATER.swayMaxPx) * env
      * Math.sin(p * wave(WATER.swayWaves) * 2 * Math.PI + phase);
    out.scale = 1 + WATER.swell * env;
    out.glow = 1 - WATER.shimmerDepth * 0.5 * (1 + Math.sin(sec * wave(WATER.shimmerHz) * 2 * Math.PI + phase));
    out.mix = 0.5 + 0.5 * Math.sin(sec * wave(WATER.glistenHz) * 2 * Math.PI + phase);
  } else {
    out.sy = -Math.min(len * FIRE.liftShare, FIRE.liftMaxPx) * (0.5 + 0.5 * w) * env;
    out.sx = Math.min(len * FIRE.waverShare, FIRE.waverMaxPx) * env
      * Math.sin(p * wave(FIRE.waverWaves) * 2 * Math.PI + phase);
    const dim = 0.5 * (1 + Math.sin(sec * wave(FIRE.flickerHz) * 2 * Math.PI + phase));   // 0 bright … 1 dim
    out.glow = 1 - FIRE.flickerDepth * dim;
    out.scale = 1 + FIRE.flare * env * (1 - dim);
    out.mix = Math.max(0, Math.min(1, FIRE.coolHash * w + (1 - FIRE.coolHash) * away));
  }
  return out;
};
// A DUST grain's mix, fixed for its flight: plain grains spread from the main colour to
// halfway by their hash, glints wear the shade — sand with sparkle in it.
export const DUST_MIX_SPREAD = TUNE.DUST_MIX_SPREAD;
export const dustMix = (w, glint) => (glint ? 1 : (w || 0) * DUST_MIX_SPREAD);
// Which stop of the ACCENT RAMP a grain at `mix` is painted from.
export const paletteIndex = (mix, stops = PALETTE_STOPS) =>
  Math.max(0, Math.min(stops - 1, Math.round(mix * (stops - 1))));
// Two grains in three ride the ramp above; the rest wear one of these, off the grain's own hash.
// They sit AFTER the ramp so one index still names one colour. Desktop twin: dustKit.hpp tintOf.
export const TINT_SHARE = TUNE.TINT_SHARE;   // of grains wear a tint; the rest ride the ramp
// Two of them follow the theme (css/theme.css), since a white speck cannot be seen on a
// pale surface, nor a deep accent one on a dark surface.
export const TINT_CSS = TUNE.TINT_CSS;
export const TINT_STOPS = TINT_CSS.length;
// Every colour paletteCss hands out: the ramp, then the tints.
export const PAINT_STOPS = PALETTE_STOPS + TINT_STOPS;
// Which tint a grain wears, or -1 for the ramp — its own slice of the hash, decorrelated
// from the shape, wobble and twinkle the same `w` picks.
export const tintOf = (w) => {
  const pick = fract((w || 0) * 13.73 + 0.41);
  if (pick >= TINT_SHARE) return -1;
  return Math.min(TINT_STOPS - 1, Math.floor((pick / TINT_SHARE) * TINT_STOPS));
};
// The stop a grain is painted from, once its tint is known — fixed for its whole flight,
// so every painter caches the tint per cloud (drawCloud's scratch.tints) instead of re-hashing.
export const stopOfTint = (mix, tint) => (tint < 0 ? paletteIndex(mix) : PALETTE_STOPS + tint);
// The shared scatter hash (motion.js tileNoise, the desktop's cellNoise): 0..1 from two ints.
export const hashNoise = (a, b) => { const h = Math.sin(a * 127.1 + b * 311.7) * 43758.5453; return h - Math.floor(h); };
export const fract = (v) => v - Math.floor(v);
