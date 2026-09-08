// ── A cloud of dust on ONE canvas ────────────────────────────────────────────
// Every element-sized cloud in the app is one <canvas> and a rAF loop rather than a <div>
// per grain: the keyframes are tabulated below (FLIGHTS), evaluated per grain per frame
// and drawn in a handful of batched fills. Desktop twin: disintegrateOverlay.hpp legAt.
//
// A cloud wears one of three STYLES (motionPrefs.js particleStyle) — dust, water (grains
// sag and sway like drops) or fire (they lift and waver like embers) — always painted in
// the theme's --accent / --accent-2, never in the surface's own pixels.
//
// Pure except for startCloud, which needs a document. Mirrored byte-for-byte in
// extension/src/lib/dustCloud.js (extension/tests/portParity.test.js).

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

// A curve sampled once into 256 steps, both ends pinned exactly — the solver only
// bisects to within a hair of 0 and 1, and that hair leaves a landed grain a fraction
// off home. Thousands of grains read it per frame; solving per read was the frame.
export const EASE_STEPS = 256;
export const easeLut = (x1, y1, x2, y2) => {
  const lut = new Float32Array(EASE_STEPS + 1);
  for (let i = 0; i <= EASE_STEPS; i++) lut[i] = bezierY(i / EASE_STEPS, x1, y1, x2, y2);
  lut[0] = 0;
  lut[EASE_STEPS] = 1;
  return (t) => lut[Math.max(0, Math.min(EASE_STEPS, Math.round(t * EASE_STEPS)))];
};

// ── The flights: css/animations.css's tile keyframes, as numbers ─────────────
// A grain flies TWO legs (no straight lines, motion.js tileWaypoint): from its start to
// the bend on the first leg's own curve, then from the bend to its end on the flight's
// — the mid keyframe that carried its own animation-timing-function. `split` is where
// that keyframe sat; `from` says which end the grain starts at ('home' scatters out to
// the far point, 'far' gathers home). `alpha` are the opacity stops, read on the clock.
// The grain's size passes through the halfway state at the bend, so nothing snaps there.
const leg = easeLut;
export const FLIGHTS = {
  // A removed row: near-linear into the bend at 38%, then the flight's own ease home.
  scatter: { from: 'home', split: 0.38, leg: leg(0.3, 0.4, 0.7, 0.8), rest: leg(0.22, 0.55, 0.3, 1),
             alpha: [[0, 1], [0.38, 0.85], [1, 0]] },
  // …played backwards for an arriving row: from where the scatter would have flung it,
  // fading up as it comes.
  gather: { from: 'far', split: 0.58, leg: leg(0.3, 0.2, 0.7, 0.6), rest: leg(0.7, 0, 0.78, 0.45),
            alpha: [[0, 0], [0.22, 0.75], [0.58, 0.9], [1, 1]] },
  // A surface's motes are the window — visible from the first frame — and their
  // ease-out covers most of the trip in the first fifth, so the bend sits early enough
  // to be seen. Same curves both ways: the motes break away at once and drift to a stop.
  surfaceGather: { from: 'far', split: 0.16, leg: leg(0.3, 0.3, 0.6, 0.8), rest: leg(0.16, 1, 0.3, 1),
                   alpha: [[0, 0.55], [0.45, 1], [1, 1]] },
  surfaceScatter: { from: 'home', split: 0.18, leg: leg(0.3, 0.3, 0.6, 0.8), rest: leg(0.16, 1, 0.3, 1),
                    alpha: [[0, 1], [0.55, 0.9], [1, 0]] },
  // A mark's departure: the desktop's Sweep::Fall — near-still for the first third,
  // then dropping away and accelerating, the bend past halfway where a fall's is.
  fall: { from: 'home', split: 0.6, leg: leg(0.45, 0, 0.8, 0.4), rest: leg(0.45, 0, 0.8, 0.4),
          alpha: [[0, 1], [0.38, 0.85], [1, 0]] },
};

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

// ── Turbulence and twinkle ──────────────────────────────────────────────────
// A grain WOBBLES sideways off its rail (strongest mid-flight, gone at both ends, so it
// still lands where the flight says) and a GLINT twinkles. Both keyed off the grain's own
// hash, so a cloud is lively but reproducible. Desktop twin: disintegrateOverlay.hpp.
export const TURBULENCE_SHARE = 0.06;   // of the throw…
export const TURBULENCE_MAX_PX = 6;     // …capped, so a window's trip does not swing wide
export const TURBULENCE_WAVES = [2.5, 4.5];   // waves per flight, by the grain's hash
export const TWINKLE_DEPTH = 0.35;      // a glint's brightness swing, as a share
export const TWINKLE_HZ = [4, 7];       // …at this many flickers a second, by hash
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

// ── Particle styles: dust, water, fire ───────────────────────────────────────
// A style is a touch laid over ANY flight, gone at both ends (the turbulence rule): an
// offset (px), a size multiplier, a brightness (`glow`) and `mix`, where between the main
// colour (0) and its shade (1) the grain is painted. Dust is the identity. Desktop twin:
// dustKit.hpp styleFrame — keep the numbers in step.
export const STYLE_DUST = 0;
export const STYLE_WATER = 1;
export const STYLE_FIRE = 2;
// The style names motionPrefs.js / the desktop settings speak, to their codes.
export const PARTICLE_STYLES = { dust: STYLE_DUST, water: STYLE_WATER, fire: STYLE_FIRE };
// A styled cloud's palette: this many even mixes from the main colour to its shade.
export const PALETTE_STOPS = 6;
export const WATER = {
  sagShare: 0.45, sagMaxPx: 30,      // a drop sags below its line by a share of the throw…
  swayShare: 0.12, swayMaxPx: 5,     // …and sways slowly across it
  swayWaves: [0.8, 1.4],             // sways per flight, by the grain's hash
  swell: 0.3,                        // grows this much mid-flight
  shimmerDepth: 0.25, shimmerHz: [1.2, 2.2],   // a slow, shallow breath of brightness
  glistenHz: [0.6, 1.1],             // …and a slow drift between the two colours
};
export const FIRE = {
  liftShare: 0.6, liftMaxPx: 44,     // an ember lifts above its line…
  waverShare: 0.08, waverMaxPx: 4,   // …and wavers quickly across it
  waverWaves: [3, 5],
  flare: 0.35,                       // grows this much mid-flight, when bright
  flickerDepth: 0.55, flickerHz: [9, 14],   // a fast, deep flicker
  coolHash: 0.25,                    // colour: this share by hash, the rest by distance from home
};
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
export const DUST_MIX_SPREAD = 0.5;
export const dustMix = (w, glint) => (glint ? 1 : (w || 0) * DUST_MIX_SPREAD);
// Which palette stop a grain at `mix` is painted from.
export const paletteIndex = (mix, stops = PALETTE_STOPS) =>
  Math.max(0, Math.min(stops - 1, Math.round(mix * (stops - 1))));
// The shared scatter hash (motion.js tileNoise, the desktop's cellNoise): 0..1 from two ints.
export const hashNoise = (a, b) => { const h = Math.sin(a * 127.1 + b * 311.7) * 43758.5453; return h - Math.floor(h); };
const fract = (v) => v - Math.floor(v);

// ── Grain shapes ─────────────────────────────────────────────────────────────
// Dust is a round speck; water ovals and short wave lines; fire triangles and streaking
// sparks. A grain keeps one shape for its flight (picked off its hash) and lies along its
// heading. Geometry in radii, so every shape covers about the area the disc did. Desktop
// twin: dustKit.hpp grainShape / shapePolygon.
export const SHAPE_DISC = 0;
export const SHAPE_OVAL = 1;
export const SHAPE_WAVE = 2;
export const SHAPE_TRIANGLE = 3;
export const SHAPE_STREAK = 4;
// A styled grain is bigger and dearer than a speck, so a screen-sized cloud grids at this
// many times the cell — about half the grains — under water and fire.
export const STYLED_CELL_SCALE = 1.4;
export const WATER_WAVE_SHARE = 0.3;    // of water grains are wave lines, the rest ovals
export const FIRE_STREAK_SHARE = 0.4;   // of fire grains are spark streaks, the rest triangles
export const SHAPES = {
  oval: { rx: 1.45, ry: 0.7 },                                   // along, across
  wave: { len: 3.6, amp: 0.42, waves: 1.5, half: 0.28, samples: 9 },
  triangle: { tip: 1.7, base: 0.85, half: 1.0 },                 // tip ahead, base behind
  streak: { head: 1.0, headHalf: 0.42, tail: 2.6, tailHalf: 0.1 },   // a spark's tail trails
};
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

// ── The theme-swap front ─────────────────────────────────────────────────────
// The palette wipe grows a ring whose EDGE wears the style: dust a perfect circle, water
// one waved by slow swells, fire one cut into tongues of flame. `edgeJitter` is vertex k's
// reach off the nominal radius, as a share of it; `edgeDipOf` the deepest dip inward.
export const EDGE_POINTS = 240;
export const EDGE = {
  water: { waves: 9, amp: 0.028, ripple: 17, rippleAmp: 0.008 },
  fire: { tongues: 20, base: 0.05, vary: 0.05, dip: 0.012, jag: 0.006 },
};
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

// The palette itself, as CSS: PALETTE_STOPS even mixes of --accent and --accent-2, so a
// cloud is violet by default and follows the accent.
export const paletteCss = (stops = PALETTE_STOPS) =>
  Array.from({ length: stops }, (_, i) =>
    `color-mix(in srgb, var(--accent) ${Math.round(100 - (100 * i) / (stops - 1))}%, var(--accent-2))`);

// One grain at progress `p` (0..1) of its OWN animation — before its delay it holds its
// 0% pose, after its end its 100% pose (animation-fill-mode: both). `tMs` is the cloud's
// clock, for the twinkle and the style's breathing; `style` is one of the codes above.
// `m`: { x, y (home centre), dx, dy (the throw), mx, my (the bend), r (radius at home),
//        s (size at the far end, as a share), a (the grain's own opacity),
//        w (the grain's own hash, 0..1), t (turbulence share), g (a glint?) }.
// Writes x, y, r, alpha and mix — its place in the palette (dustMix / styleFrame). Shape
// and heading never change over a flight, so drawCloud works them out once instead.
export const moteFrame = (m, flight, p, out = {}, tMs = 0, style = STYLE_DUST) => {
  const f = FLIGHTS[flight] || FLIGHTS.scatter;
  const mid = 1 - (1 - m.s) * 0.5;
  let x, y, size;
  if (f.from === 'far') {
    if (p <= f.split) {
      const e = f.leg(f.split > 0 ? p / f.split : 1);
      x = m.x + m.dx + (m.mx - m.dx) * e;
      y = m.y + m.dy + (m.my - m.dy) * e;
      size = m.s + (mid - m.s) * e;
    } else {
      const e = f.rest((p - f.split) / (1 - f.split));
      x = m.x + m.mx * (1 - e);
      y = m.y + m.my * (1 - e);
      size = mid + (1 - mid) * e;
    }
  } else if (p <= f.split) {
    const e = f.leg(f.split > 0 ? p / f.split : 1);
    x = m.x + m.mx * e;
    y = m.y + m.my * e;
    size = 1 + (mid - 1) * e;
  } else {
    const e = f.rest((p - f.split) / (1 - f.split));
    x = m.x + m.mx + (m.dx - m.mx) * e;
    y = m.y + m.my + (m.dy - m.my) * e;
    size = mid + (m.s - mid) * e;
  }
  const wob = turbulenceAt(m, p);
  if (wob) {
    const len = Math.hypot(m.dx, m.dy);
    x += -m.dy / len * wob;
    y += m.dx / len * wob;
  }
  let glow = twinkleAt(m, tMs);
  out.mix = dustMix(m.w, m.g);
  if (style) {
    styleFrame(style, p, f.from === 'far' ? 1 - p : p, m.w || 0, Math.hypot(m.dx, m.dy), tMs, out);
    x += out.sx;
    y += out.sy;
    size *= out.scale;
    glow = out.glow;
  }
  out.x = x;
  out.y = y;
  out.r = m.r * size;
  out.alpha = alphaAt(f.alpha, p) * m.a * glow;
  return out;
};

// Everything the cloud can paint: the union of every grain's home, bend and far end,
// padded by its size. Pure.
export const cloudBounds = (motes, pad = 4) => {
  let l = Infinity, t = Infinity, r = -Infinity, b = -Infinity;
  for (const m of motes) {
    const e = m.r + pad;
    for (const [px, py] of [[m.x, m.y], [m.x + m.mx, m.y + m.my], [m.x + m.dx, m.y + m.dy]]) {
      if (px - e < l) l = px - e;
      if (py - e < t) t = py - e;
      if (px + e > r) r = px + e;
      if (py + e > b) b = py + e;
    }
  }
  return motes.length ? { left: l, top: t, right: r, bottom: b } : null;
};

// Grains are drawn in a few batched fills — one path per (colour, opacity step) —
// instead of one fill per grain: with ~1400 grains a frame, the fills were the frame.
export const ALPHA_LEVELS = 10;
// …but a path only so long: cost per grain climbs with the path's length (7000 wave lines
// in one path measured 12x what they did in runs of 32), so batches fill in chunks.
export const FILL_CHUNK = 32;
// Fill a run of grains laid out [x, y, r, shape, heading] per grain, in chunks.
export const fillGrains = (ctx, b, n, poly) => {
  for (let i = 0; i < n; i += FILL_CHUNK) {
    const end = Math.min(n, i + FILL_CHUNK);
    ctx.beginPath();
    for (let j = i; j < end; j++) addGrainPath(ctx, b[j * 5 + 3], b[j * 5], b[j * 5 + 1], b[j * 5 + 2], b[j * 5 + 4], poly);
    ctx.fill();
  }
};

// Paint one frame of `motes` at `tMs` since launch onto `ctx` (already translated so
// viewport coords land on the canvas). `scratch` is reused between frames; `colours` is
// the resolved palette, each grain picking its stop by its `mix`.
export const drawCloud = (ctx, motes, flight, tMs, colours, scratch = { out: {}, buckets: new Map() },
                          style = STYLE_DUST) => {
  const { out, buckets } = scratch;
  const fromFar = (FLIGHTS[flight] || FLIGHTS.scatter).from === 'far';
  // Each grain's shape and heading, once per cloud (they never change over the flight).
  if (!scratch.shapes || scratch.shapes.length !== motes.length || scratch.style !== style) {
    scratch.shapes = Int8Array.from(motes, (m) => grainShape(style, m.w));
    scratch.heads = Float32Array.from(motes, (m) => headingOf(m.dx, m.dy, fromFar));
    scratch.style = style;
  }
  const { shapes, heads } = scratch;
  for (const b of buckets.values()) b.length = 0;
  for (let i = 0; i < motes.length; i++) {
    const m = motes[i];
    // A gathering grain is NOTHING until it sets off: parked at its far end with hundreds
    // of others it filled the icon with a solid blob of the accent (user report).
    if (fromFar && tMs < m.delay) continue;
    const p = m.dur > 0 ? Math.max(0, Math.min(1, (tMs - m.delay) / m.dur)) : 1;
    moteFrame(m, flight, p, out, tMs, style);
    if (out.alpha < 0.01 || out.r < 0.2) continue;
    const level = Math.round(out.alpha * (ALPHA_LEVELS - 1));
    if (level <= 0) continue;
    const key = paletteIndex(out.mix, colours.length) * ALPHA_LEVELS + level;
    let b = buckets.get(key);
    if (!b) buckets.set(key, (b = []));
    b.push(out.x, out.y, out.r, shapes[i], heads[i]);
  }
  const poly = scratch.poly || (scratch.poly = []);
  for (const [key, b] of buckets) {
    if (!b.length) continue;
    ctx.globalAlpha = (key % ALPHA_LEVELS) / (ALPHA_LEVELS - 1);
    ctx.fillStyle = colours[Math.floor(key / ALPHA_LEVELS)];
    fillGrains(ctx, b, b.length / 5, poly);
  }
  ctx.globalAlpha = 1;
};

// Resolve a CSS colour (`var(--x)`, `color-mix(…)`, a name) to something a canvas fill
// takes: the computed colour of a probe element that wears it. Plain rgb/hex pass
// through untouched.
export const resolveColour = (doc, css, probe = null) => {
  if (!css || /^(#|rgb|hsl|color\()/.test(css)) return css || '#888';
  const get = typeof getComputedStyle === 'function' ? getComputedStyle : null;
  if (!get || !doc?.body) return css;
  try {
    const span = probe || doc.createElement('span');
    span.style.color = css;
    if (!probe) doc.body.appendChild(span);
    const got = get(span).color;
    if (!probe) span.remove();
    return got || css;
  } catch {
    return css;
  }
};

// Put a canvas in `host` (a positioned layer whose origin is `origin`, viewport coords)
// and fly `motes` on it from now until `span` + a beat. Returns a stop function, and
// hangs the same on host.__stop; a document with no 2D canvas (tests) gets no painting
// and a no-op stop, so a flight's bookkeeping never depends on the paint.
export function startCloud(host, motes, { flight = 'scatter', span, colours, origin, doc = globalThis.document,
                                           viewport = null, raf = globalThis.requestAnimationFrame,
                                           now = () => (globalThis.performance?.now?.() ?? Date.now()),
                                           style = STYLE_DUST } = {}) {
  const noop = () => {};
  host.__stop = noop;
  if (!doc?.createElement || typeof raf !== 'function' || !motes.length) return noop;
  const canvas = doc.createElement('canvas');
  const ctx = canvas.getContext?.('2d');
  if (!ctx) return noop;
  // Only what can be seen: a docked panel aims well past the window edge, and a canvas
  // the size of that trip is memory for pixels nobody looks at.
  const vw = viewport?.width ?? globalThis.innerWidth ?? 4096;
  const vh = viewport?.height ?? globalThis.innerHeight ?? 4096;
  const b = cloudBounds(motes);
  if (!b) return noop;
  const left = Math.floor(Math.max(b.left, -64));
  const top = Math.floor(Math.max(b.top, -64));
  const right = Math.ceil(Math.min(b.right, vw + 64));
  const bottom = Math.ceil(Math.min(b.bottom, vh + 64));
  const w = right - left, h = bottom - top;
  if (!(w > 0 && h > 0)) return noop;
  const dpr = Math.min(globalThis.devicePixelRatio || 1, 2);
  canvas.width = Math.ceil(w * dpr);
  canvas.height = Math.ceil(h * dpr);
  canvas.style.cssText = `position:absolute;left:${left - origin.x}px;top:${top - origin.y}px;`
    + `width:${w}px;height:${h}px;pointer-events:none;`;
  host.appendChild(canvas);
  ctx.setTransform(dpr, 0, 0, dpr, -left * dpr, -top * dpr);
  const scratch = { out: {}, buckets: new Map(), poly: [] };
  const started = now();
  let live = true;
  let handle = 0;
  const cancel = globalThis.cancelAnimationFrame || noop;
  const step = () => {
    if (!live) return;
    const t = now() - started;
    ctx.clearRect(left, top, w, h);
    drawCloud(ctx, motes, flight, t, colours, scratch, style);
    if (t <= span + 150) handle = raf(step);
    else live = false;
  };
  step();
  const stop = () => {
    if (!live) return;
    live = false;
    if (handle) cancel(handle);
  };
  host.__stop = stop;
  return stop;
}
