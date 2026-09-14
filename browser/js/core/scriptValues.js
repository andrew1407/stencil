// Port of core/script/scriptValues.cpp — length tokens, colours and point lists.
import colorNamesTable from '../config/colorNames.json' with { type: 'json' };
import { makeDiag } from './scriptDiagnostics.js';
import { isHexColorWord } from './scriptLexer.js';
import { MAX_POINTS_PER_LINE, isUnitWord, unquoteWord } from './scriptTypes.js';

// A cursor over one statement's argument tokens.
export const cursorOf = (args) => ({ args, i: 0 });

// An argument run as one string: the words, unquoted, separators dropped.
export const joinWords = (args) =>
  args.filter((t) => t.kind !== 'punct').map((t) => unquoteWord(t.text)).join(' ');

export const isPunct = (t, text) => t.kind === 'punct' && t.text === text;

export const skipPunct = (c, text) => {
  while (c.i < c.args.length && isPunct(c.args[c.i], text)) c.i += 1;
};

// The same vocabulary core/color/colorNames.cpp accepts: the canonical keyword table,
// 'transparent', and the hex forms.
export const isColorToken = (t) => {
  if (t.kind === 'color') return isHexColorWord(t.text);
  if (t.kind !== 'ident') return false;
  const low = t.text.toLowerCase();
  return low === 'transparent' || Object.hasOwn(colorNamesTable, low);
};

export const applyUnit = (number, unit, fallback) => {
  const u = (unit || fallback || '').toLowerCase();
  if (!u || u === 'px') return `${number}px`;
  return `${number}${u}`;
};

// Reads NUMBER [UNIT], reporting whether the token carried its own unit.
export const readLengthRaw = (c) => {
  if (c.i >= c.args.length || c.args[c.i].kind !== 'number') return null;
  const num = c.args[c.i];
  let unit = '';
  c.i += 1;
  const next = c.args[c.i];
  if (next && next.kind === 'unit' && next.line === num.line && next.col === num.col + num.len) {
    unit = next.text;
    c.i += 1;
  }
  return { number: num.text, unit };
};

// Normalizes NUMBER [UNIT] into an absolute length token. A bare number takes `defaultUnit`,
// so '23' under `@use %` is '23%' — which is what makes a leading '-' mean "from the far edge".
export const readLength = (c, defaultUnit) => {
  const raw = readLengthRaw(c);
  return raw ? applyUnit(raw.number, raw.unit, defaultUnit) : null;
};

// A unit word sitting right after ')' applies to the whole pair.
const takePairUnit = (c) => {
  if (c.i >= c.args.length) return '';
  const t = c.args[c.i];
  const unitLike = t.kind === 'unit' || (t.kind === 'ident' && isUnitWord(t.text));
  if (!unitLike) return '';
  c.i += 1;
  return t.text.toLowerCase();
};

// Reads '(x, y) (x, y) …' into flat [x0,y0,x1,y1,…] length tokens. A unit may attach to a
// component, or to the pair after its ')'. Returns null on a malformed list, with a diagnostic.
export const readPointList = (c, defaultUnit, diags) => {
  const out = [];
  let points = 0;
  skipPunct(c, ',');
  while (c.i < c.args.length) {
    if (!isPunct(c.args[c.i], '(')) {
      diags.push(makeDiag('error', 'E_BAD_TOKEN', c.args[c.i],
        `expected a point '(x, y)', found '${c.args[c.i].text}'`));
      return null;
    }
    const open = c.args[c.i];
    c.i += 1;

    const x = readLengthRaw(c);
    if (!x) {
      diags.push(makeDiag('error', 'E_BAD_TOKEN', c.args[c.i] ?? open, 'expected a number for x'));
      return null;
    }
    skipPunct(c, ',');
    const y = readLengthRaw(c);
    if (!y) {
      diags.push(makeDiag('error', 'E_BAD_TOKEN', c.args[c.i] ?? open, 'expected a number for y'));
      return null;
    }
    if (c.i >= c.args.length || !isPunct(c.args[c.i], ')')) {
      diags.push(makeDiag('error', 'E_BAD_TOKEN', c.args[c.i] ?? open, "expected ')' to close the point"));
      return null;
    }
    c.i += 1;

    const pairUnit = takePairUnit(c);
    out.push(applyUnit(x.number, x.unit, !x.unit && pairUnit ? pairUnit : defaultUnit));
    out.push(applyUnit(y.number, y.unit, !y.unit && pairUnit ? pairUnit : defaultUnit));

    points += 1;
    if (points > MAX_POINTS_PER_LINE) {
      diags.push(makeDiag('error', 'E_LIMIT_POINTS', open, 'too many points in one shape'));
      return null;
    }
    skipPunct(c, ',');
  }
  return out;
};
