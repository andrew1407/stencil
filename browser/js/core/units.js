// Length tokens for the console API: a bare number is a pixel DELTA; a unit string ('3cm',
// '-4in', '50%') is absolute, where a leading '-' means "measured from the axis END", NOT
// a negative length.
import { CM_PER_INCH, cmToUnit } from '../utils.js';
import constants from '../config/constants.json' with { type: 'json' };
const { PAGE_SIZES } = constants;

// → { kind, value, fromEnd }: 'delta' (px move, signed) | 'px' | 'cm' (in/mm converted) |
// 'percent' (0..100). null on bad input.
export const parseLengthToken = (token) => {
  if (typeof token === 'number') return Number.isFinite(token) ? { kind: 'delta', value: token } : null;
  if (typeof token !== 'string') return null;
  const s = token.trim().toLowerCase();
  if (!s) return null;
  const m = /^(-)?\s*(\d*\.?\d+)\s*(px|cm|mm|in|%)?$/.exec(s);
  if (!m) return null;
  const fromEnd = m[1] === '-';
  const value = parseFloat(m[2]);
  const unit = m[3];
  if (unit === '%') return { kind: 'percent', value, fromEnd };
  if (unit === 'cm') return { kind: 'cm', value, fromEnd };
  if (unit === 'mm') return { kind: 'cm', value: value / 10, fromEnd };
  if (unit === 'in') return { kind: 'cm', value: value * CM_PER_INCH, fromEnd };
  if (unit === 'px') return { kind: 'px', value, fromEnd };
  return { kind: 'delta', value: fromEnd ? -value : value };
};

// → an ABSOLUTE pixel coordinate on an axis of `lengthPx`; `currentPx` is the base for a delta.
export const resolveAxisPx = (token, { lengthPx, pxPerCm, currentPx = 0 }) => {
  const t = parseLengthToken(token);
  if (!t) return null;
  if (t.kind === 'delta') return currentPx + t.value;
  let px;
  if (t.kind === 'px') px = t.value;
  else if (t.kind === 'cm') px = t.value * pxPerCm;
  else px = (t.value / 100) * lengthPx;       // percent
  return t.fromEnd ? lengthPx - px : px;
};

// Lowercased name → canonical casing, derived from PAGE_SIZES so a new format is accepted automatically.
const PAGE_NAME_BY_LOWER = Object.fromEntries(
  [...Object.keys(PAGE_SIZES), 'custom'].map((n) => [n.toLowerCase(), n]),
);

// Any case ('a3', 'Custom') → the canonical name ('A0'…'C10' or 'custom'); null otherwise.
export const normalizePageSize = (s) =>
  PAGE_NAME_BY_LOWER[String(s || '').trim().toLowerCase()] ?? null;

// Total length of every segment in a saved layout, in cm, by getPageDimensions' named-size +
// landscape-swap rule. Cached on the meta as `lineLengthCm`.
export const layoutLineLengthCm = (layout) => {
  const lines = layout && layout.lines;
  const cw = layout && layout.imageWidth;
  const ch = layout && layout.imageHeight;
  if (!Array.isArray(lines) || !lines.length || !cw || !ch) return 0;
  let pw, ph;
  if (layout.pageSize === 'custom') {
    pw = layout.customPageWidth;
    ph = layout.customPageHeight;
  } else {
    const ps = PAGE_SIZES[layout.pageSize] || PAGE_SIZES.A4;
    if (cw > ch) { pw = ps.height; ph = ps.width; } else { pw = ps.width; ph = ps.height; }
  }
  if (!pw || !ph) return 0;
  const sx = pw / cw, sy = ph / ch;
  let total = 0;
  for (const ln of lines) {
    const pts = ln && ln.points;
    if (!Array.isArray(pts)) continue;
    for (let i = 1; i < pts.length; i++) {
      total += Math.hypot((pts[i].x - pts[i - 1].x) * sx, (pts[i].y - pts[i - 1].y) * sy);
    }
  }
  return total;
};

// "A4 (21 × 29.7)"; unknown names (incl. 'custom') echo back unchanged. No trailing unit word:
// the label sits beside its own unit dropdown, which says it once per row (user report).
export const pageFormatLabel = (name, unit = 'cm') => {
  const ps = PAGE_SIZES[name];
  if (!ps) return name;
  const fmt = (cm) => +cmToUnit(cm, unit).toFixed(2);
  return `${name} (${fmt(ps.width)} × ${fmt(ps.height)})`;
};

// <option> markup for every named format, in PAGE_SIZES order; callers prepend extras such as Custom.
export const pageFormatOptions = (unit = 'cm') =>
  Object.keys(PAGE_SIZES)
    .map((n) => `<option value="${n}">${pageFormatLabel(n, unit)}</option>`)
    .join('\n');

// A relative delta (a bare number) rather than an absolute position.
export const isDeltaToken = (token) => {
  const t = parseLengthToken(token);
  return !!t && t.kind === 'delta';
};
