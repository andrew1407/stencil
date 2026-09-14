// ── What a mote is painted in (browser js/ui/motion/painters.js twin) ───────
import { SURFACE_SPECK_PX } from './surfaceMotion.js';
import { tileNoise } from './tiles.js';

// Is `c` a colour that paints nothing? An unset background, or a fully transparent one.
const blankPaint = (c) => !c || c === 'transparent' || /,\s*0\s*\)$/.test(c);

// How far a mote is lifted off the surface's background towards its ink — body, then rim.
export const MOTE_INK = 42;
export const MOTE_RIM_INK = 66;

// What the motes are PAINTED in: the element's own background, else the nearest ancestor
// that actually paints one (a transparent box would dust in a fallback nobody chose), then
// LIFTED towards that surface's ink — the background alone is the same family of colour as
// the page and dissolves into it. Mixing in the ink flips with the theme for free.
const surfacePaint = (el) => {
  const get = typeof getComputedStyle === 'function' ? getComputedStyle : null;
  const own = get ? get(el) : null;
  let bg = '';
  for (let node = el; get && node && node.nodeType === 1 && !bg; node = node.parentElement)
    if (!blankPaint(get(node).backgroundColor)) bg = get(node).backgroundColor;
  bg = bg || 'var(--panel)';
  const ink = own && !blankPaint(own.color) ? own.color : 'var(--text)';
  const grain = (pct) => `color-mix(in srgb, ${bg} ${pct}%, ${ink})`;
  // `border-style: none` still COMPUTES a border colour, and its initial value is
  // `currentColor` — the TEXT colour, which paints every rim mote near-white on a dark
  // theme. Without a real border the rim is simply a stronger grain.
  const bordered = own && own.borderTopStyle !== 'none' && parseFloat(own.borderTopWidth) > 0;
  return { fill: grain(100 - MOTE_INK), edge: bordered ? own.borderTopColor : grain(100 - MOTE_RIM_INK) };
};

// Round speck in the surface's own colours; rim cells take its border, so the cloud keeps
// the window's outline for the first frames, and a few inner grains take that tone too so
// the field glints. The speck is a GRAIN, not the cell it sits in: past the mote budget a
// cell-filling square is the "huge rectangles" a scatter must never show.
export const speckPainter = (el) => {
  const { fill, edge } = surfacePaint(el);
  return ({ cx, cy, cols, rows, cellW, cellH }) => {
    const n = tileNoise(cx, cy);
    const rim = cx === 0 || cy === 0 || cx === cols - 1 || cy === rows - 1 || n > 0.86;
    // Grains of ONE size read as a mosaic; the spread is what makes it sand…
    const grain = Math.min(cellW, cellH, SURFACE_SPECK_PX);
    // …never faint: a mote you can barely see is a flight you cannot follow.
    return { color: rim ? edge : fill, alpha: 0.78 + n * 0.22, px: grain * (0.62 + n * 0.5), glint: rim };
  };
};
