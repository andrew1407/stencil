import { SURFACE_SPECK_PX } from './surfaceMotion.js';
import { tileNoise } from './tiles.js';
import { TUNE } from './tune.js';
const blankPaint = (c) => !c || c === 'transparent' || /,\s*0\s*\)$/.test(c);

export const MOTE_INK = TUNE.MOTE_INK;
export const MOTE_RIM_INK = TUNE.MOTE_RIM_INK;

// The element's own background, else the nearest ancestor that paints one, lifted towards the
// surface's ink: ink contrasts with its background in either theme.
const surfacePaint = (el, inkOverride = null) => {
  const get = typeof getComputedStyle === 'function' ? getComputedStyle : null;
  const own = get ? get(el) : null;
  let bg = '';
  for (let node = el; get && node && node.nodeType === 1 && !bg; node = node.parentElement)
    if (!blankPaint(get(node).backgroundColor)) bg = get(node).backgroundColor;
  bg = bg || 'var(--bg-container)';
  const ink = inkOverride || (own && !blankPaint(own.color) ? own.color : 'var(--text-main)');
  const grain = (pct) => `color-mix(in srgb, ${bg} ${pct}%, ${ink})`;
// `border-style: none` still computes a border colour — `currentColor`, the text colour —
// so only a real border is used as drawn.
  const bordered = own && own.borderTopStyle !== 'none' && parseFloat(own.borderTopWidth) > 0;
  return { fill: grain(100 - MOTE_INK), edge: bordered ? own.borderTopColor : grain(100 - MOTE_RIM_INK) };
};

// A mark's own text colour is full contrast and reads as a hard fleck: --text-muted instead.
export const markPaint = (el) => surfacePaint(el, 'var(--text-muted)');

// A painter answers per cell with the grain's colour, opacity and size; rim cells take the
// border. The speck is a GRAIN, never the cell. `override` is for a mark whose colour has left.
export const speckPainter = (el, override = null) => {
  const { fill, edge } = override || surfacePaint(el);
  return ({ cx, cy, cols, rows, cellW, cellH }) => {
    const n = tileNoise(cx, cy);
    const rim = cx === 0 || cy === 0 || cx === cols - 1 || cy === rows - 1 || n > 0.86;
// One size reads as a mosaic; the spread is what makes it sand.
    const grain = Math.min(cellW, cellH, SURFACE_SPECK_PX);
// Never faint: a mote you can barely see is a flight you cannot follow.
    return { color: rim ? edge : fill, alpha: 0.78 + n * 0.22, px: grain * (0.62 + n * 0.5), glint: rim };
  };
};

// A group dusts in its controls' own colours (desktop: controlReveal.hpp groupShot): every
// descendant that paints a background claims the cells under it, innermost winning.
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
