// ── A cloud of dust on ONE canvas ────────────────────────────────────────────
// Every element-sized cloud in the app (a row coming apart, a window forming out of its
// icon, a mark's tick crumbling) used to be a <div> per grain flying CSS keyframes. That
// bought compositor-driven motion and paid for it with a node per mote: a window-sized
// cloud was hundreds of layers built in the frame the open landed on, and past a few
// hundred it read as lag — which is why every surface carried a mote ceiling.
//
// Here the whole cloud is one <canvas> and a requestAnimationFrame loop: the keyframes
// are tabulated below (FLIGHTS) and every grain is evaluated per frame, then drawn in a
// handful of batched fills — the theme wake's trick (motion.js spawnSwapDust), where
// it was measured to keep 4500 grains at frame rate. The maths is the desktop overlay's
// (disintegrateOverlay.hpp legAt): the two surfaces now evaluate the same flight.
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
// A keyframe flight is a fixed rail: every grain slides its own curve and nothing in
// the cloud ever wavers. Evaluated per frame, a grain can WOBBLE — pushed off its line
// sideways by a slow wave of its own, strongest mid-flight and gone at both ends, so
// the cloud churns as it goes and still lands exactly where it would — and a GLINT can
// twinkle, its brightness breathing on a clock of its own. Both keyed off the grain's
// own hash (`w`), so the cloud is lively but reproducible. Desktop twin:
// disintegrateOverlay.hpp turbulenceAt / twinkleAt.
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

// One grain at progress `p` (0..1) of its OWN animation — before its delay it holds its
// 0% pose, after its end its 100% pose (animation-fill-mode: both). `tMs` is the cloud's
// clock, for the twinkle.
// `m`: { x, y (home centre), dx, dy (the throw), mx, my (the bend), r (radius at home),
//        s (size at the far end, as a share), a (the grain's own opacity),
//        w (the grain's own hash, 0..1), t (turbulence share), g (a glint?) }.
export const moteFrame = (m, flight, p, out = {}, tMs = 0) => {
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
  out.x = x;
  out.y = y;
  out.r = m.r * size;
  out.alpha = alphaAt(f.alpha, p) * m.a * twinkleAt(m, tMs);
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

// Paint one frame of `motes` at `tMs` since launch onto `ctx` (already translated so
// viewport coords land on the canvas). `scratch` is reused between frames.
export const drawCloud = (ctx, motes, flight, tMs, colours, scratch = { out: {}, buckets: new Map() }) => {
  const { out, buckets } = scratch;
  for (const b of buckets.values()) b.length = 0;
  for (const m of motes) {
    const p = m.dur > 0 ? Math.max(0, Math.min(1, (tMs - m.delay) / m.dur)) : 1;
    moteFrame(m, flight, p, out, tMs);
    if (out.alpha < 0.01 || out.r < 0.2) continue;
    const level = Math.round(out.alpha * (ALPHA_LEVELS - 1));
    if (level <= 0) continue;
    const key = m.c * ALPHA_LEVELS + level;
    let b = buckets.get(key);
    if (!b) buckets.set(key, (b = []));
    b.push(out.x, out.y, out.r);
  }
  for (const [key, b] of buckets) {
    if (!b.length) continue;
    ctx.globalAlpha = (key % ALPHA_LEVELS) / (ALPHA_LEVELS - 1);
    ctx.fillStyle = colours[Math.floor(key / ALPHA_LEVELS)];
    ctx.beginPath();
    for (let i = 0; i < b.length; i += 3) {
      ctx.moveTo(b[i] + b[i + 2], b[i + 1]);
      ctx.arc(b[i], b[i + 1], b[i + 2], 0, Math.PI * 2);
    }
    ctx.fill();
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
                                           now = () => (globalThis.performance?.now?.() ?? Date.now()) } = {}) {
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
  const scratch = { out: {}, buckets: new Map() };
  const started = now();
  let live = true;
  let handle = 0;
  const cancel = globalThis.cancelAnimationFrame || noop;
  const step = () => {
    if (!live) return;
    const t = now() - started;
    ctx.clearRect(left, top, w, h);
    drawCloud(ctx, motes, flight, t, colours, scratch);
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
