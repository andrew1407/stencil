import { SURFACE_SPECK_PX } from './surfaceMotion.js';
import { tileNoise } from './tiles.js';
import { TUNE } from './tune.js';
const blankPaint = (c) => !c || c === 'transparent' || /,\s*0\s*\)$/.test(c);

// How far a mote is lifted off the surface's own background, towards its own ink: the
// body of the cloud, and its rim. See surfacePaint.
export const MOTE_INK = TUNE.MOTE_INK;
export const MOTE_RIM_INK = TUNE.MOTE_RIM_INK;

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
export const markPaint = (el) => surfacePaint(el, 'var(--text-muted)');

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
export const speckPainter = (el, override = null) => {
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
export const groupPainter = (el) => {
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
