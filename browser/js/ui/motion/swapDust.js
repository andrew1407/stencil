import { dustEnabled } from '../motionPrefs.js';
import { resolveColour, paletteCss, styleFrame, tintOf, stopOfTint, dustMix, grainShape, headingOf, fillGrains, edgeDipOf } from '../dustCloud.js';
import { DUST_ALPHA_LEVELS } from './canvasDustStage.js';
import { THEME_SWAP_MS, bezierY, swapEase, swapRadius } from './themeSwap.js';
import { tileNoise } from './tiles.js';
import { TUNE, styleCode } from './tune.js';
// ── Dust in the wipe's wake ─────────────────────────────────────────────────
// The torn front kicks up dust as it passes: specks igniting along the edge and
// settling just behind it, in the OLD palette's colours — the paint the front grinds
// away. Always just INSIDE the clip: during a view transition the page renders through
// ::view-transition-new(root), so a mote ahead of the front simply would not be seen —
// they spawn behind even the deepest tooth (the 1 − amp band).
// (Desktop twin: themeSwapOverlay.hpp dustMoteAt.)
export const SWAP_DUST_MOTES = TUNE.SWAP_DUST_MOTES;
export const SWAP_DUST_LIFE_MS = TUNE.SWAP_DUST_LIFE_MS;
// A mote never ignites at the very ends of the wipe: at t=0 the ring is a point (nothing
// to ride), and the last ones still get their whole life before the layer is reaped.
export const SWAP_DUST_MIN_T = TUNE.SWAP_DUST_MIN_T;
export const SWAP_DUST_MAX_T = TUNE.SWAP_DUST_MAX_T;


// The specs for one wipe's dust, all deterministic (tileNoise, like every other cloud
// here). `x, y` is the origin in viewport px; motes whose home is off screen are dropped,
// so the field naturally thins as the ring outgrows the viewport. Pure — unit-tested.
export function swapDustSpecs(x, y, w, h, count = SWAP_DUST_MOTES, style = styleCode()) {
  const R = swapRadius(x, y, w, h);
  const specs = [];
  if (!(R > 0)) return specs;
  const dip = edgeDipOf(style);   // how deep the style's front bites inward
  for (let i = 0; i < count; i++) {
    const n = tileNoise(i, 3);
    const m = tileNoise(i + 57, 11);
    const q = tileNoise(i + 13, 29);
    const angle = n * 2 * Math.PI;
    const u = SWAP_DUST_MIN_T + m * (SWAP_DUST_MAX_T - SWAP_DUST_MIN_T);
    // Hug the torn edge: just behind even its deepest tooth (1 − amp of the nominal
    // radius), so the band of grains and the ragged clip read as one crumbling front.
    const r = swapEase(u) * R * (1 - dip) - q * 6;
    if (r <= 0) continue;
    const cx = x + Math.cos(angle) * r;
    const cy = y + Math.sin(angle) * r;
    if (cx < -16 || cy < -16 || cx > w + 16 || cy > h + 16) continue;
    const size = +(2.5 + n * 3.5).toFixed(2);
    // Chase the front outward, slower than it (the ring accelerates away), plus a
    // sideways breath so the wake churns instead of radiating.
    const d = 8 + q * 14;
    const dx = Math.round(Math.cos(angle) * d + (m - 0.5) * 14);
    const dy = Math.round(Math.sin(angle) * d + (0.5 - q) * 14);
    specs.push({
      cx: +cx.toFixed(2),
      cy: +cy.toFixed(2),
      size,
      dx,
      dy,
      delay: Math.round(u * THEME_SWAP_MS),
      alpha: +(0.75 + q * 0.25).toFixed(2),
      // Every fourth grain is the departing accent; the rest are the old surface's own
      // grain (bg lifted towards ink, the speckPainter recipe) — so a theme flip dusts
      // in the old page's colour and an accent cycle still shows over an unchanged bg.
      accent: i % 4 === 0,
      // …and its own hash and throw length, for a water / fire wake's styleFrame.
      w: tileNoise(i + 71, 13),
      len: Math.hypot(dx, dy),
    });
  }
  return specs;
}

// ── One grain's flight ──────────────────────────────────────────────────────
// The wake used to be a div per grain running the `swapDustMote` keyframes. At this
// density that was thousands of composited layers and the swap dropped half its frames,
// so the stage below paints the grains itself and this is where those keyframes now
// live: the same cubic-bezier, the same three opacity stops, the same throw and shrink.
// Sampled into a table — the bisection is far too dear to run three times per grain per
// frame. (Desktop twin: themeSwapOverlay.hpp dustMoteAt, which evaluates them by hand.)
const SWAP_DUST_STEPS = TUNE.SWAP_DUST_STEPS;
const swapDustCurve = Float32Array.from({ length: SWAP_DUST_STEPS + 1 },
  (_, i) => bezierY(i / SWAP_DUST_STEPS, 0.22, 0.55, 0.3, 1));
// Both ends exactly: the solver bisects to within a hair of 0 and 1, and that hair is
// enough to start a grain a fraction off its home and leave it a fraction lit.
swapDustCurve[0] = 0;
swapDustCurve[SWAP_DUST_STEPS] = 1;
export const swapDustEase = (t) =>
  swapDustCurve[Math.min(SWAP_DUST_STEPS, Math.max(0, Math.round(t * SWAP_DUST_STEPS)))];

// Opacity flares over the first 18% of a grain's life and falls away across the rest.
export const SWAP_DUST_FLARE = TUNE.SWAP_DUST_FLARE;

// Where grain `s` is at life-fraction `p` (0 = ignition, 1 = burnt out), how big and how
// bright. Writes into `out` rather than returning a fresh object: this runs once per
// grain per frame. Pure — unit-tested.
export function swapDustFrame(s, p, out = {}) {
  const e = swapDustEase(p);
  const o = p < SWAP_DUST_FLARE
    ? swapDustEase(p / SWAP_DUST_FLARE)
    : 1 - swapDustEase((p - SWAP_DUST_FLARE) / (1 - SWAP_DUST_FLARE));
  out.x = s.cx + s.dx * e;
  out.y = s.cy + s.dy * e;
  out.r = (s.size / 2) * (1 - 0.7 * e);
  out.alpha = s.alpha * o;
  return out;
}

// What the wake is painted in — read BEFORE the palette flips, then baked as literals:
// by the time a mote is on screen the variables already mean the NEW theme.
export const swapDustPaint = () => {
  try {
    const s = getComputedStyle(document.documentElement);
    const v = (name) => (s.getPropertyValue(name) || '').trim();
    if (!v('--accent')) return null;
    // The wake is painted from the departing palette — the accent ramp plus its tints,
    // what every cloud wears (dustCloud.js paletteCss) — resolved while the variables
    // still mean the OLD theme.
    return { palette: paletteCss().map((css) => resolveColour(document, css)) };
  } catch { return null; }
};

// Whatever the canvas makes of a colour: it normalizes what it accepts and silently
// keeps what it had for anything it cannot parse, so a round trip that comes back
// unchanged from `fallback` is a colour that did not survive.
const canvasColour = (ctx, c, fallback) => {
  ctx.fillStyle = fallback;
  ctx.fillStyle = c;
  return ctx.fillStyle;
};

// Build the layer. `px` is the origin/viewport the wipe was actually written with.
// ONE canvas, not a div per grain: grains are batched by colour and alpha step into a
// handful of fills a frame, the same trick the canvas dust plays (drawDust below).
// Decoration only: any stub environment bails inside the catch and the swap plays clean.
export function spawnSwapDust(px, paint) {
  try {
    if (!px || !paint || typeof document === 'undefined' || !document.body?.appendChild) return;
    if (typeof requestAnimationFrame !== 'function' || !dustEnabled()) return;
    const root = document.documentElement;
    // A second swap mid-wake starts a new wipe — the newest one owns the dust.
    root._swapDustStop?.();
    const specs = swapDustSpecs(px.x, px.y, px.w, px.h);
    if (!specs.length) return;
    const stage = document.createElement('canvas');
    const ctx = stage.getContext?.('2d');
    if (!ctx) return;
    stage.className = 'swap-dust';
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    stage.width = Math.round(px.w * dpr);
    stage.height = Math.round(px.h * dpr);
    stage.style.width = `${px.w}px`;
    stage.style.height = `${px.h}px`;
    ctx.scale(dpr, dpr);
    // One run of one fillStyle per palette stop, so a frame is a handful of fills rather
    // than a thousand switches — resolved already, before the palette moved under us.
    const style = styleCode();
    const runs = paint.palette.map((c) => ({ colour: canvasColour(ctx, c, '#888') }));
    document.body.appendChild(stage);
    // [x, y, r, shape, heading] per grain, bucketed by alpha step and reused every frame.
    const lvl = Array.from({ length: DUST_ALPHA_LEVELS }, () => new Float32Array(specs.length * 5));
    const lvlN = new Int32Array(DUST_ALPHA_LEVELS);
    const at = {};
    const sf = {};
    const poly = [];
    // Every grain's frame, computed once and read by every run's sweep; its shape and
    // heading (dustCloud.js grainShape / headingOf) never change.
    const stopOf = new Int8Array(specs.length);
    const fx = new Float32Array(specs.length * 4);
    const shapes = Int8Array.from(specs, (s) => grainShape(style, s.w));
    const heads = Float32Array.from(specs, (s) => headingOf(s.dx, s.dy, false));
    const tints = Int8Array.from(specs, (s) => tintOf(s.w));
    const total = THEME_SWAP_MS + SWAP_DUST_LIFE_MS;
    const started = performance.now();
    let raf = 0;
    const stop = () => {
      if (typeof cancelAnimationFrame === 'function') cancelAnimationFrame(raf);
      clearTimeout(root._swapDustTimer);
      stage.remove();
      if (root._swapDustStop === stop) { root._swapDustStop = null; root._swapDustTimer = null; }
    };
    const frame = (now) => {
      const ms = now - started;
      if (ms >= total) { stop(); return; }
      ctx.clearRect(0, 0, px.w, px.h);
      for (let i = 0; i < specs.length; i++) {
        const s = specs[i];
        const p = (ms - s.delay) / SWAP_DUST_LIFE_MS;
        if (p <= 0 || p >= 1) { stopOf[i] = -1; continue; }
        swapDustFrame(s, p, at);
        styleFrame(style, p, p, s.w, s.len, ms, sf);
        stopOf[i] = stopOfTint(style ? sf.mix : dustMix(s.w, s.accent), tints[i]);
        fx[i * 4] = at.x + sf.sx; fx[i * 4 + 1] = at.y + sf.sy;
        fx[i * 4 + 2] = at.r * sf.scale; fx[i * 4 + 3] = at.alpha * sf.glow;
      }
      for (let c = 0; c < runs.length; c++) {
        const run = runs[c];
        lvlN.fill(0);
        for (let i = 0; i < specs.length; i++) {
          if (stopOf[i] !== c) continue;
          const l = Math.round(fx[i * 4 + 3] * DUST_ALPHA_LEVELS) - 1;
          if (l < 0) continue;
          const buf = lvl[l], j = lvlN[l]++ * 5;
          buf[j] = fx[i * 4]; buf[j + 1] = fx[i * 4 + 1]; buf[j + 2] = fx[i * 4 + 2];
          buf[j + 3] = shapes[i]; buf[j + 4] = heads[i];
        }
        ctx.fillStyle = run.colour;
        for (let l = 0; l < DUST_ALPHA_LEVELS; l++) {
          const n = lvlN[l];
          if (!n) continue;
          ctx.globalAlpha = (l + 1) / DUST_ALPHA_LEVELS;
          fillGrains(ctx, lvl[l], n, poly);
        }
      }
      raf = requestAnimationFrame(frame);
    };
    raf = requestAnimationFrame(frame);
    root._swapDustStop = stop;
    // Belt and braces: a throttled or paused rAF (a backgrounded tab) would otherwise
    // leave the stage sitting over the page for good.
    root._swapDustTimer = setTimeout(stop, total + 200);
  } catch { /* decoration only — the swap carries on regardless */ }
}
