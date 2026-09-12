import { core } from './stencilCore.js';

// Port of core/parse/durationParser.cpp. Grammar (1–2 whitespace tokens, case-insensitive):
// off|never|none → 0; a unit (day/week/fortnight/month/year, trailing 's' ok) alone means
// one; a positive integer + a unit in either order. Fixed durations (week=7d, fortnight=14d,
// month=30d, year=365d) match PERIOD_MS in projectsStore. Invalid spec ⇒ null.

const DAY_MS = 24 * 60 * 60 * 1000;

const UNIT_MS = {
  day: DAY_MS,
  week: 7 * DAY_MS,
  fortnight: 14 * DAY_MS,
  month: 30 * DAY_MS,
  year: 365 * DAY_MS,
};

// Singular or trailing-'s' plural; null if unknown.
function unitMs(word) {
  const w = word.length > 1 && word.endsWith('s') ? word.slice(0, -1) : word;
  return Object.prototype.hasOwnProperty.call(UNIT_MS, w) ? UNIT_MS[w] : null;
}

// Rejects signs, decimals, overflow.
function positiveInt(s) {
  if (!/^[0-9]+$/.test(s)) return null;
  const v = Number(s);
  return Number.isSafeInteger(v) && v > 0 ? v : null;
}

// JS reference for DurationParser::parse.
function parse(spec) {
  const toks = String(spec ?? '').toLowerCase().trim().split(/\s+/).filter(Boolean);
  if (toks.length === 0 || toks.length > 2) return null;

  if (toks.length === 1) {
    const t = toks[0];
    if (t === 'off' || t === 'never' || t === 'none') return 0;
    return unitMs(t);
  }

// A count and a unit, in either order.
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

// ms (0 = keep forever) or null; wasm when loaded, else the JS reference.
export const parseDuration = core.bind('parseDuration', parse);
