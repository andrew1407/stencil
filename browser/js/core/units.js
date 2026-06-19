// ── Length-token parsing for the console API (pure) ─────────────────────────
// window.stencil expresses positions/sizes as a bare number (a pixel DELTA — "move by N px")
// or a unit string ('3cm', '-4in', '50%', '-60%'). A leading '-' on a UNIT/PERCENT token means
// "measured from the axis END" (image/page edge), NOT a negative length; on a bare number '-'
// keeps its arithmetic meaning (leftward/upward move). All pure + unit-tested; no DOM.
import { CM_PER_INCH } from '../utils.js';

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
  switch (m[3]) {
    case '%': return { kind: 'percent', value, fromEnd };
    case 'cm': return { kind: 'cm', value, fromEnd };
    case 'mm': return { kind: 'cm', value: value / 10, fromEnd };
    case 'in': return { kind: 'cm', value: value * CM_PER_INCH, fromEnd };
    case 'px': return { kind: 'px', value, fromEnd };
    // A bare number string is a delta, like a real number — keep the sign.
    default: return { kind: 'delta', value: fromEnd ? -value : value };
  }
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

// Normalize a page-size argument to the canonical 'A3' | 'A4' | 'custom' the model
// uses, accepting any case ('a3', 'Custom', …). Returns null for anything else.
export const normalizePageSize = (s) => {
  switch (String(s || '').trim().toLowerCase()) {
    case 'a3': return 'A3';
    case 'a4': return 'A4';
    case 'custom': return 'custom';
    default: return null;
  }
};

// True when a token is a relative delta (a bare number) rather than an absolute
// position — callers that "move" vs "set" branch on this.
export const isDeltaToken = (token) => {
  const t = parseLengthToken(token);
  return !!t && t.kind === 'delta';
};
