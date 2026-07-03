// ── Length-token parsing for the console API (pure) ─────────────────────────
// window.stencil expresses positions/sizes as a bare number (a pixel DELTA — "move by N px")
// or a unit string ('3cm', '-4in', '50%', '-60%'). A leading '-' on a UNIT/PERCENT token means
// "measured from the axis END" (image/page edge), NOT a negative length; on a bare number '-'
// keeps its arithmetic meaning (leftward/upward move). All pure + unit-tested; no DOM.
import { CM_PER_INCH, cmToUnit, unitLabel } from '../utils.js';
import constants from '../config/constants.json' with { type: 'json' };
const { PAGE_SIZES } = constants;

// Parse a token into { kind, value, fromEnd }: 'delta' = relative px move (value keeps its
// sign); 'px'|'cm' = absolute length (cm already converted from in/mm); 'percent' = 0..100
// fraction of axis length. fromEnd (absolute kinds) = measure from axis end. null on bad input.
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
  // A bare number string is a delta, like a real number — keep the sign.
  return { kind: 'delta', value: fromEnd ? -value : value };
};

// Resolve a token to an ABSOLUTE pixel coordinate on an axis. lengthPx = total axis length
// px (image/page extent); pxPerCm = px/cm for cm/in conversion; currentPx = base for a delta
// move. Returns null on unparseable input.
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

// Lowercased name → canonical casing, derived from the PAGE_SIZES table keys plus
// 'custom' — a new format added to the table is accepted here automatically.
const PAGE_NAME_BY_LOWER = Object.fromEntries(
  [...Object.keys(PAGE_SIZES), 'custom'].map((n) => [n.toLowerCase(), n]),
);

// Normalize a page-size argument to the canonical name the model uses ('A0'…'C10'
// or 'custom'), accepting any case ('a3', 'b5', 'Custom', …). Returns null for
// anything else.
export const normalizePageSize = (s) =>
  PAGE_NAME_BY_LOWER[String(s || '').trim().toLowerCase()] ?? null;

// Selector label for a named page format in the given display unit, e.g.
// "A4 (21 × 29.7 cm)" / "A4 (8.27 × 11.69 in)" (≤2 decimals, trailing zeros
// trimmed). Shared by the toolbar/links-modal option lists and applyUnitToUI's
// unit-change re-render. Unknown names (incl. 'custom') echo back unchanged.
export const pageFormatLabel = (name, unit = 'cm') => {
  const ps = PAGE_SIZES[name];
  if (!ps) return name;
  const fmt = (cm) => +cmToUnit(cm, unit).toFixed(2);
  return `${name} (${fmt(ps.width)} × ${fmt(ps.height)} ${unitLabel(unit)})`;
};

// <option> markup for every named format in the PAGE_SIZES table (canonical order),
// labelled via pageFormatLabel — the one builder behind the toolbar #page-size and
// links-modal quick-crop selects (callers prepend extras such as Custom…).
export const pageFormatOptions = (unit = 'cm') =>
  Object.keys(PAGE_SIZES)
    .map((n) => `<option value="${n}">${pageFormatLabel(n, unit)}</option>`)
    .join('\n');

// True when a token is a relative delta (a bare number) rather than an absolute
// position — callers that "move" vs "set" branch on this.
export const isDeltaToken = (token) => {
  const t = parseLengthToken(token);
  return !!t && t.kind === 'delta';
};
