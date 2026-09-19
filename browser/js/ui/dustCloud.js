// ── A cloud of dust on ONE canvas ────────────────────────────────────────────
// Every element-sized cloud is one <canvas> and a rAF loop, not a <div> per grain: each grain is
// posed by dustFlight.js, shaped by dustGrain.js and drawn here in batched fills. A cloud wears one
// of three styles (dust, water, fire), always painted from the theme palette, never the surface.
// Byte-pinned to browser-extension/src/lib.
import { FLIGHTS, STYLE_DUST, alphaAt, dustMix, stopOfTint, styleFrame, tintOf,
         turbulenceAt, twinkleAt, TUNE } from './dustFlight.js';
import { addGrainPath, grainShape, headingOf } from './dustGrain.js';

// The flight table and the grain shapes stay part of this module's surface: every caller and the
// pinned parity tests import them from here.
export {
  bezierY, EASE_STEPS, easeLut, FLIGHTS, alphaAt, TURBULENCE_SHARE, TURBULENCE_MAX_PX,
  TURBULENCE_WAVES, TWINKLE_DEPTH, TWINKLE_HZ, turbulenceAt, twinkleAt, STYLE_DUST, STYLE_WATER,
  STYLE_FIRE, PARTICLE_STYLES, PALETTE_STOPS, WATER, FIRE, styleFrame, DUST_MIX_SPREAD, dustMix,
  paletteIndex, TINT_SHARE, TINT_CSS, TINT_STOPS, PAINT_STOPS, tintOf, stopOfTint, hashNoise,
 
} from './dustFlight.js';
export {
  SHAPE_DISC, SHAPE_OVAL, SHAPE_WAVE, SHAPE_TRIANGLE, SHAPE_STREAK, STYLED_CELL_SCALE,
  WATER_WAVE_SHARE, FIRE_STREAK_SHARE, SHAPES, grainShape, headingOf, shapePolygon,
  addGrainPath, EDGE_POINTS, EDGE, edgeJitter, edgeDipOf, edgeReachOf, edgeBaseOf, paletteCss,
} from './dustGrain.js';

// One grain at progress `p` of its OWN animation; before its delay it holds its 0% pose, after
// its end its 100% (animation-fill-mode: both). Writes x, y, r, alpha and mix.
export const moteFrame = (m, flight, p, out = {}, tMs = 0, style = STYLE_DUST) => {
  const f = FLIGHTS[flight] || FLIGHTS.scatter;
  const fromFar = f.from === 'far';
  const mid = 1 - (1 - m.s) * 0.5;
  let x, y, size;
  if (fromFar) {
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
    // The sag/lift is a share of what the grain has LEFT to reach its end, not of its whole
    // throw: converging on an icon it then has nowhere to sag, and lands where dust lands.
    const ex = fromFar ? m.x : m.x + m.dx;
    const ey = fromFar ? m.y : m.y + m.dy;
    styleFrame(style, p, fromFar ? 1 - p : p, m.w || 0, Math.hypot(ex - x, ey - y), tMs, out);
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
export const ALPHA_LEVELS = TUNE.ALPHA_LEVELS;
// …but a path only so long: cost per grain climbs with the path's length (7000 wave lines
// in one path measured 12x what they did in runs of 32), so batches fill in chunks.
export const FILL_CHUNK = TUNE.FILL_CHUNK;
// Fill a run of grains laid out [x, y, r, shape, heading] per grain, in chunks.
export const fillGrains = (ctx, b, n, poly) => {
  for (let i = 0; i < n; i += FILL_CHUNK) {
    const end = Math.min(n, i + FILL_CHUNK);
    ctx.beginPath();
    for (let j = i; j < end; j++) addGrainPath(ctx, b[j * 5 + 3], b[j * 5], b[j * 5 + 1], b[j * 5 + 2], b[j * 5 + 4], poly);
    ctx.fill();
  }
};

// Paint one frame of `motes` at `tMs` since launch onto `ctx`, already translated so viewport
// coords land on the canvas. `scratch` is reused between frames.
export const drawCloud = (ctx, motes, flight, tMs, colours, scratch = { out: {}, buckets: new Map() },
                          style = STYLE_DUST) => {
  const { out, buckets } = scratch;
  const fromFar = (FLIGHTS[flight] || FLIGHTS.scatter).from === 'far';
  // Each grain's shape, heading and tint, once per cloud (none change over the flight).
  if (!scratch.shapes || scratch.shapes.length !== motes.length || scratch.style !== style) {
    scratch.shapes = Int8Array.from(motes, (m) => grainShape(style, m.w));
    scratch.heads = Float32Array.from(motes, (m) => headingOf(m.dx, m.dy, fromFar));
    scratch.tints = Int8Array.from(motes, (m) => tintOf(m.w));
    scratch.style = style;
  }
  const { shapes, heads, tints } = scratch;
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
    const key = Math.min(colours.length - 1, stopOfTint(out.mix, tints[i])) * ALPHA_LEVELS + level;
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

// Resolve a CSS colour to something a canvas fill takes: the computed colour of a probe element
// wearing it. Plain rgb/hex pass through untouched.
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

// Fly `motes` on a canvas in `host` until `span` + a beat; returns a stop.
// A document with no 2D canvas (tests) gets no painting, so bookkeeping never needs the paint.
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
  const cancel = globalThis.cancelAnimationFrame ?? noop;
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

