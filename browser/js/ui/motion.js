// ── Shared UI motion helpers ────────────────────────────────────
// Pure decoration: a missing IntersectionObserver/MutationObserver (node tests,
// old engines) simply means no animation — never a broken or hidden view. CSS
// owns the actual keyframes (css/animations.css); this file only toggles classes.
import { dustEnabled, motionReduced, particleStyle } from './motionPrefs.js';
import { startCloud, resolveColour, PARTICLE_STYLES, paletteCss, styleFrame, paletteIndex, dustMix,
         grainShape, headingOf, fillGrains, edgeJitter, edgeBaseOf, edgeDipOf, STYLED_CELL_SCALE } from './dustCloud.js';

// The two gates every helper below asks: `motionReduced()` is "nothing may move"
// (the OS preference, or the user's own 'none'), `dustEnabled()` is "and it may be
// made of particles" — false in 'slide', where each surface keeps its own plain
// CSS entrance instead. Re-exported so a caller needs one import, not two.
export { dustEnabled, motionReduced };

// Rows only dissolve by the amount the scroller is ALREADY clipping them; a row you can
// see in full is never touched — the grain is finer than a glyph's strokes, and
// decoration must never cost legibility.
export const REVEAL_ITEM_CLASS = 'reveal-item';
export const REVEAL_IN_CLASS = 'reveal-in';
export const REVEAL_ENTERING_CLASS = 'reveal-entering';
// Gates the mask itself: only a row straddling an edge is worth masking.
export const REVEAL_MASKED_CLASS = 'reveal-masked';
export const REVEAL_SMOOTH_CLASS = 'reveal-smooth';
export const REVEAL_NO_TRIGGER_CLASS = 'reveal-no-trigger';
export const REVEAL_ENTER_MS = 700;
// How far the wipe softens into the cut — a share of the ROW, matching animations.css.
export const REVEAL_FEATHER = '10%';

// How dissolved a row spanning [top, bottom) is in a scroller `viewH` tall: 0 while
// wholly on screen, rising with the clipped share, 1 once it is gone. Pure.
export const revealDissolve = (top, bottom, viewH) => {
  const h = bottom - top;
  if (viewH <= 0 || h <= 0) return 0;
  const visible = Math.max(0, Math.min(bottom, viewH) - Math.max(top, 0));
  return 1 - visible / h;
};

// Which of a row's edges the scroller is ACTUALLY cutting — the only ones the wipe may
// soften: feathering both ends unconditionally sands content sitting on a fully visible
// edge (the hover "…" trigger). Half-pixel slack: flush with an edge is not clipped. Pure.
export const revealFeather = (top, bottom, viewH, feather = REVEAL_FEATHER) => ({
  in: top < -0.5 ? feather : '0%',
  out: bottom > viewH + 0.5 ? feather : '0%',
});

// Text rows (the chat transcript) fade over a FIXED band instead — see the smooth
// option on observeReveal. A constant is also what the bottom-anchored trigger below
// clears, so the two agree by construction.
export const REVEAL_SMOOTH_FEATHER_PX = 12;
export const REVEAL_SMOOTH_FEATHER = `${REVEAL_SMOOTH_FEATHER_PX}px`;

// Distance UP from the row's own bottom where bottom-anchored chrome (the chat "…"
// trigger) must sit to stay inside the slice the SCROLLER is showing, clearing the cut
// edge's fade band — a CONSTANT band, not a share of the row. Pure.
export const revealVisibleBottom = (top, bottom, viewH) => {
  const h = bottom - top;
  if (h <= 0) return 0;
  const vTop = Math.min(h, Math.max(0, -top));
  const vBot = Math.min(h, Math.max(0, viewH - top));
  if (vBot <= vTop) return 0;   // none of the row is on screen — nothing to anchor to
  // Never take more than half the slice for the band: on a sliver, being INSIDE what
  // the user can see beats clearing a band that is already faded anyway.
  const cut = Math.min(bottom > viewH + 0.5 ? REVEAL_SMOOTH_FEATHER_PX : 0, (vBot - vTop) / 2);
  return h - (vBot - cut);
};

// …and whether it may be shown AT ALL: on a short visible slice the pill overflows onto
// the neighbouring bubble, so a row that cannot hold it cleanly gets none; a fully
// visible row always does. `hidden` gives the threshold hysteresis against flutter. Pure.
export const REVEAL_TRIGGER_SIZE = 21;    // .chat-row-menu-btn is 21×21
export const REVEAL_TRIGGER_PAD = 4;      // clearance from the neighbouring bubble
export const REVEAL_TRIGGER_HYSTERESIS = 2;
export const revealTriggerFits = (top, bottom, viewH, hidden = false) => {
  const h = bottom - top;
  if (h <= 0) return false;
  const clipped = top < -0.5 || bottom > viewH + 0.5;
  if (!clipped) return true;
  const vTop = Math.min(h, Math.max(0, -top));
  const vBot = Math.min(h, Math.max(0, viewH - top));
  const cut = Math.min(bottom > viewH + 0.5 ? REVEAL_SMOOTH_FEATHER_PX : 0, (vBot - vTop) / 2);
  const room = REVEAL_TRIGGER_SIZE + REVEAL_TRIGGER_PAD + cut + (hidden ? REVEAL_TRIGGER_HYSTERESIS : 0);
  return vBot - vTop >= room;
};

// Grain for a row. The dot screen covers the whole element, so the clipped share won't
// do: a row taller than the scroller is clipped by definition and would stay speckled.
// Showing as much as the viewport holds counts as fully visible. Pure.
export const revealGrain = (top, bottom, viewH) => {
  const h = bottom - top;
  if (viewH <= 0 || h <= 0) return 0;
  const visible = Math.max(0, Math.min(bottom, viewH) - Math.max(top, 0));
  return 1 - visible / Math.min(h, viewH);
};

// Watch `root` and ramp every child matching `selector` as it scrolls. Returns a
// disconnect function; safe to call in any environment.
// Deliberately LAZY: geometry is measured once per list change (per-frame rect reads
// force layout and stutter) and the mask mounts only on rows straddling an edge.
// `smooth` is for TEXT rows (the chat transcript): the dot grain eats letters on the
// cut edge, so they get one clean alpha fade — and the "…"-fits class.
export function observeReveal(root, selector, { smooth = false } = {}) {
  const noop = () => {};
  if (!root?.addEventListener || typeof requestAnimationFrame === 'undefined') return noop;

  const seen = new WeakSet();
  let rows = [];        // cached geometry: { el, top, h } in the scroller's content space
  let raf = 0;

  // One layout read for the whole list, and only when the list itself changed.
  const measure = () => {
    const rootTop = root.getBoundingClientRect().top - root.scrollTop;
    rows = [...root.querySelectorAll(selector)].map((el) => {
      const r = el.getBoundingClientRect();
      return { el, top: r.top - rootTop, h: r.height };
    });
  };

  const apply = () => {
    raf = 0;
    const viewH = root.clientHeight;
    const scrollTop = root.scrollTop;
    for (const row of rows) {
      const top = row.top - scrollTop;             // pure arithmetic — no layout read
      const bottom = top + row.h;
      const d = revealDissolve(top, bottom, viewH);
      // Only a row straddling an edge needs the mask; everything else is either fully
      // readable or fully clipped away by the scroller.
      const masked = d > 0.001 && d < 0.999;
      if (masked !== row.masked) {
        row.masked = masked;
        row.el.classList.toggle(REVEAL_MASKED_CLASS, masked);
      }
      // Published for EVERY row, masked or not (and before the bailout below): bottom-
      // anchored chrome rides this so a cut row keeps its control against the visible
      // bottom. Written only when it moves — this runs per scroll frame.
      const vb = Math.round(revealVisibleBottom(top, bottom, viewH));
      if (vb !== row.visBottom) {
        row.visBottom = vb;
        row.el.style.setProperty('--visible-bottom', `${vb}px`);
      }
      // …and whether that chrome fits at all, or would be drawn over the neighbour.
      if (smooth) {
        const hide = !revealTriggerFits(top, bottom, viewH, row.noTrigger);
        if (hide !== row.noTrigger) {
          row.noTrigger = hide;
          row.el.classList.toggle(REVEAL_NO_TRIGGER_CLASS, hide);
        }
      }
      if (!masked) continue;
      if (row.h > 0) {
        row.el.style.setProperty('--vis-start', `${(Math.min(1, Math.max(0, -top / row.h)) * 100).toFixed(2)}%`);
        row.el.style.setProperty('--vis-end', `${(Math.min(1, Math.max(0, (viewH - top) / row.h)) * 100).toFixed(2)}%`);
      }
      // …softened only where the scroller really cuts (revealFeather): an edge the user
      // can see in full stays hard, so nothing readable — nor the "…" trigger on the
      // row's bottom edge — is sanded away by a clip happening at the OTHER end.
      const fade = revealFeather(top, bottom, viewH, smooth ? REVEAL_SMOOTH_FEATHER : REVEAL_FEATHER);
      row.el.style.setProperty('--fade-in', fade.in);
      row.el.style.setProperty('--fade-out', fade.out);
      // The soft edge (--vis-*) follows the clipping; the grain has its own ramp so a
      // long message stays readable while you are reading it. Text rows have no grain.
      if (!smooth) row.el.style.setProperty('--dissolve', revealGrain(top, bottom, viewH).toFixed(3));
    }
  };
  const schedule = () => { if (!raf) raf = requestAnimationFrame(apply); };
  const remeasure = () => { measure(); schedule(); };

  // New rows start dissolved and carry a LONGER transition for their first ramp, so an
  // arriving message assembles visibly instead of tracking the scroll instantly.
  const scan = () => {
    for (const el of root.querySelectorAll(selector)) {
      if (seen.has(el)) continue;
      seen.add(el);
      el.classList.add(REVEAL_ITEM_CLASS, REVEAL_MASKED_CLASS, REVEAL_ENTERING_CLASS);
      if (smooth) el.classList.add(REVEAL_SMOOTH_CLASS);
      el.style.setProperty('--dissolve', smooth ? '0' : '1');
      el.style.setProperty('--vis-start', '50%');   // assembles outward from its middle
      el.style.setProperty('--vis-end', '50%');
      setTimeout(() => el.classList.remove(REVEAL_ENTERING_CLASS), REVEAL_ENTER_MS + 60);
    }
    remeasure();
  };
  scan();

  root.addEventListener('scroll', schedule, { passive: true });
  let mo = null;
  if (typeof MutationObserver !== 'undefined') {
    mo = new MutationObserver(scan);
    mo.observe(root, { childList: true, subtree: true });
  }
  let ro = null;
  if (typeof ResizeObserver !== 'undefined') {
    ro = new ResizeObserver(remeasure);   // the scroller resized: every cached top moved
    ro.observe(root);
  }
  return () => {
    root.removeEventListener('scroll', schedule);
    mo?.disconnect();
    ro?.disconnect();
  };
}

// ── Menu pop (canvas context menu + chat row menu) ──────────────────────────
// transform-origin for a fixed-position menu popping out of its OPEN point: the
// click clamped into the menu's placed box (post viewport clamping), relative to
// its top-left — so the pop visibly grows out of the cursor. Pure.
export const menuPopOrigin = (x, y, rect) => {
  const cl = (v, max) => Math.min(Math.max(v, 0), max);
  return `${cl(x - rect.left, rect.width)}px ${cl(y - rect.top, rect.height)}px`;
};

// ── FLIP: play an element's NEW box out of the one it had a moment ago ──────
// Used by the fullscreen toggle: entering stretches out of the old box, leaving
// minimises back into the new one. Call AFTER the layout change, with the rect
// measured BEFORE it. Transform-only, so it never re-triggers layout.
// Long and hard-eased-out, so a full-window stretch reads as deliberate.
export const FLIP_MS = 560;
export const FLIP_EASING = 'cubic-bezier(0.16, 1, 0.22, 1)';
// Held on the element for the whole flight. CSS uses it to lift the element above the
// page and stop its scrollbars flashing while it is scaled — without it, an element
// scaled UP out of its in-flow box paints behind the toolbars around it.
export const FLIP_ACTIVE_CLASS = 'flip-active';

// The inverse transform mapping `to` back onto `from`, or null when either box is
// degenerate or the two already match (nothing to play). Pure — unit-tested.
export function flipTransform(from, to) {
  if (!from || !to) return null;
  if (!(from.width > 0 && from.height > 0 && to.width > 0 && to.height > 0)) return null;
  const sx = from.width / to.width;
  const sy = from.height / to.height;
  const dx = from.left - to.left;
  const dy = from.top - to.top;
  const still = Math.abs(sx - 1) < 0.001 && Math.abs(sy - 1) < 0.001
    && Math.abs(dx) < 0.5 && Math.abs(dy) < 0.5;
  if (still) return null;
  return `translate(${dx}px, ${dy}px) scale(${sx}, ${sy})`;
}

export function flipFrom(el, from, { ms = FLIP_MS, activeClass = FLIP_ACTIVE_CLASS } = {}) {
  if (!el?.style || typeof requestAnimationFrame === 'undefined') return;
  if (motionReduced()) return;
  const clear = () => {
    el.classList?.remove(activeClass);
    el.style.transition = '';
    el.style.transform = '';
    el.style.transformOrigin = '';
    el.style.willChange = '';
  };
  clearTimeout(el._flipTimer);
  clear();
  // Measured with the class ON: it changes overflow (and so whether scrollbars take
  // space), and a box measured without it would be the wrong one to invert.
  el.classList?.add(activeClass);
  const invert = flipTransform(from, el.getBoundingClientRect());
  if (!invert) { clear(); return; }
  el.style.transformOrigin = 'top left';
  el.style.willChange = 'transform';
  el.style.transition = 'none';
  el.style.transform = invert;
  // Two frames: one lands the inverted state, the next starts the transition off it
  // (setting both in the same frame would be coalesced into no animation at all).
  requestAnimationFrame(() => requestAnimationFrame(() => {
    el.style.transition = `transform ${ms}ms ${FLIP_EASING}`;
    el.style.transform = 'none';
  }));
  el._flipTimer = setTimeout(clear, ms + 90);
}

// ── Theme / accent swap ─────────────────────────────────────────────────────
// The new palette floods out of the control that changed it, as a growing circle —
// the native View Transitions API where it exists, a plain cross-fade elsewhere.
// Either way `apply` runs exactly once and synchronously.
// Drives both the wipe (as --swap-ms) and the colour cross-fade. One length across
// all three surfaces (extension accent.js SWAP_MS, desktop themeSwapOverlay.hpp kSwapMs).
export const THEME_SWAP_MS = 280;
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
const bezierY = (t, x1, y1, x2, y2) => {
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
export const SWAP_EDGE_POINTS = 240;

// The per-vertex reach multiplier of vertex k: 1 + the style's jitter. Pure.
export const swapEdgeJitter = (k, style = styleCode()) => edgeJitter(style, k, SWAP_EDGE_POINTS);

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

// ── Dust in the wipe's wake ─────────────────────────────────────────────────
// The torn front kicks up dust as it passes: specks igniting along the edge and
// settling just behind it, in the OLD palette's colours — the paint the front grinds
// away. Always just INSIDE the clip: during a view transition the page renders through
// ::view-transition-new(root), so a mote ahead of the front simply would not be seen —
// they spawn behind even the deepest tooth (the 1 − amp band).
// (Desktop twin: themeSwapOverlay.hpp dustMoteAt.)
export const SWAP_DUST_MOTES = 4500;
export const SWAP_DUST_LIFE_MS = 340;
// A mote never ignites at the very ends of the wipe: at t=0 the ring is a point (nothing
// to ride), and the last ones still get their whole life before the layer is reaped.
export const SWAP_DUST_MIN_T = 0.06;
export const SWAP_DUST_MAX_T = 0.94;

// The style the particles wear right now, as dustCloud.js's code (0 = dust, the flight
// as tabulated). Every cloud builder below reads it once, when the cloud is built.
const styleCode = () => PARTICLE_STYLES[particleStyle()] || 0;

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
const SWAP_DUST_STEPS = 256;
const swapDustCurve = Float32Array.from({ length: SWAP_DUST_STEPS + 1 },
  (_, i) => bezierY(i / SWAP_DUST_STEPS, 0.22, 0.55, 0.3, 1));
// Both ends exactly: the solver bisects to within a hair of 0 and 1, and that hair is
// enough to start a grain a fraction off its home and leave it a fraction lit.
swapDustCurve[0] = 0;
swapDustCurve[SWAP_DUST_STEPS] = 1;
export const swapDustEase = (t) =>
  swapDustCurve[Math.min(SWAP_DUST_STEPS, Math.max(0, Math.round(t * SWAP_DUST_STEPS)))];

// Opacity flares over the first 18% of a grain's life and falls away across the rest.
export const SWAP_DUST_FLARE = 0.18;

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
const swapDustPaint = () => {
  try {
    const s = getComputedStyle(document.documentElement);
    const v = (name) => (s.getPropertyValue(name) || '').trim();
    if (!v('--accent')) return null;
    // The wake is painted from the departing accent palette — the same two colours every
    // cloud wears (dustCloud.js paletteCss) — resolved while they still mean the OLD theme.
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
function spawnSwapDust(px, paint) {
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
    // One run of one fillStyle per palette stop, so a frame is six fills rather than a
    // thousand switches — resolved already, before the palette moved under us.
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
        stopOf[i] = paletteIndex(style ? sf.mix : dustMix(s.w, s.accent), runs.length);
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

export function themeSwap(apply, origin = null) {
  if (typeof document === 'undefined') { apply(); return; }
  const root = document.documentElement;
  const reduced = motionReduced();

  if (reduced || typeof document.startViewTransition !== 'function') {
    // No snapshot to wipe: one beat of colour transition instead (a no-op under reduced
    // motion). A document without a classList is a test stub — the palette write still
    // happens; only the decoration is skipped.
    if (!root?.classList) { apply(); return; }
    root.classList.add(THEME_SWAP_CLASS);
    clearTimeout(root._themeSwapTimer);
    apply();
    root._themeSwapTimer = setTimeout(() => root.classList.remove(THEME_SWAP_CLASS), THEME_SWAP_MS);
    return;
  }

  // `origin` may be a POINT or a function that resolves one. A function is re-asked after
  // the palette is written: the control can move between the two (label width changes,
  // toolbar reflow, scroll), and the circle is painted against the NEW page.
  // `last` keeps the same answer in PIXELS — the dust is seeded off wherever the circle
  // was really painted from, so the wake and the ring can never disagree.
  let last = null;
  const at = () => {
    const w = window.innerWidth, h = window.innerHeight;
    const p = (typeof origin === 'function' ? origin() : origin) || { x: w / 2, y: h / 2 };
    last = { x: p.x, y: p.y, w, h };
    return swapPercent(p.x, p.y, w, h);
  };
  // The OLD palette, read before `apply` flips it — the wake is the paint coming off.
  const paint = swapDustPaint();
  // Handed to the DECLARATIVE keyframes in animations.css. Scripting the animation from
  // ready.then() instead races the transition's own teardown — it ends as soon as its
  // pseudo-elements have no animations, so the wipe stopped half way.
  // --swap-x/y/r stay the wipe's authoritative geometry record (and the keyframes'
  // circle fallback); the clip the reveal actually plays is the ragged polygon pair.
  const write = ({ x, y, r }) => {
    root.style.setProperty('--swap-x', `${x}%`);
    root.style.setProperty('--swap-y', `${y}%`);
    root.style.setProperty('--swap-r', `${r}%`);
    root.style.setProperty('--swap-ms', `${THEME_SWAP_MS}ms`);
    const from = last && swapEdgePolygon(last.x, last.y, last.w, last.h, 0);
    if (from) {
      root.style.setProperty('--swap-clip-from', from);
      root.style.setProperty('--swap-clip-to', swapEdgePolygon(last.x, last.y, last.w, last.h, 1));
    }
  };
  write(at());
  // Raised BEFORE startViewTransition: the browser drops :hover (synthetic
  // pointerleaves) the moment the transition starts, and hover-latches (ui/toolbar.js
  // logo) tell that synthetic leave from a real one by this class.
  root.classList.add(THEME_INSTANT_CLASS);
  const settle = () => root.classList.remove(THEME_INSTANT_CLASS);
  let t;
  try {
    // The re-ask rides INSIDE the update callback: it is the one place that runs after the
    // palette is written and before the new state is captured, so a control that moved is
    // measured where the wipe will actually be seen.
    t = document.startViewTransition(() => { apply(); write(at()); });
  } catch {
    settle(); apply(); return;   // a sync throw never ran the callback — the write still must happen
  }
  t.finished.then(settle, settle);   // also on a skipped/failed transition
  // Dust rides in only once the wipe's own animation is running (ready), so a mote's
  // delay and the ring's clock start on the same frame. An engine without `ready`
  // (or a skipped transition) simply gets no dust — the wipe never depends on it.
  t.ready?.then?.(() => spawnSwapDust(last, paint), () => {});
}

// Resolve an id to a swap origin, preferring the element that is actually ON SCREEN:
// the fullscreen layer CLONES the whole toolbar, duplicate ids and all, so
// getElementById can hand back a hidden copy.
export function originOfId(id) {
  if (typeof document?.querySelectorAll !== 'function') return null;
  for (const el of document.querySelectorAll(`[id="${id}"]`)) {
    const o = originOf(el);
    if (o) return o;
  }
  return null;
}

// Clipped out of sight by an ANCESTOR? checkVisibility below cannot see that: a control
// inside a collapsed panel keeps a perfectly good rect and its own `visibility: visible`;
// it is the ancestor's `overflow: hidden` that hides it.
function clippedAway(el, r) {
  if (typeof getComputedStyle !== 'function') return false;   // a stub (tests): nothing to clip
  for (let p = el.parentElement; p; p = p.parentElement) {
    const s = getComputedStyle(p);
    if (s.overflow === 'visible' && s.overflowX === 'visible' && s.overflowY === 'visible') continue;
    const b = p.getBoundingClientRect();
    if (r.right <= b.left || r.left >= b.right || r.bottom <= b.top || r.top >= b.bottom) return true;
  }
  return false;
}

export function originOf(el) {
  const r = el?.getBoundingClientRect?.();
  if (!r || (!r.width && !r.height)) return null;
  if (el.parentElement && clippedAway(el, r)) return null;
  // `visibility: hidden` and `opacity: 0` both leave a perfectly good rect behind, and
  // an off-screen clone keeps its size too — never bloom from one. checkVisibility is
  // Chromium/WebKit-only, so it stays a bonus check.
  if (typeof el.checkVisibility === 'function' &&
      !el.checkVisibility({ visibilityProperty: true, opacityProperty: true, contentVisibilityAuto: true }))
    return null;
  const c = { x: r.left + r.width / 2, y: r.top + r.height / 2 };
  const w = typeof window !== 'undefined' ? window.innerWidth : 0;
  const h = typeof window !== 'undefined' ? window.innerHeight : 0;
  if (w && h && (c.x < 0 || c.y < 0 || c.x > w || c.y > h)) return null;   // scrolled/parked off-screen
  return c;
}

// ── Arriving ────────────────────────────────────────────────────────────────
// The counterpart to leaveThenRemove: dropped content plays IN out of the drop point.
// Without one (the Open dialog, a paste, a fetched URL) there is no place to come
// from, so it keeps the plain landing: a scale-up plus the accent pulse.
export const ARRIVE_MS = 620;
export const ARRIVE_GLOW_CLASS = 'drop-arriving';   // glow only — the FLIP owns the transform
export const ARRIVE_ACTIVE_CLASS = 'arrive-active';
export const LANDING_CLASS = 'drop-landing';        // scale-up + glow, for the no-point case

// The box the arrival flies out of: a small one centred on the drop point. Kept small so
// the content visibly grows out of the cursor, and non-degenerate so flipTransform will
// play it at all. Pure.
export const arrivalBox = (point, size = 96) => ({
  left: point.x - size / 2,
  top: point.y - size / 2,
  width: size,
  height: size * 0.75,
});

// `point` is the drop position in client coordinates ({x, y}), or null.
export function arriveFrom(el, point = null, { ms = ARRIVE_MS } = {}) {
  if (!el?.classList) return;
  const usable = Number.isFinite(point?.x) && Number.isFinite(point?.y);
  if (!usable) { flashLanding(el, LANDING_CLASS, 700); return; }
  // Glow and flight together: the glow is a CSS animation, the flight an inline
  // transform, so the two never fight over the same property.
  flashLanding(el, ARRIVE_GLOW_CLASS, ms + 120);
  flipFrom(el, arrivalBox(point), { ms, activeClass: ARRIVE_ACTIVE_CLASS });
}

// ── Leaving ─────────────────────────────────────────────────────────────────
// A row that is about to be destroyed collapses and fades out first, so a delete
// reads as the row going away rather than the list jumping. The caller does the
// actual removal in the callback — this only buys it the time.
export const LEAVE_MS = 220;
// Chat entries come apart into a finer grid than a list row — the extra particles are
// what make a delete read as "it dissolved" rather than "it blinked out"; the fade
// stays SHORT. Kept here so the panel, the flyout and the extension use one number.
export const CHAT_LEAVE_MS = 260;
// Attachment chips leave on a LONGER clock: the row holds the doomed chip's slot for
// an opening beat (css chipLeave's hold phase) so the dust reads before the survivors
// slide over — the removal must not fire until the collapse really ends
export const CHIP_LEAVE_MS = 420;
export const CHAT_DISINTEGRATE_COLS = 32;
export const CHAT_DISINTEGRATE_ROWS = 16;
export const LEAVING_CLASS = 'leaving';

// A WIPE scatters every row at once and the cost is the sum, not the per-row grid —
// so the mesh is budgeted: one row keeps the full fine grain, a mass clear coarsens
// each row until the total fits. Pure — unit-tested.
export const SCATTER_TILE_BUDGET = 1200;
// …and past this many simultaneous rows the extra ones simply fade: a dozen scatters
// at once is already more than the eye resolves, and 200 of them would blow any mesh
// budget however coarse it got.
export const SCATTER_MAX_ROWS = 12;
// `fine` is the one-row grid to start from — the chat's by default; the lists bring a
// finer one (ROW_DUST_GRID), still under the same total budget.
export const scatterGridFor = (count, index = 0, fine = { cols: CHAT_DISINTEGRATE_COLS, rows: CHAT_DISINTEGRATE_ROWS }) => {
  const n = Math.min(Math.max(1, count | 0), SCATTER_MAX_ROWS);
  if (index >= SCATTER_MAX_ROWS) return { cols: 0, rows: 0 };   // fade only, no dust
  const total = n * fine.cols * fine.rows;
  if (total <= SCATTER_TILE_BUDGET) return fine;
  // Scale both axes by the same factor, so the cells stay square-ish, and keep a floor
  // — below it the "dust" reads as broken glass instead.
  const k = Math.sqrt(SCATTER_TILE_BUDGET / total);
  return { cols: Math.max(8, Math.round(fine.cols * k)), rows: Math.max(4, Math.round(fine.rows * k)) };
};

// Play `el` out, then run `done`. Returns a promise resolving after `done`, so a
// caller can await the whole thing. `done` ALWAYS runs, even with no element and
// even under reduced motion — the removal must never depend on the animation.
export function leaveThenRemove(el, done = () => {}, { ms = LEAVE_MS, cols, rows, dustMs = 0,
                                                      drift = 1, px = 0 } = {}) {
  const finish = () => { try { done(); } catch { /* the caller owns its own errors */ } };
  const reduced = motionReduced();
  if (!el?.classList || reduced || typeof setTimeout === 'undefined') { finish(); return Promise.resolve(); }
  // Freeze the height so the collapse has something to animate from (rows are
  // auto-height, and `height: auto → 0` does not transition). Width too — chips
  // leave a HORIZONTAL row and collapse sideways (.chat-attach-chip.leaving).
  if (el.getBoundingClientRect) {
    const r = el.getBoundingClientRect();
    if (r.height) el.style.setProperty('--leave-h', `${r.height}px`);
    if (r.width) el.style.setProperty('--leave-w', `${r.width}px`);
  }
  // The row's own box collapses on its short timer (the list closes the gap) while a
  // copy scatters into particles that own their lifetime in their own layer over the
  // page. cols === 0 is the budget's "fade only" (scatterGridFor past SCATTER_MAX_ROWS).
  if (cols !== 0) disintegrate(el, { ...(cols ? { cols } : {}), ...(rows ? { rows } : {}),
                                    ...(dustMs ? { ms: dustMs } : {}), ...(px ? { px } : {}),
                                    ...(drift === 1 ? {} : { drift }) });
  el.classList.add(LEAVING_CLASS);
  return new Promise((resolve) => setTimeout(() => { finish(); resolve(); }, ms));
}

// How long a wipe REALLY lasts on screen: leaveThenRemove resolves on the short
// collapse while the particles fall for DISINTEGRATE_MS — a caller swapping in a
// PLACEHOLDER must wait for the longer one (same rule as storage.js's GHOST_MS hold).
// 0 under reduced motion: nothing is playing.
export const wipeDurationMs = (dustMs = 0) => {
  if (motionReduced()) return 0;
  if (dustMs) return dustEnabled() ? Math.max(LEAVE_MS, dustMs) : LEAVE_MS;
  // No dust ('slide') means no particle tail to wait out — the row's own collapse is
  // the whole wipe, and a caller holding for the longer clock would just stall.
  return dustEnabled() ? Math.max(LEAVE_MS, DISINTEGRATE_MS) : LEAVE_MS;
};

// ── List hold: wipes in flight ──────────────────────────────────────────────
// The projects modal's removal-in-flight hold as a shared factory: begin() returns a
// settle fn that waits out the REAL wipe (wipeDurationMs) then settles exactly once;
// `holding` gates out-of-band re-renders meanwhile; finalizeAll() settles everything
// NOW (a modal closing mid-animation). Timers injectable — unit-tested.
export const createListHold = ({ settle = () => {}, wait = wipeDurationMs, setTimer = setTimeout } = {}) => {
  const pending = new Set();
  const begin = () => {
    let done = false;
    const finish = () => {
      if (done) return;
      done = true;
      pending.delete(finish);
      settle();
    };
    pending.add(finish);
    return () => new Promise((resolve) => setTimer(() => { finish(); resolve(); }, wait()));
  };
  return {
    begin,
    finalizeAll: () => { for (const f of [...pending]) f(); },
    get holding() { return pending.size > 0; },
  };
};

// ── Filtering a list ────────────────────────────────────────────────────────
// A filter is a question re-answered, not a removal: dropped rows just vanish with the
// rebuild, and the whole effect belongs to the rows that are LEFT, which assemble anew.
export const FILTER_ENTER_MS = 340;
// Small enough that a long list still settles in one beat.
export const FILTER_STAGGER_MS = 22;
// Past this many rows the effect is noise (and a cost) — the rest simply appear.
export const FILTER_MAX_ANIMATED = 16;
export const FILTER_ENTERING_CLASS = 'filter-entering';
// The kept rows form from anonymous specks (speckPainter) on a shorter, non-destructive
// throw — a filter is a view change, never mistakable for a deletion's scatter.
export const FILTER_DUST_MS = 560;
export const FILTER_DUST_DRIFT = 0.5;
// `index`/`count` share ONE mesh budget across every row a change moves (scatterGridFor),
// so a filter that leaves a dozen rows costs about what one deletion does. Past the
// budget's row ceiling a row simply fades, as it always did.
// `box` is an optional pre-measured rect, so a burst of rows can be measured in one
// read pass before any cloud's writes start (createFilterAnimator's playEnter).
export const filterDust = (el, index = 0, count = 1, box = null) => {
  const { cols, rows } = scatterGridFor(count, index);
  if (!cols || !el?.getBoundingClientRect || motionReduced()) return false;
  return disintegrate(el, {
    cols, rows, gather: true, ms: FILTER_DUST_MS, drift: FILTER_DUST_DRIFT, px: MOTE_PX,
    toBody: true, hostClass: 'dust-forming', paintTile: speckPainter(el), box,
  });
};

// What a filter change does to a list, by row key: which keys it drops, which it
// reveals, and whether the sequence moved at all (`moved` is what a SORT switch has —
// same membership, new order). Pure — unit-tested.
export const filterDelta = (before = [], after = []) => {
  const prev = [...before];
  const next = [...after];
  const prevSet = new Set(prev);
  const nextSet = new Set(next);
  return {
    leaving: prev.filter((k) => !nextSet.has(k)),
    entering: next.filter((k) => !prevSet.has(k)),
    moved: prev.length !== next.length || prev.some((k, i) => k !== next[i]),
  };
};

// One filter animator per list. `keys()` is what the list shows RIGHT NOW, `next()`
// what the pending state WILL show, `render()` rebuilds it, `find(key)` resolves a
// rendered row. Returns a run() for every filter/sort/search handler to call.
//
// render() ALWAYS runs exactly once per call, FIRST and synchronously: the new answer is
// on screen before any decoration starts, so nothing about what you can see waits on an
// effect and a burst of keystrokes can never leave a stale set behind. What plays is the
// arrival of the rows that are LEFT — every one of them, because the filtered set IS the
// thing that changed, not just the rows that happen to be new to it.
export const createFilterAnimator = ({
  keys = () => [], next = () => [], render = () => {}, find = () => null,
  enterMs = FILTER_ENTER_MS, max = FILTER_MAX_ANIMATED,
  setTimer = setTimeout, reduced = motionReduced,
} = {}) => {
  const rowsFor = (keyList) => keyList.slice(0, max).map((k) => find(k)).filter((el) => el?.classList);

  const playEnter = (rows) => {
    // Every rect in ONE read pass, then clouds/classes in a write pass — interleaving
    // the two forced a layout per row on every keystroke.
    const boxes = rows.map((el) => el.getBoundingClientRect?.() || null);
    rows.forEach((el, i) => {
      const delay = i * FILTER_STAGGER_MS;
      if (el.style) el.style.animationDelay = `${delay}ms`;
      filterDust(el, i, rows.length, boxes[i]);
      el.classList.add(FILTER_ENTERING_CLASS);
      setTimer(() => {
        el.classList.remove(FILTER_ENTERING_CLASS);
        if (el.style) el.style.animationDelay = '';
      }, enterMs + delay + 60);
    });
  };

  return () => {
    const after = [...next()];
    const { leaving, entering, moved } = filterDelta(keys(), after);
    render();
    // A keystroke that narrows nothing must not flash the list: only a change that
    // really moved the answer is worth replaying.
    const arriving = (leaving.length || entering.length || moved) ? after : [];
    if (!reduced()) playEnter(rowsFor(arriving));
    return Promise.resolve({ leaving, entering: arriving });
  };
};

// May a list's "nothing here" placeholder show RIGHT NOW? Only when it is truly empty
// AND no wipe is still playing — under a hold the empty state would land beneath the
// falling ash and read as appearing before the removal finished. Pure — unit-tested.
export const emptyStateVisible = (count, holding = false) => count === 0 && !holding;

// ── Replaying a glyph's state motion on a switch ─────────────────────────────
// A control that switches wears one of these classes for one flight, so the parts
// whose visibility IS that state (the mic's sound waves) arrive with the hover's own
// swell when it goes ON, and fly out past the edges when it goes OFF, rather than just
// appearing / vanishing. CSS: `.voice-waves-in` / `.voice-waves-out` in animations.css.
export const WAVES_IN_CLASS = 'voice-waves-in';
export const WAVES_OUT_CLASS = 'voice-waves-out';
export const WAVES_FLIGHT_MS = 500;
export function replayWaves(el, on, { setTimer = setTimeout } = {}) {
  if (!el?.classList) return;
  el.classList.remove(WAVES_IN_CLASS, WAVES_OUT_CLASS);
  void el.offsetWidth;   // restart the keyframes on a quick double switch
  const cls = on ? WAVES_IN_CLASS : WAVES_OUT_CLASS;
  el.classList.add(cls);
  // The class comes off once the flight is over — but not under a resting pointer: the
  // hover trigger would then take the waves back and play its own swell a second time.
  const done = () => {
    if (el.matches?.(':hover')) { el.addEventListener('pointerleave', () => el.classList.remove(cls), { once: true }); return; }
    el.classList.remove(cls);
  };
  setTimer(done, WAVES_FLIGHT_MS);
}

// ── Swapping a control's face ───────────────────────────────────────────────
// One shared transition for the toggles that rewrite themselves in place — the Draw
// group's Start↔Stop and Line↔Rect. Replacing innerHTML outright cannot animate, so
// the new markup is written FIRST (the DOM is never behind the state, however fast
// the toggling) and the decoration plays around it: the new glyph turns in, the new
// word rises, and the outgoing face leaves as a ghost stacked on top of it. CSS owns
// the keyframes (animations.css .swapping / .swap-ghost).
export const SWAP_MS = 260;
export const SWAP_CLASS = 'swapping';
export const SWAP_GHOST_CLASS = 'swap-ghost';

// The face each element last rendered, and the generation of its in-flight swap.
// Keyed by the ELEMENT: a re-rendered toolbar hands us a fresh node with no entry,
// which paints rather than being skipped as unchanged.
const swapFace = new WeakMap();
const swapGen = new WeakMap();

/**
 * Swap an element's content with the shared transition. `key` identifies the face
 * (markup does not survive a DOM round-trip byte-for-byte, so it is not the trigger).
 * Returns whether the swap ANIMATED — false for an unchanged face, the first paint,
 * or reduced motion, all of which still leave the correct content behind.
 */
export function swapContent(el, html, {
  key = html, ms = SWAP_MS, reduced = motionReduced, setTimer = setTimeout,
} = {}) {
  if (!el) return false;
  const first = !swapFace.has(el);
  if (!first && swapFace.get(el) === key) return false;
  swapFace.set(el, key);
  // Drop a ghost still in flight: it belongs to a face that is now two swaps old.
  for (const g of el.querySelectorAll?.(`.${SWAP_GHOST_CLASS}`) || []) g.remove?.();
  const before = el.innerHTML;
  el.innerHTML = html;
  if (first || reduced()) return false;

  const doc = el.ownerDocument || (typeof document !== 'undefined' ? document : null);
  const ghost = el.appendChild ? doc?.createElement?.('span') : null;
  if (ghost) {
    ghost.className = SWAP_GHOST_CLASS;
    ghost.innerHTML = before;
    ghost.setAttribute?.('aria-hidden', 'true');
    el.appendChild(ghost);
  }
  const gen = (swapGen.get(el) || 0) + 1;
  swapGen.set(el, gen);
  el.classList?.remove(SWAP_CLASS);
  void el.offsetWidth;   // reflow, so the keyframes replay from the top mid-swap
  el.classList?.add(SWAP_CLASS);
  setTimer(() => {
    if (swapGen.get(el) !== gen) return;   // a newer swap owns the element now
    el.classList?.remove(SWAP_CLASS);
    ghost?.remove?.();
  }, ms + 60);
  return true;
}

// ── Pinning a swapping control's box ────────────────────────────────────────
// A face that swaps in place must not resize the button under the cursor, so the Draw
// group's two toggles are width-pinned. The pin is MEASURED, never guessed: each face is
// written into THE BUTTON ITSELF at width:auto and measured there, so the number comes
// from the real font, gap, padding and border — a clone loses whatever its id styles it
// with. That survives a font swap, a zoom and a translated label; a hard-coded rem does
// not. The whole probe is synchronous, so no intermediate face is ever painted, and the
// original markup is put back before returning (the caller's swap sees no change).
// Once per element — a re-rendered toolbar hands over a new node, which re-measures —
// plus one re-measure when webfonts settle, since metrics can change under us.
// `prop` picks which box the number lands in: `width` PINS the control (the Draw toggles,
// whose two faces are the only widths it will ever hold), `minWidth` gives it a FLOOR that
// CSS may still stretch — a dropdown whose option list can grow past `max` later.
// `max` caps the pin, so one very long option cannot push a control past its row.
const facePinned = new WeakSet();
export function pinWidestFace(el, faces, { doc = el?.ownerDocument, force = false,
                                           prop = 'width', max = Infinity } = {}) {
  if (!el || !faces?.length || !el.style || !el.getBoundingClientRect) return 0;
  if (!force && facePinned.has(el)) return 0;
  const html0 = el.innerHTML, pin0 = el.style[prop];
  el.style[prop] = 'auto';                 // beats the pin; min-width:max-content is the floor
  let widest = 0;
  for (const html of faces) {
    el.innerHTML = html;
    widest = Math.max(widest, el.getBoundingClientRect().width || 0);
  }
  el.innerHTML = html0;
  el.style[prop] = pin0;
  if (!(widest > 0)) return 0;             // no layout (a stub, a hidden panel): keep the CSS floor
  const px = Math.min(Math.ceil(widest), max);
  el.style[prop] = `${px}px`;
  if (!facePinned.has(el)) {
    facePinned.add(el);
    doc?.fonts?.ready?.then?.(() => {
      if (el.isConnected !== false) pinWidestFace(el, faces, { doc, force: true, prop, max });
    });
  }
  return px;
}

// ── Disintegration ("the snap") ─────────────────────────────────────────────
// A removed element comes apart into MOTES: one round speck per grid cell, painted in
// the element's own colours (speckPainter), drifting off in a staggered sweep. Never
// clones of the element — a clone per cell was hundreds of copies of a row's whole
// subtree, and at mote size it showed nothing a speck does not. The motes live in a
// FIXED layer over the page: the row is collapsing to zero height at the same time and
// would clip anything inside it. Every flight here — a row, a chat entry, a window, a
// tick — is this one particle system (desktop twin: disintegrateOverlay.hpp).
// Half again the app's original 900ms wipe: 520 and 260 were both tried for briskness
// and read as hurried over anything you are still looking at.
// Every dust clock in this file runs about a quarter longer than the desktop's twin of
// it: the browser's grains are flat specks where the desktop flies the window's own
// pixels, and the same span read as hurried here (user report, 2026-09-08).
export const DISINTEGRATE_MS = 1650;
// A fine grid — small cells read as ash rather than a broken window; one node per
// cell, so this is the practical ceiling for a list row. (Matched by the desktop's
// DisintegrateOverlay::kDustCellPx, which sizes its motes in pixels instead.)
export const DISINTEGRATE_COLS = 34;
export const DISINTEGRATE_ROWS = 16;
// However late a mote sets off, it still gets this long to fly: the floor keeps the last
// grains of a short flight (a mark swap, a menu) from being a blink rather than a flight.
export const MIN_TILE_MS = 160;
// The clock a list passes for its own items — the row clock today, named apart because
// the two have been parted before (desktop twin: kItemMs).
export const ITEM_DUST_MS = DISINTEGRATE_MS;
// …and the CONNECTIONS list, the odd one out: a row there is a URL you already know, so
// it comes and goes half again as briskly (desktop twin: kConnMs).
export const CONN_DUST_MS = Math.round(DISINTEGRATE_MS / 1.5);
// ── A LIST ROW's dust: the connections and projects lists ───────────────────
// The throw, as a share of the row default. That default is a fixed pixel count, so over
// a 44px row it is two and a half times its height; this is the desktop's row ratio.
export const ROW_DUST_DRIFT = 0.8;
// …and the grain: at the row default a short row holds a mosaic of big dots rather than
// sand (user report). Finer cells, twice as many, still inside SCATTER_TILE_BUDGET.
export const ROW_DUST_PX = 5;
export const ROW_DUST_GRID = { cols: 40, rows: 30 };   // 1200 = SCATTER_TILE_BUDGET
// The grain and budgeted grid every list row's dust shares (connections + projects) —
// `index` of `count` rows leaving at once. An ARRIVAL keeps materialize's own clock and
// throw, so it takes this alone; a REMOVAL adds the list's clock and the row throw.
export const rowDustGrid = (count = 1, index = 0) => ({
  ...scatterGridFor(count, index, ROW_DUST_GRID), px: ROW_DUST_PX,
});
export const rowLeaveDust = (count, index, dustMs) => ({
  ...rowDustGrid(count, index), dustMs, drift: ROW_DUST_DRIFT,
});
// A gathering tile's flight, and a mote's own jitter, as SHARES of the span (the CSS
// defaults' 0.48s and 60ms of 0.9s) — not divisions by it: shortening DISINTEGRATE_MS
// must shorten both with it, not hand them a bigger slice of a smaller flight.
export const TILE_GATHER_SHARE = 480 / 900;
export const TILE_JITTER_SHARE = 60 / 900;

// Deterministic per-tile jitter — a hash, not Math.random, so the scatter is varied
// but reproducible (and unit-testable). Returns a 0..1 float.
export const tileNoise = (cx, cy) => {
  const h = Math.sin(cx * 127.1 + cy * 311.7) * 43758.5453;
  return h - Math.floor(h);
};

// ── The waypoint: no mote flies a straight line ─────────────────────────────
// Part-way along its throw each mote is pushed off its line by its own amount, to its
// own side — a bend, not a beam — so a cloud churns instead of radiating in spokes.
// CSS plays it as the mid keyframe (--mx/--my; animations.css tileScatter and kin),
// the desktop as a sine bulge on the same throw (disintegrateOverlay.hpp swirlAt).
// The push is a share of the throw, capped: a window's 400px trip must not swing its
// motes across half the page. Pure — unit-tested.
export const WAYPOINT_ALONG = 0.62;
export const SWIRL_SHARE = 0.32;
export const SWIRL_MAX_PX = 44;
export const tileWaypoint = (dx, dy, q) => {
  const len = Math.hypot(dx, dy);
  if (!(len > 0.5)) return { mx: 0, my: 0 };
  const amp = (q - 0.5) * 2 * Math.min(len * SWIRL_SHARE, SWIRL_MAX_PX);
  return {
    mx: Math.round(dx * WAYPOINT_ALONG - (dy / len) * amp),
    my: Math.round(dy * WAYPOINT_ALONG + (dx / len) * amp),
  };
};

// Where a cell goes and when it starts. `reverse` inverts only the SWEEP (for the
// gather — see reintegrate): the mote that leaves first is the last one home, the same
// rule the canvas dust follows (dustDelay); the flight path is shared. Pure.
// `drift` scales the throw: a list row flings its motes tens of pixels, and a 15px
// checkbox indicator doing the same would read as the dialog exploding (the desktop's
// controlSwap.hpp kCheckSwapSpread is this number).
// `span` is the flight's own length: the sweep and its jitter are SHARES of it, so a
// chat entry arriving on a shorter clock (CHAT_ENTER_MS) still lands every mote in time.
export const tileMotion = (cx, cy, cols = DISINTEGRATE_COLS, rows = DISINTEGRATE_ROWS, reverse = false, drift = 1,
                           span = DISINTEGRATE_MS) => {
  const n = tileNoise(cx, cy);
  // A second, decorrelated noise so a mote's SIDEWAYS drift is independent of its fall
  // and its spin — one hash drove all three, which made whole diagonals move as one and
  // read as a sheet tearing rather than a thing coming apart. A third bends the path.
  const m = tileNoise(cx + 41, cy + 17);
  const q = tileNoise(cx + 97, cy + 53);
  // 0 at the TOP row (goes first), 1 at the bottom (goes last): the row crumbles from
  // its top edge downward, the way the cleared image does (ghostOut).
  const progress = rows > 1 ? cy / (rows - 1) : 0;
  // A SCATTER's sweep is half the gather's: the row itself is gone in LEAVE_MS, and a
  // mote still at its 0% pose past that is a dot screen sitting where the row was, not
  // sand leaving (the same halving surfaceMotion's delayScale does). The gather keeps
  // the full sweep — its motes are the row forming, and there is nothing under them.
  const sweep = span * (reverse ? 0.4 : 0.2);
  const delay = Math.round((reverse ? 1 - progress : progress) * sweep + n * span * TILE_JITTER_SHARE);
  // …and the motes FALL, fanning out as they go. Signed drift, so they spread both
  // ways instead of all sliding one.
  const dx = Math.round((m - 0.5) * 66 * drift);
  const dy = Math.round((26 + progress * 30 + n * 44) * drift);
  return {
    delay, dx, dy,
    ...tileWaypoint(dx, dy, q),
    rot: +((m - 0.5) * 70).toFixed(2),
    scale: +(0.3 + n * 0.3).toFixed(2),
  };
};

// Motes sized in PIXELS, not as a share of the element — a fixed grid over a wide row
// gives slivers, over a small card gives real dust. Aim for MOTE_PX; the quoted grid's
// cell COUNT is the frame-budget ceiling. Pure — unit-tested. (Desktop: kDustCellPx.)
export const MOTE_PX = 7;
export const reshapeGrid = (cols, rows, w, h, px = MOTE_PX) => {
  const budget = Math.max(1, cols * rows);
  // At least three bands each way: two reads as a thing splitting in half, not crumbling.
  let c = Math.max(3, Math.round((w || 1) / px));
  let r = Math.max(3, Math.round((h || 1) / px));
  if (c * r > budget) {
    const k = Math.sqrt(budget / (c * r));
    c = Math.max(3, Math.round(c * k));
    r = Math.max(3, Math.round(r * k));
  }
  return { cols: c, rows: r };
};

// Drop the dust layer an element still owns, if any. A superseding open/close calls
// this, so a double-clicked menu never strands a cloud over the page.
export function cancelDust(el) {
  if (!el) return;
  if (typeof clearTimeout === 'function') clearTimeout(el.__dustTimer);
  el.__dustTimer = null;
  el.__dustHost?.__stop?.();   // the canvas loop, before the layer it paints goes
  el.__dustHost?.remove?.();
  el.__dustHost = null;
}

// Re-anchor a still-flying cloud to `el`'s CURRENT box. The host's left/top are pinned
// once, at creation (disintegrate below) — a layout change that moves `el` afterward (a
// sibling toast pushing it up the stack) leaves the cloud stranded at the old spot while
// the real element reappears somewhere else with no motes to show for it. A no-op when
// `el` owns no live cloud.
export function retargetDust(el, r = null) {
  if (!el?.__dustHost || !el.getBoundingClientRect) return;
  r = r || el.getBoundingClientRect();
  if (!(r.width > 0 && r.height > 0)) return;
  el.__dustHost.style.left = `${r.left}px`;
  el.__dustHost.style.top = `${r.top}px`;
}

// Which of dustCloud's FLIGHTS a cloud flies: a surface's aimed gather/scatter, a row's
// fall-and-fan (or its reverse), or a mark's own fall (`flight: 'fall'`, markOut).
const flightOf = (toward, gather, flight) => flight
  || (toward ? (gather ? 'surfaceGather' : 'surfaceScatter') : (gather ? 'gather' : 'scatter'));

export function disintegrate(el, { cols = DISINTEGRATE_COLS, rows = DISINTEGRATE_ROWS,
                                   gather = false, toward = null, ms = 0, px = MOTE_PX,
                                   spread = SURFACE_SPREAD, drift = 1, toBody = false,
                                   hostClass = '', paintTile = null, own = true, box = null,
                                   delayScale = null, flight = null } = {}) {
  if (typeof document === 'undefined' || !el?.getBoundingClientRect || !document.body) return false;
  // Every element-sized cloud in the app is built here, so this is where the motion
  // mode turns particles off: saying no leaves the caller on its own CSS entrance
  // (that is what its `false` return has always meant).
  if (!dustEnabled()) return false;
  try {
    // One cloud per element: the newest gesture owns it. `own: false` opts a flight out of
    // that bookkeeping — an in-place VALUE swap plays two clouds over one element (the old
    // mark leaving, the new one forming) and the second must not cancel the first.
    if (own) cancelDust(el);
    // `box` is for a surface that is not AT the box it dusts over: a folding one is still
    // collapsed the moment its reveal is toggled, so the caller measures the box it is
    // about to take (foldBox) and hands it in. Everything else measures live.
    const r = box || el.getBoundingClientRect();
    if (r.width < 4 || r.height < 4) return false;
    ({ cols, rows } = reshapeGrid(cols, rows, r.width, r.height, px));
    // Specks in the element's own colours unless the caller brought a recipe (a mark
    // whose ink has already left it — markOut's `paint`).
    const paint = paintTile || speckPainter(el);
    const host = document.createElement('div');
    host.className = hostClass ? `disintegrate-host ${hostClass}` : 'disintegrate-host';
    // Decoration, and nothing but: the layer must never take a click or a Tab stop.
    host.setAttribute?.('aria-hidden', 'true');
    host.inert = true;
    const span = ms || DISINTEGRATE_MS;
    // A surface flies on its own (shorter) clock; a row keeps the defaults. A ROW gather
    // on its own clock (a chat entry) keeps the default's proportions: the grain's
    // flight is the span less the reversed sweep (0.48s of 0.9s), so the last mote to
    // set off still lands before the veil lifts.
    const gatherMs = toward || !gather ? span : Math.round(span * TILE_GATHER_SHARE);
    if (ms) {
      host.style.setProperty('--dust-ms', `${ms}ms`);
      host.style.setProperty('--gather-ms', `${gatherMs}ms`);
      // …and the HOST lives the WHOLE span, whatever leg its grains fly: a gather's set
      // off across the reversed sweep, so the last lands at `ms`, and a host fading on
      // the leg alone took every late mote off the screen at half time.
      host.style.setProperty('--host-ms', `${ms}ms`);
    }
    host.style.left = `${r.left}px`;
    host.style.top = `${r.top}px`;
    host.style.width = `${r.width}px`;
    host.style.height = `${r.height}px`;
    const cellW = r.width / cols;
    const cellH = r.height / rows;
    // Every grain, computed once: its home (the cell's centre), its throw and bend
    // (tileMotion / surfaceMotion), its colour, size and clock. The cloud is then ONE
    // canvas evaluating these per frame (dustCloud.js) — no node per mote, so a
    // window-sized cloud costs a few batched fills rather than hundreds of layers.
    const motes = [];
    for (let cy = 0; cy < rows; cy++) {
      for (let cx = 0; cx < cols; cx++) {
        // A row FALLS (tileMotion); a surface flies at the control that owns it. The
        // scatter's sweep is halved (delayScale), and a caller may compress it further:
        // stragglers starting after the rest have gone read as a long, thin tail.
        const m = toward
          ? surfaceMotion(cx, cy, cols, rows, r, toward,
                          { span, spread, delayScale: delayScale ?? (gather ? 1 : 0.5) })
          : tileMotion(cx, cy, cols, rows, gather, drift, span);
        // The speck's size, opacity and glint; its colour comes from the palette below.
        const speck = paint({ cx, cy, cols, rows, cellW, cellH });
        // The sweep is INSIDE the span, never added to it (the desktop overlay's
        // `t = (t - delay) / (1 - delay)`): a late mote flies the window it has left, so
        // the whole cloud is done at `span` instead of trailing a quarter-second of
        // stragglers past it. Gathers already fit — their flight is the short gather
        // clock and the sweep is what fills the rest.
        motes.push({
          x: r.left + (cx + 0.5) * cellW, y: r.top + (cy + 0.5) * cellH,
          dx: m.dx, dy: m.dy, mx: m.mx, my: m.my, r: speck.px / 2, s: m.scale, a: speck.alpha,
          delay: m.delay, dur: gather ? gatherMs : Math.max(MIN_TILE_MS, span - m.delay),
          // …and its own hash for the wobble, the twinkle and its place in the palette
          // (dustCloud.js turbulenceAt / dustMix).
          w: tileNoise(cx + 13, cy + 71), t: 1, g: speck.glint ? 1 : 0,
        });
      }
    }
    const kind = flightOf(toward, gather, flight);
    // Every cloud is painted in --accent / --accent-2, never in the surface's own colours:
    // each grain picks its stop by its mix (dustCloud.js drawCloud).
    const style = styleCode();
    const paints = paletteCss();
    host.__cloud = { motes, colours: paints, flight: kind, span, style };   // what a test reads
    // Appended to the element's own PARENT, not <body>: a row's cloud is torn down with
    // the list it belongs to. Still position:fixed, so viewport-anchored, clear of
    // scroller clipping. A SURFACE goes on <body> outright: its own parent (a modal
    // overlay) is about to go display:none under it.
    (toBody ? document.body : (el.parentElement || document.body)).appendChild(host);
    // …but only if the parent can actually host it: an ancestor with a transform,
    // filter or backdrop-filter becomes the containing block for position:fixed and can
    // re-anchor or clip the layer. Detected by measuring, not by guessing which
    // properties are in play — if the layer did not land where told, re-home on <body>.
    // Skipped for a SURFACE cloud: `toBody` already put it straight on <body>.
    if (!toBody) {
      const got = host.getBoundingClientRect();
      if (Math.abs(got.left - r.left) > 1 || Math.abs(got.top - r.top) > 1) {
        document.body.appendChild(host);
      }
    }
    // Colours resolved ONCE per cloud through one probe, after the host is in the
    // document (a `var(--…)` needs the page's own scope to mean anything).
    const probe = document.createElement('span');
    host.appendChild(probe);
    const fills = paints.map((css) => resolveColour(document, css, probe));
    probe.remove();
    startCloud(host, motes, { flight: kind, span, colours: fills, origin: { x: r.left, y: r.top }, style });
    // …and the layer goes one beat after the last mote lands (the flight ends AT the
    // span now, sweep included), not most of a second later.
    const life = setTimeout(() => {
      host.__stop?.();
      host.remove();
      if (el.__dustHost === host) { el.__dustHost = null; el.__dustTimer = null; }
    }, span + 150);
    if (own) { el.__dustHost = host; el.__dustTimer = life; }
    return true;
  } catch {
    return false;   // decoration only — the removal carries on regardless
  }
}

// ── Reintegration: the snap played backwards ────────────────────────────────
// The same tile layer as disintegrate, but every mote starts where the scatter would
// have flung it and flies HOME (tileGather in animations.css), with the sweep reversed
// so the first mote out is the last one in. Used by materialize below.
export const reintegrate = (el, opts = {}) => disintegrate(el, { ...opts, gather: true });

// ── Surfaces: a window, a panel and a mini popup are dust too ───────────────
// A modal, the chat panel and the ⋯/context popups play the SAME scatter a deleted
// row does — only every mote flies INTO (or out of) the point that owns the surface:
// the icon that opened it, the click a context menu grew from, the edge a docked
// panel slides off. Origin and direction are exactly what the old scale had; what
// changed is that the flight is rendered as particles instead of a moving rectangle.
// The way IN is the slower half on purpose: a window forming is the thing you watch,
// and it has to arrive gently enough to read as sand gathering rather than a flash.
// Going out is brisk — you have already decided.
export const SURFACE_IN_MS = 760;
export const SURFACE_OUT_MS = 470;   // ui/base.js CLOSE_MS rides this
// A MENU is not a window: it is opened to be clicked, often blind, so it may not spend
// half a second forming. Its own, brisker clock — the flight is the same one.
export const SURFACE_MENU_IN_MS = 420;
export const SURFACE_MENU_OUT_MS = 270;
// The grain a mote AIMS for, and the mote-budget ceiling. The ceiling was 672 when a
// mote was a compositor layer of its own; on one canvas (dustCloud.js) a grain costs a
// few arcs, so it now matches the extension's and the desktop's (kSurfaceMaxCells).
// The speck is still sized separately (SURFACE_SPECK_PX) — air between grains is sand.
export const SURFACE_MOTE_PX = 6;
export const SURFACE_COLS = 46;
export const SURFACE_ROWS = 30;      // 1380 motes
// …and past that ceiling the CELL is bigger than the grain we want, so the speck drawn
// inside it is capped instead of filling it. What you see is the speck, not the cell.
export const SURFACE_SPECK_PX = 7;
export const SURFACE_SPREAD = 34;    // how far a mote may fan off its line to the point
export const SURFACE_FORMING_CLASS = 'surface-forming';
export const SURFACE_LEAVING_CLASS = 'surface-leaving';
// Permanent once a surface has been dusted: its old CSS pop/slide must stay off for
// good, or removing the forming class at the end of the flight would replay it.
export const SURFACE_DRIVEN_CLASS = 'dust-driven';

// One mote's flight when a whole surface gathers into — or bursts out of — a single
// POINT. The path is the cell's own offset to that point, so every mote converges
// there instead of falling; the two decorrelated noises only fan the arrival. The
// delay rides the DISTANCE, so the edge nearest the point goes first and the far one
// last — a window draining into its icon, and pouring back out of it. Pure.
// `delayScale` halves that stagger for a SCATTER (disintegrate's own `gather: false`):
// "going out is brisk" was already the rule for the span, but the sweep itself still
// held every mote at its 0% pose (still, opaque — indistinguishable from the surface
// not having reacted yet) for up to 45% of it. On a big surface that reads as nothing
// moving at all until a sudden, late flick — a blink, not sand leaving.
export const surfaceMotion = (cx, cy, cols, rows, box, point,
                              { span = SURFACE_OUT_MS, spread = SURFACE_SPREAD, delayScale = 1 } = {}) => {
  const n = tileNoise(cx, cy);
  const m = tileNoise(cx + 41, cy + 17);
  const q = tileNoise(cx + 97, cy + 53);
  const w = (box?.width || 0) / Math.max(1, cols);
  const h = (box?.height || 0) / Math.max(1, rows);
  const homeX = (box?.left || 0) + (cx + 0.5) * w;
  const homeY = (box?.top || 0) + (cy + 0.5) * h;
  const toX = (point?.x || 0) - homeX;
  const toY = (point?.y || 0) - homeY;
  // Normalised against the longest trip any cell in this box makes, so the sweep fills
  // the whole flight whatever the point's distance is.
  const far = Math.hypot(box?.width || 0, box?.height || 0) + Math.hypot(toX, toY);
  const progress = far > 0 ? Math.min(1, Math.hypot(toX, toY) / far) : 0;
  const dx = Math.round(toX + (m - 0.5) * spread);
  const dy = Math.round(toY + (n - 0.5) * spread);
  return {
    delay: Math.round((progress * span * 0.45 + n * span * 0.12) * delayScale),
    dx, dy,
    ...tileWaypoint(dx, dy, q),
    rot: +((m - 0.5) * 60).toFixed(2),
    scale: +(0.12 + n * 0.25).toFixed(2),
  };
};

// Where a DOCKED panel's dust comes from, and goes back to: far out past the edge it
// is docked to, so the motes stream along the panel's own slide direction — the same
// "where it comes from" the slide had. A float has an icon instead, so: null. `reach`
// is measured from the panel's own CENTRE, not its edge — 1.7 lands the point 1.2
// panel-widths past the edge itself, matching the desktop overlay's own reachPx
// (mainWindow.cpp chatSurfaceFlight: `picture.width() * 1.2`). Pure.
export const dockAwayPoint = (rect, dock, reach = 1.7) => {
  if (!rect) return null;
  const cx = rect.left + rect.width / 2;
  const cy = rect.top + rect.height / 2;
  if (dock === 'left') return { x: cx - rect.width * reach, y: cy };
  if (dock === 'right') return { x: cx + rect.width * reach, y: cy };
  if (dock === 'top') return { x: cx, y: cy - rect.height * reach };
  if (dock === 'bottom') return { x: cx, y: cy + rect.height * reach };
  return null;
};

// Is `c` a colour that paints nothing? An unset background, or a fully transparent one.
const blankPaint = (c) => !c || c === 'transparent' || /,\s*0\s*\)$/.test(c);

// How far a mote is lifted off the surface's own background, towards its own ink: the
// body of the cloud, and its rim. See surfacePaint.
export const MOTE_INK = 42;
export const MOTE_RIM_INK = 66;

// What the motes are PAINTED in. The element's own background, else the nearest
// ancestor that actually paints one — a surface whose box is transparent (a panel that
// leaves the colour to a child) would otherwise dust in a fallback nobody chose.
//
// …and then LIFTED towards that surface's own ink, because the background alone is
// invisible: a window and the page under it are the same family of colour, so a cloud
// painted in the window's exact background dissolved into nothing at all. Mixing in the
// ink keeps every mote the window's own colour and gives it something to read against,
// and it flips with the theme for free — dark surfaces lighten, light ones darken,
// because ink always contrasts with the background it is written on.
const surfacePaint = (el, inkOverride = null) => {
  const get = typeof getComputedStyle === 'function' ? getComputedStyle : null;
  const own = get ? get(el) : null;
  let bg = '';
  for (let node = el; get && node && node.nodeType === 1 && !bg; node = node.parentElement)
    if (!blankPaint(get(node).backgroundColor)) bg = get(node).backgroundColor;
  bg = bg || 'var(--bg-container)';
  const ink = inkOverride || (own && !blankPaint(own.color) ? own.color : 'var(--text-main)');
  const grain = (pct) => `color-mix(in srgb, ${bg} ${pct}%, ${ink})`;
  // An element with `border-style: none` still COMPUTES a border colour, and its
  // initial value is `currentColor` — the TEXT colour. Reading it unguarded painted
  // every rim mote near-white on a dark theme, whatever the theme actually was; a real
  // border is used as drawn, and without one the rim is simply a stronger grain, so the
  // cloud keeps the window's outline for its first frames either way.
  const bordered = own && own.borderTopStyle !== 'none' && parseFloat(own.borderTopWidth) > 0;
  return { fill: grain(100 - MOTE_INK), edge: bordered ? own.borderTopColor : grain(100 - MOTE_RIM_INK) };
};

// The same walk, for a MARK: its own text colour is full contrast (near-black in
// light, near-white in dark) and read as a hard white/black fleck. --text-muted instead.
const markPaint = (el) => surfacePaint(el, 'var(--text-muted)');

// Round speck in the surface's own colours; the rim cells take its border instead, so
// the cloud keeps the window's outline for the first frames, and a few inner grains
// take the rim's stronger tone too, so the field glints rather than reading flat. The
// speck is a GRAIN, not the cell it sits in: past the mote budget a cell can be several
// times the grain we want, and a cell-filling square is the "huge rectangles" a
// scatter must never show.
//
// A painter answers per cell with the grain's colour, its own opacity and its size;
// disintegrate seats it at the cell's centre and dustCloud.js draws it.
// `override` is for a mark whose colour is NOT on the box when the flight runs: a
// checkbox that has just been UNticked no longer paints anything accent, so reading the
// live element would dust the toolbar's own grey. The caller passes what left instead.
const speckPainter = (el, override = null) => {
  const { fill, edge } = override || surfacePaint(el);
  return ({ cx, cy, cols, rows, cellW, cellH }) => {
    const n = tileNoise(cx, cy);
    const rim = cx === 0 || cy === 0 || cx === cols - 1 || cy === rows - 1 || n > 0.86;
    // Grains of ONE size read as a mosaic; the spread is what makes it sand…
    const grain = Math.min(cellW, cellH, SURFACE_SPECK_PX);
    // …never faint: a mote you can barely see is a flight you cannot follow.
    return { color: rim ? edge : fill, alpha: 0.78 + n * 0.22, px: grain * (0.62 + n * 0.5), glint: rim };
  };
};

// A GROUP of controls dusts in its controls' own colours — the desktop's groupShot
// (controlReveal.hpp) without a screenshot: every descendant that paints a background
// claims the cells under its box, innermost winning, and cells over nothing take the
// group's own recipe. One flat field over a purple and a red button read as grey haze
// (user report). Measured NOW, while the group is still laid out.
const groupPainter = (el) => {
  const base = speckPainter(el, markPaint(el));
  const get = typeof getComputedStyle === 'function' ? getComputedStyle : null;
  if (!get || !el?.getBoundingClientRect || !el.querySelectorAll) return base;
  const root = el.getBoundingClientRect();
  const parts = [];
  for (const child of el.querySelectorAll('*')) {
    const cs = get(child);
    if (blankPaint(cs.backgroundColor) || !child.getBoundingClientRect) continue;
    const r = child.getBoundingClientRect();
    if (r.width < 2 || r.height < 2) continue;
    const edge = cs.borderTopStyle !== 'none' && parseFloat(cs.borderTopWidth) > 0 && !blankPaint(cs.borderTopColor)
      ? cs.borderTopColor : cs.backgroundColor;
    parts.push({ l: r.left - root.left, t: r.top - root.top, r: r.right - root.left, b: r.bottom - root.top,
                 paint: speckPainter(el, { fill: cs.backgroundColor, edge }) });
  }
  if (!parts.length) return base;
  return (g) => {
    const x = (g.cx + 0.5) * g.cellW;
    const y = (g.cy + 0.5) * g.cellH;
    for (let i = parts.length - 1; i >= 0; i--) {   // last in document order = innermost
      const p = parts[i];
      if (x >= p.l && x < p.r && y >= p.t && y < p.b) return p.paint(g);
    }
    return base(g);
  };
};

// A surface NEVER dusts as clones of itself, however small it is. A row scatter can
// afford to (a list row is one element in one place), but a surface's cloud lands on
// <body> — and a cloud of a few hundred copies of a menu is a few hundred more elements
// answering to `.accent-dd-menu`, `.ctx-sub`, `#chat-…`. Everything that queries the
// page — the app's own code, and every test that drives it — would have to know about a
// decoration. Flat specks in the surface's own colours carry no identity at all, and at
// a 6px grain that is very nearly all a clone would have shown anyway.
const surfaceDust = (el, point, { ms, gather, px = SURFACE_MOTE_PX, paint = null, box = null,
                                 delayScale = null }) => {
  if (typeof document === 'undefined' || !el?.getBoundingClientRect) return false;
  if (!(Number.isFinite(point?.x) && Number.isFinite(point?.y))) return false;
  const r = box || el.getBoundingClientRect();
  if (!(r.width >= 8 && r.height >= 8)) return false;
  const grid = reshapeGrid(SURFACE_COLS, SURFACE_ROWS, r.width, r.height, px);
  return disintegrate(el, {
    ...grid, gather, toward: point, ms, px, toBody: true, box, delayScale,
    hostClass: gather ? 'dust-forming' : 'dust-leaving',
    paintTile: speckPainter(el, paint),
  });
};

// Drop whatever a surface has in flight — the cloud AND the classes driving its own
// opacity — leaving the end state untouched. Every open/close begins here, so a
// double-clicked menu or a swept-past modal always converges on the true state.
export function settleSurface(el) {
  if (!el?.classList) return;
  if (typeof clearTimeout === 'function') clearTimeout(el.__surfaceTimer);
  el.__surfaceTimer = null;
  el.classList.remove(SURFACE_FORMING_CLASS, SURFACE_LEAVING_CLASS);
  el.style?.removeProperty?.('--dust-ms');
  cancelDust(el);
}

const playSurface = (el, point, { ms, gather, box = null, delayScale = null }) => {
  if (!el?.classList) return false;
  settleSurface(el);
  if (motionReduced()) return false;
  // No point to fly at ⇒ exactly settleSurface (already ran): callers never need their
  // own `if (!point) settleSurface(el)` fallback around surfaceIn/surfaceOut.
  if (!(Number.isFinite(point?.x) && Number.isFinite(point?.y))) return false;
  // The marker goes on BEFORE the measure: the element's own entrance (modalFromIcon,
  // chatSlide*, menuPop) FILLS an icon-sized from-state, so a box read under it is the
  // icon's box — the same trap `modal-measuring` dodges in ui/base.js. Off again if the
  // dust declines, so a surface that never plays it keeps its old CSS entrance.
  el.classList.add(SURFACE_DRIVEN_CLASS);
  if (!surfaceDust(el, point, { ms, gather, box, delayScale })) {
    el.classList.remove(SURFACE_DRIVEN_CLASS);
    return false;
  }
  el.style?.setProperty?.('--dust-ms', `${ms}ms`);
  el.classList.add(gather ? SURFACE_FORMING_CLASS : SURFACE_LEAVING_CLASS);
  el.__surfaceTimer = setTimeout(() => {
    el.__surfaceTimer = null;
    el.classList.remove(SURFACE_FORMING_CLASS, SURFACE_LEAVING_CLASS);
    el.style?.removeProperty?.('--dust-ms');
  }, ms + 60);
  return true;
};

// The surface waits behind its own dust and fades up as the last motes land.
export const surfaceIn = (el, point, { ms = SURFACE_IN_MS, box = null, delayScale = null } = {}) =>
  playSurface(el, point, { ms, gather: true, box, delayScale });
// …and hands over to it at once on the way out. The caller still owns the real
// hide/remove: like leaveThenRemove, the end state never depends on the animation.
// `delayScale` compresses the per-mote stagger — the default sweep makes a window come
// apart in a wave, but on a small surface it is just a thin tail of stragglers.
export const surfaceOut = (el, point, { ms = SURFACE_OUT_MS, box = null, delayScale = null } = {}) =>
  playSurface(el, point, { ms, gather: false, box, delayScale });

// ── Hover tips / preview popups: one shared clock and origin ────────────────
// Every cursor-adjacent popup (the control tooltip, the Alt-hover export preview, the
// chat gear tip, the projects thumb zoom) dusts in and out of the control it describes,
// fast enough to be over before a sweep reaches the next one.
export const TIP_DUST_IN_MS = 320;
export const TIP_DUST_OUT_MS = 235;
// …and wakes on one delay across surfaces (desktop SnappyTooltipStyle, main.cpp).
export const TIP_SHOW_DELAY_MS = 200;

// The centre of an element (or of a rect): the point a popup's dust belongs to.
// Null for a detached/unmeasurable owner — the flight then settles instead.
export const rectCenter = (elOrRect) => {
  const r = typeof elOrRect?.getBoundingClientRect === 'function'
    ? elOrRect.getBoundingClientRect() : elOrRect;
  if (!r || !(r.width > 0 || r.height > 0)) return null;
  return { x: r.left + r.width / 2, y: r.top + r.height / 2 };
};

// ── Folding surfaces: the box one is ABOUT to take ─────────────────────────
// A folding surface (tool rows, points panel) is still at its collapsed box when its
// toggle flips, so this measures the box it will take: run the target state with the
// fold's transitions off (`instant`), read the rect, revert. `scope` carries the state
// class, `el` is measured. Null when there is nothing worth dusting.
export function foldBox(el, scope, cls, on, instant) {
  if (!el?.getBoundingClientRect || !scope?.classList) return null;
  const had = scope.classList.contains(cls);
  scope.classList.add(instant);
  scope.classList.toggle(cls, on);
  const r = el.getBoundingClientRect();
  scope.classList.toggle(cls, had);
  el.getBoundingClientRect();   // flush the revert while the transitions are still off
  scope.classList.remove(instant);
  return r.width >= 8 && r.height >= 8
    ? { left: r.left, top: r.top, width: r.width, height: r.height } : null;
}
// The class that switches those transitions off for the read (css/animations.css).
export const FOLD_INSTANT_CLASS = 'fold-instant';
// A fold COLLAPSING is the slower half — the opposite of every other surface, and the
// reason it has its own exit clock: with no icon to shrink into, the fold itself is the
// only thing that reads as the menu leaving, so a brisk exit registered as a snap.
// 1.5x SURFACE_OUT_MS, matching --fold-out-ms against --fold-ms in css/animations.css.
export const FOLD_DUST_OUT_MS = 705;   // 1.5x SURFACE_OUT_MS, as the CSS fold's --fold-out-ms is of --fold-ms

// The whole fold-with-dust ritual (toolbar rows, points panel): measure the SHOWN box
// before the fold runs (foldBox), let `toggle` flip the fold class, then stream the
// sand past the `dock` edge — collapsing rides the fold's own slower clock.
export function foldDust(el, scope, cls, hiding, dock, { inMs = SURFACE_IN_MS, toggle = null } = {}) {
  const box = motionReduced() ? null : foldBox(el, scope, cls, false, FOLD_INSTANT_CLASS);
  toggle?.();
  const away = box && dockAwayPoint(box, dock);
  (hiding ? surfaceOut : surfaceIn)(el, away || null, { box, ms: hiding ? FOLD_DUST_OUT_MS : inMs });
}

// ── Hover popups: the two whose visibility is pure CSS ─────────────────────
// The toolbar's hints bubble and the install menu are shown by a `:hover` rule alone,
// with no JS open/close to hang a flight off. They are surfaces all the same, so they
// take the same sand — the only difference is the EXIT: by the time pointerleave runs,
// `:hover` is gone and the popup is already display:none, so there is nothing left to
// copy. The hold class puts the box back for exactly as long as the motes need.
export const HOVER_DUST_HOLD_CLASS = 'dust-hold';
export const HOVER_DUST_HOLD_MS = 120;
export function wireHoverDust(host, popup, { inMs = 300, outMs = 200 } = {}) {
  if (!host?.addEventListener || !popup?.classList) return;
  const point = () => (motionReduced() ? null : rectCenter(host));
  host.addEventListener('pointerenter', () => surfaceIn(popup, point(), { ms: inMs }));
  host.addEventListener('pointerleave', () => {
    const p = point();
    if (!p) { settleSurface(popup); return; }
    popup.classList.add(HOVER_DUST_HOLD_CLASS);
    const played = surfaceOut(popup, p, { ms: outMs });
    setTimeout(() => popup.classList.remove(HOVER_DUST_HOLD_CLASS), played ? HOVER_DUST_HOLD_MS : 0);
  });
}

// ── A control's own MARK: the same sand, at control scale ───────────────────
// A tick, a select's chosen word, a toggle-revealed row: marks come and go inside a
// control whose box stays put. A row's fall-and-fan flight (tileMotion; desktop
// controlSwap.hpp parity) with the throw and grain scaled down — and no default veil,
// since a checkbox keeps its outline while only its fill goes.
export const MARK_IN_MS = 400;
export const MARK_OUT_MS = 300;
export const MARK_MOTE_PX = 3;
export const MARK_DRIFT = 0.3;        // desktop kCheckSwapSpread
// …under a ceiling of its own, well below a window's: a 15px indicator wants every mote
// the grain gives it (25 of them), but the f(x,y) row is 380px wide and gridded at the
// same 3px would have built over 1300 nodes for a 320ms decoration. Past the ceiling the
// cell grows and the speck grows with it, which is what keeps the grain honest.
export const MARK_COLS = 40;
export const MARK_ROWS = 15;          // 600 motes
export const MARK_FORMING_CLASS = 'mark-forming';
// …and the veil a GROUP goes behind: hidden while its dust flies and its slot closes,
// so the buttons are never seen squeezing shut.
export const MARK_LEAVING_CLASS = 'mark-leaving';

// `painter` is a ready per-cell painter (a group's, see groupPainter); `paint` a colour
// recipe for the default speck painter. The painter wins.
const markDust = (el, { gather, ms, paint, px, drift, own = true, painter = null }) => {
  if (typeof document === 'undefined' || !el?.getBoundingClientRect) return false;
  if (motionReduced()) return false;
  const r = el.getBoundingClientRect();
  if (!(r.width >= 8 && r.height >= 8)) return false;
  const grid = reshapeGrid(MARK_COLS, MARK_ROWS, r.width, r.height, px);
  return disintegrate(el, {
    ...grid, gather, ms, px, drift, toBody: true, own,
    // Visible from the first frame either way — a mark's motes ARE the mark. Arrival is
    // the surface gather; departure its own fall (dustCloud.js FLIGHTS.fall — the
    // desktop's Sweep::Fall). The surface leave was tried and read as a flick (user report).
    hostClass: gather ? 'dust-forming' : 'dust-falling',
    flight: gather ? 'surfaceGather' : 'fall',
    paintTile: painter || speckPainter(el, paint || markPaint(el)),
  });
};

// Hold a veil on `el` for the length of a flight, then lift it (markIn / markOut).
const holdMarkVeil = (el, veil, ms) => {
  el.__markVeil = veil;
  el.classList.add(veil);
  el.style?.setProperty?.('--mark-ms', `${ms}ms`);
  if (typeof setTimeout === 'function')
    el.__markTimer = setTimeout(() => {
      el.__markTimer = null;
      el.classList.remove(veil);
      el.__markVeil = null;
      el.style?.removeProperty?.('--mark-ms');
    }, ms + 40);
};

// Drop whatever `el` has in flight, veil included, leaving the end state untouched.
export function settleMark(el) {
  if (!el?.classList) return;
  if (typeof clearTimeout === 'function') clearTimeout(el.__markTimer);
  el.__markTimer = null;
  if (el.__markVeil) el.classList.remove(el.__markVeil);
  el.__markVeil = null;
  el.style?.removeProperty?.('--mark-ms');
  cancelDust(el);
}

// The mark comes apart. The caller still owns the real state change — like surfaceOut,
// the cloud is a copy on <body> and the end state never waits for it.
// `veil` (opt-in: a group leaving under revealControls) hides the real thing the moment
// its motes set off, so the dust IS it going. A bare mark keeps its box visible.
export function markOut(el, { ms = MARK_OUT_MS, paint = null, px = MARK_MOTE_PX,
                              drift = MARK_DRIFT, own = true, painter = null, veil = null } = {}) {
  if (own) settleMark(el);
  const played = markDust(el, { gather: false, ms, paint, px, drift, own, painter });
  if (played && veil && el.classList) holdMarkVeil(el, veil, ms);
  return played;
}

// …and forms. `veil` is the class that holds the real mark back while the motes gather
// (they ARE the mark forming); null for a mark that is already invisible on its own.
export function markIn(el, { ms = MARK_IN_MS, paint = null, px = MARK_MOTE_PX,
                             drift = MARK_DRIFT, veil = MARK_FORMING_CLASS, painter = null } = {}) {
  settleMark(el);
  const played = markDust(el, { gather: true, ms, paint, px, drift, painter });
  if (!played || !veil || !el.classList) return played;
  holdMarkVeil(el, veil, ms);
  return true;
}

// Show or hide a group of controls another control governs — the f(x,y) inputs, the
// custom page's W/H boxes — with that sand, while the space it takes in the row also
// opens or closes smoothly, so neighbours don't jump the instant it toggles.
// `display: 'block'` collapses HEIGHT instead of width; everything else is a flex row.
// A group's slot is a wider move than a single mark and reads as a snap at the mark's
// clock, so it gets its own longer one — handed to the dust too, so the two land together.
export const REVEAL_GROUP_IN_MS = 520;
export const REVEAL_GROUP_OUT_MS = 400;

// A BAR holding revealed controls (the selection strips): the BAR ITSELF never flies —
// only its controls do, so this is a display flip, deferred on the way OUT by their
// flight (the desktop's ProjectsDialog / ConnectDialog::updateBatchBar). Sliding its own
// slot was tried and read wrong both ways: opening it clipped the button forming inside
// it, closing it left the border and padding as a bare grey line (user report).
// `want()` is the single source of whether the bar belongs — asked now, and again on
// arrival, so a selection made mid-flight keeps it. Timer injectable — unit-tested.
// The class a leaving bar wears while its SLOT closes: height, padding and the divider
// under it all go together, or whatever is left of its footprint drops the list below it
// in one frame at the end.
export const BAR_CLOSING_CLASS = 'bar-closing';
// …and the freeze it wears first. Its controls fly out before the slot closes, and the
// last one being hidden takes the strip's content height with it — that collapse IS the
// jump, before any slide could start (user report: the pinned row jumped when Select all
// left). Held at its measured height, the strip keeps its shape while they leave.
export const BAR_HELD_CLASS = 'bar-held';

const releaseBarSlot = (el) => {
  el.classList?.remove(BAR_CLOSING_CLASS, BAR_HELD_CLASS);
  el.style.removeProperty?.('--bar-h');
  el.style.removeProperty?.('--reveal-ms');
};

// Close the bar's slot from the height it is holding, then take it out of the flow.
export const closeBarSlot = (el, ms, { setTimer = setTimeout } = {}) => {
  const h = parseFloat(el.style.getPropertyValue?.('--bar-h'))
    || el.getBoundingClientRect?.().height || 0;
  if (!h || motionReduced()) { releaseBarSlot(el); el.style.display = 'none'; return false; }
  el.style.setProperty('--reveal-ms', `${ms}ms`);
  el.style.setProperty('--bar-h', `${h}px`);
  el.classList.add(BAR_HELD_CLASS);
  void el.offsetWidth;                    // commit the held height as the start
  el.classList.add(BAR_CLOSING_CLASS);    // …and everything it owns goes to zero
  setTimer(() => { el.style.display = 'none'; releaseBarSlot(el); }, ms);
  return true;
};

export const revealBar = (el, want, { display = 'flex', ms = 0, setTimer = setTimeout } = {}) => {
  if (!el?.style) return false;
  const shown = el.style.display !== 'none';
  if (want()) {
    releaseBarSlot(el);   // asked back mid-close: give its own height back first
    if (!shown) el.style.display = display;   // at once: the slot the controls fly INTO
    return !shown;
  }
  if (!shown) return false;
  if (motionReduced()) { el.style.display = 'none'; return false; }
  const out = ms || REVEAL_GROUP_OUT_MS;
  // Freeze the footprint NOW, before the controls inside start leaving.
  const held = el.getBoundingClientRect?.().height || 0;
  if (held) { el.style.setProperty?.('--bar-h', `${held}px`); el.classList?.add(BAR_HELD_CLASS); }
  setTimer(() => {
    if (want()) { releaseBarSlot(el); return; }   // wanted again mid-wait: it stays
    closeBarSlot(el, out, { setTimer });
  }, out);
  return false;
};
// A transition, not @keyframes: markIn/markOut may also add `.mark-forming`
// (an animation), and two `animation` rules on one element would fight over a winner.
const REVEAL_GROUP_TRANSITION_CLASS = 'reveal-group-transition';

// One slide of the space a revealed group reserves: commit `from` as the transition's
// start, apply `to`, clean up after `ms` (+`slack`). `defer` waits two painted frames
// before `to` — set in the same busy turn, the box leapt to wherever the curve already was.
// `ease` is the slot's own curve: the app's usual cubic-bezier(0.16, 1, .3, 1) is half done
// in 30ms, which makes a whole group's slot jump open and then crawl. Opening rides
// easeOutCubic, closing the gentle S the modal flight closes on.
const REVEAL_EASE_IN = 'cubic-bezier(0.22, 0.61, 0.36, 1)';
const REVEAL_EASE_OUT = 'cubic-bezier(0.45, 0.05, 0.6, 0.9)';
const slideRevealSize = (el, sizeProp, from, to, ms, { defer = false, slack = 0, cleanup = null, ease = REVEAL_EASE_IN } = {}) => {
  el.classList.add(REVEAL_GROUP_TRANSITION_CLASS);
  el.style.setProperty('--reveal-ease', ease);
  el.style[sizeProp] = from;
  void el.offsetWidth;   // commit FROM as the transition's start value
  const go = () => {
    el.style.setProperty('--reveal-ms', `${ms}ms`);
    el.style[sizeProp] = to;
    setTimeout(() => {
      cleanup?.();
      el.classList.remove(REVEAL_GROUP_TRANSITION_CLASS);
      el.style[sizeProp] = '';
      el.style.removeProperty('--reveal-ms');
      el.style.removeProperty('--reveal-ease');
    }, ms + slack);
  };
  if (!defer) { go(); return; }
  const raf = typeof requestAnimationFrame === 'function'
    ? requestAnimationFrame
    : (fn) => setTimeout(fn, 16);
  raf(() => raf(go));
};

// Keep a group's cloud anchored to the group for the length of its flight — the desktop's
// DisintegrateOverlay::setFollow. A control revealed beside a sibling is photographed
// where it sits, and the sibling's slot then pushes it along the row: the motes gathered
// where the group first stood and jumped over on landing (user report). Left/top only —
// the box they fly at is the natural one while the slot itself is mid-slide. Stops with
// the host, or on display:none (an all-zero rect).
const followDust = (el, ms) => {
  if (typeof requestAnimationFrame !== 'function' || !el.getBoundingClientRect) return;
  const started = Date.now();
  const step = () => {
    const host = el.__dustHost;
    if (!host || Date.now() - started >= ms) return;
    const r = el.getBoundingClientRect();
    if (!r || (!r.width && !r.height)) return;
    host.style.left = `${r.left}px`;
    host.style.top = `${r.top}px`;
    requestAnimationFrame(step);
  };
  requestAnimationFrame(step);
};

// `dust: false` slides the slot without a cloud — for a wide, mostly EMPTY element,
// whose motes are a grey band the width of the window rather than anything the eye can
// follow (user report). Its CONTENTS still dust. Desktop twin: revealControls' `dust`.
// `ms` overrides the slot's own clock, for a group that must land together with something
// else — the connections bar leaves beside the row that emptied it.
export function revealControls(el, show, display = 'inline-flex',
                               { vertical: axis = null, dust = true, ms = 0 } = {}) {
  const inMs = ms || REVEAL_GROUP_IN_MS;
  const outMs = ms || REVEAL_GROUP_OUT_MS;
  if (!el?.style) return false;
  const wasShown = el.style.display !== 'none';
  if (wasShown === !!show) return false;   // already there: nothing comes or goes
  // Which way the slot closes: a block collapses its height, an inline group its width.
  // Right for every caller but a full-width bar, which is a flex row that must still open
  // downward — hence the explicit override (connectModal.js's selection bar).
  const vertical = axis === null ? display === 'block' : !!axis;
  const sizeProp = vertical ? 'maxHeight' : 'maxWidth';
  if (show) {
    el.style.display = display;
    const r = el.getBoundingClientRect();   // now laid out at its natural size
    const size = vertical ? r.height : r.width;
    // grid sized off that same natural box, painted in the group's own colours
    const played = dust ? markIn(el, { ms: inMs, painter: groupPainter(el) }) : !motionReduced();
    if (size && played)
      slideRevealSize(el, sizeProp, '0px', `${size}px`, inMs, { defer: true, slack: 40 });
    if (dust && played) followDust(el, inMs);
    return played;
  }
  // Measured while it is still laid out, so both the dust and the collapse start
  // from the true box.
  const r = el.getBoundingClientRect();
  const size = vertical ? r.height : r.width;
  const played = dust
    ? markOut(el, { ms: outMs, painter: groupPainter(el), veil: MARK_LEAVING_CLASS })
    : !motionReduced();
  if (size && played) {
    slideRevealSize(el, sizeProp, `${size}px`, '0px', outMs,
      { ease: REVEAL_EASE_OUT, cleanup: () => { el.style.display = 'none'; } });
  } else {
    el.style.display = 'none';   // declined (reduced motion, too small): instant, as before
  }
  if (dust && played) followDust(el, outMs);
  return played;
}

// Swap the TEXT a control displays, the mark coming apart and the new one forming out
// of the motes. `apply` writes the new value; it runs between the two flights, so the
// element is never blank and the DOM is never behind the state.
export function markSwap(el, apply, { ms = MARK_IN_MS, outMs = MARK_OUT_MS, paint = null } = {}) {
  if (typeof apply !== 'function') return false;
  // The outgoing cloud is deliberately UNOWNED: the arrival below is the flight this
  // element owns, and claiming both would have the second cancel the first.
  const left = markOut(el, { ms: outMs, paint, own: false });
  apply();
  return markIn(el, { ms, paint }) || left;
}

// ── Materialize: leaveThenRemove reversed, for a freshly-ADDED row ──────────
// Call on the new row right after the render that inserted it: its box expands on the
// short timer while a dust copy gathers over the full wipe; the row stays veiled until
// the motes land (the dust IS the row forming). Resolves once the veil lifts.
export const MATERIALIZE_CLASS = 'materializing';
export const MATERIALIZE_VEIL_CLASS = 'materialize-veil';
export const MATERIALIZE_LIFT_CLASS = 'materialize-lift';   // the veil on its way up
// `drift` scales the throw the motes gather FROM: a whole row's default carries them
// most of a hundred pixels, which reads as sand arriving from somewhere else rather than
// the row forming (user report). A caller whose item is short says so.
export function materialize(el, { ms = LEAVE_MS, cols, rows, dustMs = FILTER_DUST_MS,
                                  drift = FILTER_DUST_DRIFT, px = 0 } = {}) {
  const reduced = motionReduced();
  if (!el?.classList || reduced || typeof setTimeout === 'undefined') return Promise.resolve();
  // Freeze the natural height (the row is already laid out) so the expansion has
  // something to animate to — the same trick the leave plays with --leave-h.
  if (el.getBoundingClientRect) {
    const r = el.getBoundingClientRect();
    if (r.height) el.style.setProperty('--enter-h', `${r.height}px`);
  }
  // The dust is filterDust's recipe — the surface gather (visible from the first frame,
  // eased out), on half a row's throw and the filter's short clock. The row gather
  // (reintegrate) was tried here and put the row inside a cloud bigger than itself.
  // cols === 0 is the budget's "fade only" (scatterGridFor past SCATTER_MAX_ROWS).
  const dusted = cols !== 0 && disintegrate(el, {
    ...(cols ? { cols } : {}), ...(rows ? { rows } : {}), ...(px ? { px } : {}),
    gather: true, ms: dustMs, drift, toBody: true, hostClass: 'dust-forming',
    paintTile: speckPainter(el),
  });
  el.classList.add(MATERIALIZE_CLASS);
  if (dusted) {
    // The veil LIFTS as the motes land, not after them: held at nothing until the first
    // are home (the gather leg), then up to full by the last. A hard veil dropped at the
    // end left a hole — dust gone, nothing, then the row (user report).
    el.classList.add(MATERIALIZE_VEIL_CLASS);
    const lift = Math.round(dustMs * TILE_GATHER_SHARE);
    el.style?.setProperty?.('--veil-fade', `${Math.max(1, dustMs - lift)}ms`);
    setTimeout(() => el.classList.add(MATERIALIZE_LIFT_CLASS), lift);
  }
  return new Promise((resolve) => setTimeout(() => {
    el.classList.remove(MATERIALIZE_CLASS, MATERIALIZE_VEIL_CLASS, MATERIALIZE_LIFT_CLASS);
    el.style?.removeProperty?.('--veil-fade');
    resolve();
  }, dusted ? wipeDurationMs(dustMs) : ms));
}

// ── A chat entry ARRIVES as dust, from its own side ─────────────────────────
// An arrival is a toast arriving (notifications.js): the same speck cloud, gathered out
// of a point off the edge the entry belongs to — the user's messages from the right, the
// assistant's from the left (the LEAVE is still a scatter around the row). The entry is
// held back for the whole flight: the motes ARE it forming, so fading it up underneath
// them would show the message first and the animation after.
// On a short clock of its own: the motes carry no text, so a long answer is unreadable
// until the veil lifts. A fixed FRACTION of the row's flight (520 of the old 900), so
// shortening DISINTEGRATE_MS shortens this with it rather than letting the two meet.
export const CHAT_ENTER_MS = Math.round(ITEM_DUST_MS * 0.58);
export const CHAT_ENTERING_CLASS = 'chat-entering';
// How far off the row's own edge its motes are gathered from. The cloud is clipped to
// the transcript (clipDustToScroller), so a point outside it simply means the sand
// streams in over the edge — exactly what a toast does off the window's.
export const CHAT_ENTER_REACH = 0.9;

// The point an arriving entry's dust flies out of: the edge it sits against, read off the
// geometry rather than the role class, so an attachment strip or result card follows the
// message it rides with. Null when unmeasurable — the caller then settles the entry.
export const chatArrivalPoint = (el, r = null, s = null) => {
  r = r || el?.getBoundingClientRect?.();
  s = s || el?.parentElement?.getBoundingClientRect?.();
  if (!r || !s || !(r.width > 0)) return null;
  // Hugging the scroller's right edge more closely than its left ⇒ the user's side.
  return dockAwayPoint(r, (s.right - r.right) <= (r.left - s.left) ? 'right' : 'left',
                       CHAT_ENTER_REACH);
};

// Two frames, so the measure below happens on a SETTLED transcript: frame one is the new
// entries' own layout, frame two is the scroll that follows it (chatView stickToBottom
// pins on a rAF). No rAF (node) ⇒ a macrotask, which is still after the caller returns —
// measuring synchronously would read the pre-scroll box every time.
const afterLayout = (fn) => (typeof requestAnimationFrame === 'function'
  ? requestAnimationFrame(() => requestAnimationFrame(fn))
  : setTimeout(fn, 0));

// Is `el` a whole entry sitting inside its scroller right now? The cloud is
// position:fixed, so the transcript does NOT clip it: an entry still below the fold
// would scatter its motes over the composer under it. Taller than the scroller ⇒ no
// dust either, for the same reason. Pure enough to unit-test.
// `r`/`s` are optional pre-measured rects (trackDust reads each box once per tick).
export const dustFitsScroller = (el, scroller = el?.parentElement, r = null, s = null) => {
  if (!el?.getBoundingClientRect || !scroller?.getBoundingClientRect) return false;
  r = r || el.getBoundingClientRect();
  s = s || scroller.getBoundingClientRect();
  if (!(r.width > 0 && r.height > 0)) return false;
  return r.top >= s.top - 1 && r.bottom <= s.bottom + 1;
};

// Confine a flying cloud to its SCROLLER: the tiles translate freely out of an
// `overflow: visible` host, so a gather next to the input rained motes across it
// (reported). Negative insets EXPAND, so motes roam the transcript, never outside it.
const clipDustToScroller = (el, scroller = el?.parentElement, r = null, s = null) => {
  const host = el?.__dustHost;
  if (!host || !scroller?.getBoundingClientRect || !el.getBoundingClientRect) return;
  r = r || el.getBoundingClientRect();
  s = s || scroller.getBoundingClientRect();
  const px = (n) => `${Math.round(n)}px`;
  host.style.clipPath =
    `inset(${px(s.top - r.top)} ${px(r.right - s.right)} ${px(r.bottom - s.bottom)} ${px(s.left - r.left)})`;
};

// Keep a flying cloud pinned to its entry until the motes land: the layer is
// position:fixed but the transcript scrolls under it, so it is re-anchored and
// re-clipped per frame — and dropped (with the entry handed over) if the entry leaves
// the scroller or resizes, since a stale fixed-size cloud would visibly lie about what
// lands. Returns a stop function.
const trackDust = (el, ms, onDrop = () => {}) => {
  if (typeof requestAnimationFrame !== 'function') return () => {};
  let raf = 0;
  let live = true;
  const started = Date.now();
  const shot = el.getBoundingClientRect?.();   // the box the cloud was photographed at
  const step = () => {
    if (!live) return;
    if (Date.now() - started >= ms) return;
    // BOTH rects read once per tick, up front, then handed to every check/helper —
    // the helpers' own reads interleaved with style writes forced a layout per frame.
    const r = el.getBoundingClientRect?.();
    const s = el.parentElement?.getBoundingClientRect?.();
    const resized = !r || !shot
      || Math.abs(r.width - shot.width) > 1 || Math.abs(r.height - shot.height) > 1;
    // Dropping the cloud must HAND THE ENTRY OVER in the same frame: the veil is lifted
    // by a timer at the end of the full flight, so a cancel that only killed the motes
    // left the message invisible with nothing standing in for it until that timer fired.
    if (!dustFitsScroller(el, el.parentElement, r, s) || resized) {
      cancelDust(el); live = false; onDrop(); return;
    }
    retargetDust(el, r);
    clipDustToScroller(el, el.parentElement, r, s);
    raf = requestAnimationFrame(step);
  };
  raf = requestAnimationFrame(step);
  return () => { live = false; if (raf) cancelAnimationFrame(raf); };
};

export const CHAT_SLIDE_CLASS = 'chat-slide-in';
export const CHAT_SLIDE_MS = 320;
export function chatIn(el, count = 1, index = 0) {
  if (!el?.classList || motionReduced() || typeof setTimeout === 'undefined') return Promise.resolve();
  // No particles ('slide'): the entry has no entrance of its own to fall back on — the
  // cloud WAS it — so it rises in instead (animations.css .chat-slide-in).
  if (!dustEnabled()) { flashLanding(el, CHAT_SLIDE_CLASS, CHAT_SLIDE_MS); return Promise.resolve(); }
  const { cols } = scatterGridFor(count, index);   // the burst's budget; the grid is surfaceDust's
  // Veiled from the FIRST frame, before anything is painted: the entry keeps its height
  // (so the transcript grows and scrolls to it as usual) but is never seen ahead of its
  // own motes. Lifted below the moment they land — or at once if none can fly.
  el.classList.add(CHAT_ENTERING_CLASS);
  const unveil = () => el.classList.remove(CHAT_ENTERING_CLASS);
  return new Promise((resolve) => {
    // Fonts first, when the platform offers the promise: a webfont landing after the
    // photograph re-wraps the entry and widens it, and the cloud is then visibly the
    // wrong size for what arrives. Already-loaded fonts resolve this in the same tick,
    // so only the very first arrival of a session ever waits on it.
    const ready = globalThis.document?.fonts?.ready;
    const go = () => afterLayout(() => {
      // cols === 0 is the budget's "fade only" (scatterGridFor past SCATTER_MAX_ROWS).
      // Past that guard the cloud is the toast's: it sizes its grid to the bubble and
      // paints specks, not clones, so nothing walking the transcript sees a live row.
      const flying = cols !== 0 && dustFitsScroller(el)
        && surfaceDust(el, chatArrivalPoint(el), { ms: CHAT_ENTER_MS, gather: true });
      if (!flying) { unveil(); resolve(); return; }
      clipDustToScroller(el);   // before the first frame paints, not after it
      let handedOver = false;
      const handOver = () => {
        if (handedOver) return;   // the cut happens once, whichever path gets there first
        handedOver = true;
        unveil();
        cancelDust(el);
        resolve();
      };
      const stop = trackDust(el, CHAT_ENTER_MS, handOver);
      setTimeout(() => {
        // The motes have landed, so the entry takes their place in the SAME frame the
        // cloud goes. Left to its own grace period the layer holds its finished state —
        // opaque, at identity — which is an exact second copy over the real entry.
        stop();
        handOver();
      }, CHAT_ENTER_MS);
    });
    if (ready?.then) ready.then(go, go); else go();
  });
}

// ── Ghosting a canvas out ───────────────────────────────────────────────────
// Clearing the canvas is synchronous, so the pixels are copied into a throwaway canvas
// laid over the real one and THAT is animated away. Call immediately BEFORE the clear;
// a failed ghost must never block the clear itself.
// Cut to 730 once for briskness and half again as long since, which lands it back near
// the 1100ms it started at: the picture is worth watching arrive and leave.
export const GHOST_MS = 1350;
// Dust motes are sized on SCREEN, not as a share of the image: a fixed grid over a
// big canvas gives big rectangles, which is what stopped it reading as dust.
export const DUST_CELL_PX = 6;
// Halving the cell quadruples the count, so the ceiling has to rise with it — but it
// still exists: past this, a frame costs more than the effect is worth and the grid is
// thinned back instead.
export const DUST_MAX_PARTICLES = 7000;

// The grid the ghost dust flies on: aim for `cellPx` per mote ON SCREEN (a water / fire
// ghost grids coarser), thinned evenly once the particle ceiling bites. Pure.
export const dustGrid = (w, h, cellPx = DUST_CELL_PX) => {
  let cols = Math.max(1, Math.round(w / cellPx));
  let rows = Math.max(1, Math.round(h / cellPx));
  while (cols * rows > DUST_MAX_PARTICLES) { cols = Math.ceil(cols / 1.1); rows = Math.ceil(rows / 1.1); }
  return { cols, rows };
};

// Clip the canvas box to the frame it is seen through. Zoom scales the canvas's CSS box
// while the viewport stays put, so gridding the WHOLE box let the particle ceiling thin
// a zoomed-in canvas back into big flakes — mote size followed the zoom level. Only the
// slice inside the frame is visible (the viewport scrolls the rest away), so only it
// plays, and the grid stays viewport-bounded at any zoom. Pure — unit-tested.
export const dustVisibleBox = (r, frame) => {
  const left = Math.max(r.left, frame.left), top = Math.max(r.top, frame.top);
  return { left, top,
           width: Math.min(r.left + r.width, frame.left + frame.width) - left,
           height: Math.min(r.top + r.height, frame.top + frame.height) - top };
};

// The canvas disintegrates as DUST, downward — a particle system on ONE canvas, not
// the DOM tiles a row uses: thousands of elements would not survive a frame. Each mote
// is a round grain in the colour of the pixels it came from (sampled once, dustParts);
// while a cell is still at home it is the picture itself — the visible slice is blitted
// whole each frame and only the departed cells are cleared out of it — so the front
// reads as the image grinding into grains of its own colour, not a dot screen popping
// over it. Landing is the same run backwards: a cell that has settled is the picture.
// When a mote's clock starts, as a fraction of the flight: OUT runs TOP-DOWN; IN is
// the same sweep REVERSED, so the mote that left first is the last one home. Pure.
export const dustDelay = (cy, rows, n, reverse = false) => {
  const progress = rows > 1 ? cy / (rows - 1) : 0;
  return (reverse ? 1 - progress : progress) * 0.55 + n * 0.12;
};

// Ease-out for the arrival: a mote covers most of the distance early and settles into
// place, rather than crawling the last few pixels. Pure.
export const dustEase = (k) => 1 - (1 - k) ** 3;

// Anchor to the VIEWPORT frame, not canvas.parentElement (.canvas-container): the
// container sizes itself to the canvas and re-centers via `margin: auto`, so zeroing
// canvas.width/height right after (the clear) collapses it to 0×0 and strands an
// absolutely-positioned child at its new, recentred spot. The viewport never shrinks.
const canvasDustHost = (canvas) => canvas.closest?.('.canvas-viewport') || canvas.parentElement;

// Measure where the dust plays: the visible slice of the canvas, plus the stage's
// offsets inside the host. clientLeft/Top step over the host's border, and the scroll
// offsets are added because an absolutely-positioned stage scrolls WITH the content —
// without them a panned, zoomed-in canvas got its cloud a scroll-offset away.
const dustField = (canvas) => {
  const r = canvas.getBoundingClientRect();
  const host = canvasDustHost(canvas);
  const hr = host.getBoundingClientRect();
  const frame = { left: hr.left + (host.clientLeft || 0), top: hr.top + (host.clientTop || 0),
                  width: host.clientWidth || hr.width, height: host.clientHeight || hr.height };
  const vis = dustVisibleBox(r, frame);
  if (vis.width < 8 || vis.height < 8) return null;
  return { r, host, vis,
           left: vis.left - frame.left + (host.scrollLeft || 0),
           top: vis.top - frame.top + (host.scrollTop || 0) };
};

// Keep a flying stage over the frame: it is absolutely positioned inside a SCROLLING
// host, so left alone it rides away with the content — a project restore jumps to its
// saved scroll right after the arrival goes up, and at high zoom that carried the whole
// cloud off-screen ("no animation at all"). Returns an unpin to call on removal.
export const pinDustStage = (stage, host, baseLeft = 0, baseTop = 0) => {
  const onScroll = () => {
    stage.style.left = `${baseLeft + (host.scrollLeft || 0)}px`;
    stage.style.top = `${baseTop + (host.scrollTop || 0)}px`;
  };
  host.addEventListener?.('scroll', onScroll, { passive: true });
  return () => host.removeEventListener?.('scroll', onScroll);
};

// One dust stage over a canvas' visible slice, shared by ghostOut/ghostIn: the pixel
// snapshot, the DPR-scaled stage canvas pinned to its scrolling host, the snapshot↔
// screen mapping, and the per-frame clear/step/teardown loop. Null when there is
// nothing to play over.
const makeDustStage = (canvas) => {
  const field = dustField(canvas);
  if (!field) return null;
  const { r, vis } = field;
  const { cols, rows } = dustGrid(vis.width, vis.height, DUST_CELL_PX * (styleCode() ? STYLED_CELL_SCALE : 1));
  // The source is snapshotted once — ghostOut clears the real canvas moments later.
  const snap = document.createElement('canvas');
  snap.width = canvas.width;
  snap.height = canvas.height;
  const snapCtx = snap.getContext('2d');
  snapCtx.drawImage(canvas, 0, 0);
  const dpr = window.devicePixelRatio || 1;
  const stage = document.createElement('canvas');
  stage.className = 'canvas-dust';
  stage.width = Math.round(vis.width * dpr);
  stage.height = Math.round(vis.height * dpr);
  stage.style.left = `${field.left}px`;
  stage.style.top = `${field.top}px`;
  stage.style.width = `${vis.width}px`;
  stage.style.height = `${vis.height}px`;
  field.host.appendChild(stage);
  const unpin = pinDustStage(stage, field.host,
    field.left - (field.host.scrollLeft || 0), field.top - (field.host.scrollTop || 0));
  const finish = () => { unpin(); stage.remove(); };
  const ctx = stage.getContext('2d');
  ctx.scale(dpr, dpr);
  // Map the visible slice back into the snapshot: sources are bitmap pixels, the
  // visible box is screen pixels — at zoom the two differ by the zoom factor.
  const kx = snap.width / r.width, ky = snap.height / r.height;
  return {
    snap, snapCtx, ctx, finish, cols, rows,
    sw: (vis.width / cols) * kx, sh: (vis.height / rows) * ky,
    sox: (vis.left - r.left) * kx, soy: (vis.top - r.top) * ky,
    dw: vis.width / cols, dh: vis.height / rows,
    // Run `draw(t)` per rAF frame over `ms`, clearing the stage first each frame;
    // the timeout is belt and braces in case rAF is throttled.
    run(ms, draw) {
      const started = performance.now();
      const step = (now) => {
        const t = (now - started) / ms;
        if (t >= 1) { finish(); return; }
        ctx.clearRect(0, 0, vis.width, vis.height);
        draw(t);
        requestAnimationFrame(step);
      };
      requestAnimationFrame(step);
      setTimeout(finish, ms + 400);
    },
  };
};

// The colour of every cell, in ONE read: the visible slice is drawn down to a
// cols×rows canvas (the engine's own box filter) and read back once. A cell that holds
// no paint (alpha ~0 — the transparent margin of a page) flies nothing. Pure over its
// inputs, apart from the canvas it needs to sample with.
const sampleDustColours = (st) => {
  const { snap, cols, rows, sw, sh, sox, soy } = st;
  const probe = document.createElement('canvas');
  probe.width = cols;
  probe.height = rows;
  const pc = probe.getContext('2d', { willReadFrequently: true });
  pc.imageSmoothingEnabled = true;
  pc.imageSmoothingQuality = 'high';
  pc.drawImage(snap, sox, soy, sw * cols, sh * rows, 0, 0, cols, rows);
  return pc.getImageData(0, 0, cols, rows).data;
};

// How many alpha steps a fading grain is drawn in. The stage batches every grain of one
// colour AND one alpha step into a single fill — thousands of tiny fills a frame was the
// cost, not the arcs — and eight steps on a 3px grain are below what the eye resolves.
export const DUST_ALPHA_LEVELS = 8;

// Every grain of a flight. `gather` picks the arrival's sweep and throw; the fall is
// shared, the sideways fan differs (the arrival fans less). Grains are small source cells
// turned into discs: home cell, clear box, radius, coverage, throw and swirl. Painted from
// the accent palette like every cloud, not in the picture's own pixels.
const dustParts = (st, gather) => {
  const { cols, rows, dw, dh } = st;
  const px = sampleDustColours(st);
  const parts = [];
  for (let cy = 0; cy < rows; cy++) {
    for (let cx = 0; cx < cols; cx++) {
      const n = tileNoise(cx, cy);
      // Decorrelated from `n`: one hash driving fall AND drift slid the picture apart
      // in diagonal sheets; a separate hash for the x-axis is what makes it scatter.
      const m = tileNoise(cx + 41, cy + 17);
      const q = tileNoise(cx + 97, cy + 53);
      const i = (cy * cols + cx) * 4;
      const a = px[i + 3] / 255;
      const dx = gather ? (n - 0.5) * 26 : (m - 0.5) * 58;
      const dy = 26 + n * 46;
      const len = Math.hypot(dx, dy);
      const amp = (q - 0.5) * 2 * Math.min(len * SWIRL_SHARE, SWIRL_MAX_PX);
      // Cell bounds are rounded so neighbours share an edge — but the picture is blitted
      // at its fractional size, so the OUTER edge rounds up: rounding it down left a
      // sliver of picture along the right/bottom that no clear ever reached (a hairline).
      parts.push({
        x0: Math.round(cx * dw), y0: Math.round(cy * dh),
        x1: cx === cols - 1 ? Math.ceil(cols * dw) : Math.round((cx + 1) * dw),
        y1: cy === rows - 1 ? Math.ceil(rows * dh) : Math.round((cy + 1) * dh),
        hx: (cx + 0.5) * dw, hy: (cy + 0.5) * dh,
        r: (Math.min(dw, dh) / 2) * (0.62 + n * 0.5),
        delay: dustDelay(cy, rows, n, gather),
        dx, dy, len,
        swx: -(dy / len) * amp, swy: (dx / len) * amp,
        w: tileNoise(cx + 13, cy + 71),   // its own hash, for a water / fire ghost's styleFrame
        a,
        empty: a < 0.04,
      });
    }
  }
  return parts;
};

// One frame: the picture, minus every cell that has left it, plus the grains in the
// air. `k` is how far from home a grain is (0 whole, 1 gone); a cell at k=0 is simply
// the picture, and at k=1 nothing. Two passes — every clear first, then every grain —
// or a clear would punch holes in grains already drawn. Grains take their style's nudge,
// swell and glow (styleFrame) and are painted from the accent palette by their mix — one
// sweep per stop, batched per alpha step. `styled` (runDust) carries style and scratch.
const drawDust = (st, parts, t, gather, ks, lvl, lvlN, styled) => {
  const { ctx, snap, cols, rows, sw, sh, sox, soy, dw, dh } = st;
  ctx.globalAlpha = 1;
  ctx.drawImage(snap, sox, soy, sw * cols, sh * rows, 0, 0, dw * cols, dh * rows);
  for (let i = 0; i < parts.length; i++) {
    const p = parts[i];
    const pt = (t - p.delay) / Math.max(0.05, 1 - p.delay);
    styled.pts[i] = Math.max(0, Math.min(1, pt));
    let k;
    if (gather) { if (pt >= 1) { ks[i] = 0; continue; } k = pt <= 0 ? 1 : 1 - dustEase(pt); }
    else { if (pt <= 0) { ks[i] = 0; continue; } k = Math.min(1, pt); }
    ks[i] = k;
    ctx.clearRect(p.x0, p.y0, p.x1 - p.x0, p.y1 - p.y0);
  }
  const { style, palette, sf, pts, stop, fx, shapes, heads, poly } = styled;
  const flush = () => {
    for (let l = 0; l < DUST_ALPHA_LEVELS; l++) {
      const n = lvlN[l];
      if (!n) continue;
      ctx.globalAlpha = (l + 1) / DUST_ALPHA_LEVELS;
      fillGrains(ctx, lvl[l], n, poly);
      lvlN[l] = 0;
    }
  };
  // One grain into its alpha bucket: home + throw + swirl + the style's nudge, with its
  // shape and heading. A falling grain accelerates (k²); a landing one is eased already.
  const place = (i, p, k, l, sx, sy, scale) => {
    const s = Math.sin(Math.PI * k);
    const buf = lvl[l];
    const j = lvlN[l]++ * 5;
    buf[j] = p.hx + k * p.dx + s * p.swx + sx;
    buf[j + 1] = p.hy + (gather ? k : k * k) * p.dy + s * p.swy + sy;
    buf[j + 2] = p.r * (1 - k * 0.35) * scale;
    buf[j + 3] = shapes[i];
    buf[j + 4] = heads[i];
  };
  const tMs = t * styled.ms;
  // Every grain's touch once…
  for (let i = 0; i < parts.length; i++) {
    const k = ks[i], p = parts[i];
    if (k <= 0 || k >= 1 || p.empty) { stop[i] = -1; continue; }
    styleFrame(style, pts[i], k, p.w, p.len, tMs, sf);
    stop[i] = paletteIndex(style ? sf.mix : dustMix(p.w, 0), palette.length);
    fx[i * 4] = sf.sx; fx[i * 4 + 1] = sf.sy; fx[i * 4 + 2] = sf.scale; fx[i * 4 + 3] = sf.glow;
  }
  // …then one sweep per palette stop.
  for (let c = 0; c < palette.length; c++) {
    ctx.fillStyle = palette[c];
    for (let i = 0; i < parts.length; i++) {
      if (stop[i] !== c) continue;
      const l = Math.round((1 - ks[i]) * parts[i].a * fx[i * 4 + 3] * DUST_ALPHA_LEVELS) - 1;
      if (l < 0) continue;
      place(i, parts[i], ks[i], l, fx[i * 4], fx[i * 4 + 1], fx[i * 4 + 2]);
    }
    flush();
  }
};

// Play a whole flight over the stage: build the grains once, then draw per frame.
const runDust = (st, ms, gather) => {
  const parts = dustParts(st, gather);
  const ks = new Float32Array(parts.length);
  const lvl = Array.from({ length: DUST_ALPHA_LEVELS }, () => new Float32Array(parts.length * 5));
  const lvlN = new Int32Array(DUST_ALPHA_LEVELS);
  // The style, the palette (resolved once), each grain's fixed shape and heading, and the
  // per-grain scratch drawDust fills each frame: progress, stop, [sx, sy, scale, glow].
  const style = styleCode();
  const styled = {
    style, ms, palette: paletteCss().map((css) => resolveColour(document, css)), sf: {}, poly: [],
    pts: new Float32Array(parts.length), stop: new Int8Array(parts.length), fx: new Float32Array(parts.length * 4),
    shapes: Int8Array.from(parts, (p) => grainShape(style, p.w)),
    heads: Float32Array.from(parts, (p) => headingOf(p.dx, p.dy, gather)),
  };
  st.run(ms, (t) => drawDust(st, parts, t, gather, ks, lvl, lvlN, styled));
};

// Returns true when the dust is actually playing — the same contract as ghostIn, and
// for the same reason: the caller hides the emptied editor only while there are motes
// in front of it. Under reduced motion (or any other bail-out) it said nothing, and the
// editor was held blank for the whole 1.1s with nothing to look at.
export function ghostOut(canvas, { ms = GHOST_MS } = {}) {
  if (typeof document === 'undefined' || !canvas?.width || !canvas.height) return false;
  if (!dustEnabled()) return false;
  if (typeof requestAnimationFrame === 'undefined' || !canvas.parentElement) return false;
  try {
    const st = makeDustStage(canvas);
    if (!st) return false;
    runDust(st, ms, false);
    return true;
  } catch {
    return false;   // decoration only — the clear still happens
  }
}

// ── Canvas dust, the other way (ghostIn) ────────────────────────────────────
// The arrival is the clear PLAYED BACKWARDS: same grid, noise and fall, sweep
// reversed, so the picture builds bottom-to-top. It deliberately does NOT stream in
// from the drop point: the stage canvas is only as big as the image, and a mote pulled
// toward the cursor spends the whole flight outside it, clipped away.
// Returns true when the dust is actually playing: the caller only hides the real
// canvas if it is, or a bail-out (reduced motion, tiny canvas, no rAF) would leave a
// blank editor.
// Is anything actually drawn here? Sampled, not exhaustive — a stride wide enough to be
// cheap on a big canvas and fine enough that no real image reads as empty.
export function hasPixels(ctx, w, h, stride = 41) {
  if (!ctx?.getImageData || w < 1 || h < 1) return false;
  const d = ctx.getImageData(0, 0, w, h).data;
  for (let p = 3; p < d.length; p += 4 * stride) if (d[p] > 0) return true;
  return false;
}

export function ghostIn(canvas, { ms = GHOST_MS } = {}) {
  if (typeof document === 'undefined' || !canvas?.width || !canvas.height) return false;
  if (!dustEnabled()) return false;
  if (typeof requestAnimationFrame === 'undefined' || !canvas.parentElement) return false;
  try {
    const st = makeDustStage(canvas);
    if (!st) return false;
    // The snapshot has to HAVE something: taken before the load has painted, the dust
    // is invisible motes in front of a canvas the caller is holding hidden. Saying no
    // makes the caller fall back to the plain flight instead of animating nothing.
    if (!hasPixels(st.snapCtx, st.snap.width, st.snap.height)) { st.finish(); return false; }
    // The fall inverted: ghostOut carries a grain DOWN and away, so the arrival starts
    // it down there and lifts it home (dustParts, gather).
    runDust(st, ms, true);
    return true;
  } catch {
    return false;   // decoration only — the image is already loaded either way
  }
}

// ── The one place an image's ARRIVAL is played ──────────────────────────────
// Every route that puts a picture on the canvas ends here: a fresh file load AND a
// reopened project. It used to live inline in the loader only, so opening a saved
// project painted the image with no motion at all.
// Dust gather when ghostIn can run (the canvas waits behind it under
// ASSEMBLING_CLASS), the drop-point flight when it can't. Returns which one played,
// so the wiring is testable without a DOM.
export const ASSEMBLING_CLASS = 'canvas-assembling';
export const CLEARING_CLASS = 'canvas-clearing';

export function playCanvasArrival(canvas, { from = null, viewport, container, ghost = ghostIn } = {}) {
  const doc = typeof document !== 'undefined' ? document : null;
  // Both looked up by ID: the loader used to reach for the viewport by CLASS, and the
  // fullscreen layer clones the shell — two elements answering to one selector.
  const vp = viewport !== undefined ? viewport : doc?.getElementById('canvas-viewport') || null;
  const box = container !== undefined ? container : doc?.getElementById('canvas-container') || null;
  if (ghost(canvas)) {
    if (vp) flashLanding(vp, ASSEMBLING_CLASS, GHOST_MS);
    return 'dust';
  }
  // ghostIn declined (reduced motion, a canvas too small, nothing painted yet) — the
  // plain landing still runs, and is itself inert under reduced motion.
  arriveFrom(box, from);
  return 'flight';
}

// One-shot "it landed here" flash — restart-safe, so dropping twice in a row
// replays the animation instead of silently doing nothing the second time.
export function flashLanding(el, cls = 'drop-landing', ms = 700) {
  if (!el?.classList) return;
  // One timer PER CLASS, not per element: the canvas viewport carries two of these,
  // and a shared handle let the second call cancel the first one's removal — leaving a
  // class that hides the canvas on it for good.
  const timers = el._landingTimers || (el._landingTimers = {});
  clearTimeout(timers[cls]);
  el.classList.remove(cls);
  void el.offsetWidth;   // force a reflow so re-adding the class restarts it
  el.classList.add(cls);
  timers[cls] = setTimeout(() => el.classList.remove(cls), ms);
}

// ── Drawing a stroke: the new vertex FLIES to where you put it ──────────────
// Every route that adds a point ends in one motion: the vertex leaves where it came from
// — the point it extends, or its projection on the segment it splits — and travels to the
// click on a bowed path, overshooting before it settles. The renderer paints the flown
// position, so the segments hanging off it bend and join by themselves.
// C++ mirror: desktop/src/canvas/strokeGrowth.hpp — keep the two in step.

// The flight's own length: a short hop is nearly instant, a reach across the page
// still lands promptly. Lengths are IMAGE pixels on both sides, so zoom does not
// change the timing. Pure.
export const STROKE_FLY_MIN_MS = 150;
export const STROKE_FLY_MAX_MS = 420;
export const STROKE_FLY_PX_PER_MS = 2.4;
export const strokeFlyMs = (len) => Math.min(
  STROKE_FLY_MAX_MS, STROKE_FLY_MIN_MS + Math.max(0, len) / STROKE_FLY_PX_PER_MS);

// Ease-out-back: the vertex shoots a little past its target and comes back, which is
// what makes the segment read as REACHING for the point rather than being switched on.
// Weaker than the textbook 1.70158 — on a 3px stroke a big overshoot reads as a glitch.
export const STROKE_FLY_BACK = 1.28;
export const strokeFlyEase = (t) => {
  if (t <= 0) return 0;
  if (t >= 1) return 1;
  const u = t - 1;
  return 1 + (STROKE_FLY_BACK + 1) * u * u * u + STROKE_FLY_BACK * u * u;
};

// No vertex flies a straight line (the same rule the dust follows — tileWaypoint): it
// is pushed off its path by a share of the trip, capped, and back by the time it
// lands. `bow` is the signed side, -1..1. Pure.
export const STROKE_BOW_SHARE = 0.13;
export const STROKE_BOW_MAX = 22;
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
export const STROKE_POP_MS = 240;
export const STROKE_POP_PEAK = 1.5;
export const STROKE_FLY_R0 = 0.5;
export const strokeFlyRadius = (t) =>
  STROKE_FLY_R0 + (STROKE_POP_PEAK - STROKE_FLY_R0) * Math.min(1, Math.max(0, t)) ** 2;
export const strokePopScale = (u) => {
  if (u >= 1) return 1;
  const k = 1 - Math.min(1, Math.max(0, u));
  return 1 + (STROKE_POP_PEAK - 1) * k * k;
};

// The ring the landing pushes out — the one part of this that is not the line itself,
// so it stays faint and brief.
export const STROKE_RIPPLE_MS = 420;
export const STROKE_RIPPLE_REACH = 4.2;
export const STROKE_RIPPLE_ALPHA = 0.55;
export const strokeRipple = (u) => {
  const k = Math.min(1, Math.max(0, u));
  return {
    scale: 1 + (STROKE_RIPPLE_REACH - 1) * (1 - (1 - k) ** 2),
    alpha: STROKE_RIPPLE_ALPHA * (1 - k) ** 1.6,
  };
};

// The glow riding the vertex in flight: nothing at either end (it must not smudge the
// anchor it left or the point it became), brightest mid-trip.
export const STROKE_SPARK_REACH = 2.8;
export const STROKE_SPARK_ALPHA = 0.6;
export const strokeSpark = (t) => {
  const k = strokeArc(t);
  return { scale: 1 + (STROKE_SPARK_REACH - 1) * k, alpha: STROKE_SPARK_ALPHA * k ** 0.7 };
};

// How hot the segments the vertex is dragging burn, over the whole flight + settle:
// full as it leaves, out by the time it has landed.
export const STROKE_WAKE_ALPHA = 0.5;
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
