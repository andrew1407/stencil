import { core } from './stencilCore.js';

// ── Human-duration parser (pure logic) ──────────────────────────
// Port of `core/parse/durationParser.cpp`: turns a free-form spec ("days 23",
// "fortnight", "month", "off") into a length in milliseconds (0 = keep forever),
// which the caller adds to Date.now() for an expiry timestamp. JS reference +
// fallback; wasm delegates to the same parser when loaded.
// Grammar (1–2 whitespace tokens, case-insensitive): off|never|none → 0; a unit
// (day/week/fortnight/month/year, trailing 's' ok) alone means one of it; a
// positive integer count + a unit (either order) means count units. Fixed
// durations — week=7d, fortnight=14d, month=30d, year=365d — matching PERIOD_MS
// in projectsStore so this and the C++ core agree. Invalid spec ⇒ null.

const DAY_MS = 24 * 60 * 60 * 1000;

// Milliseconds per unit word (fixed durations, no calendar library).
const UNIT_MS = {
  day: DAY_MS,
  week: 7 * DAY_MS,
  fortnight: 14 * DAY_MS,
  month: 30 * DAY_MS,
  year: 365 * DAY_MS,
};

// ms for one unit word (singular or trailing-'s' plural), or null if unknown.
function unitMs(word) {
  const w = word.length > 1 && word.endsWith('s') ? word.slice(0, -1) : word;
  return Object.prototype.hasOwnProperty.call(UNIT_MS, w) ? UNIT_MS[w] : null;
}

// A strictly-positive base-10 integer, or null (rejects signs, decimals, overflow).
function positiveInt(s) {
  if (!/^[0-9]+$/.test(s)) return null;
  const v = Number(s);
  return Number.isSafeInteger(v) && v > 0 ? v : null;
}

// JS reference — behaviorally identical to DurationParser::parse. Returns ms
// (0 for off/never) or null when the spec is invalid.
function parse(spec) {
  const toks = String(spec ?? '').toLowerCase().trim().split(/\s+/).filter(Boolean);
  if (toks.length === 0 || toks.length > 2) return null;

  if (toks.length === 1) {
    const t = toks[0];
    if (t === 'off' || t === 'never' || t === 'none') return 0;
    return unitMs(t); // bare unit = one of it (null if unknown)
  }

  // Two tokens: a count and a unit, in either order.
  let count = positiveInt(toks[0]);
  let unit = unitMs(toks[1]);
  if (count === null || unit === null) {
    unit = unitMs(toks[0]);
    count = positiveInt(toks[1]);
    if (unit === null || count === null) return null;
  }
  const ms = count * unit;
  return Number.isSafeInteger(ms) ? ms : null; // overflow guard (parity with C++)
}

// Parse `spec` → milliseconds (0 = keep forever) or null. Delegates to the wasm
// core when loaded, otherwise the JS reference above.
export const parseDuration = core.bind('parseDuration', parse);
