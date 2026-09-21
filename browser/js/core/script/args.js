// Port of core/script/args.cpp — the per-directive argument grammars, minus crop
// (crop.js) and `@use` (lineStyle.js).
import { makeDiag, tokenOfStmt } from './diagnostics.js';
import { SOURCE_KINDS, classifySource } from './types.js';
import { cursorOf, isColorToken, joinWords, readPointList } from './values.js';

// Where a whole-statement complaint points: the first argument, or the directive itself.
export const whereOf = (st, fallbackText) =>
  st.args.length > 0 ? st.args[0] : { ...tokenOfStmt(st), text: fallbackText };

export const argsFilter = (st, op, diags) => {
  const word = joinWords(st.args);
  const where = whereOf(st, '@filter');
  if (!word) {
    diags.push(makeDiag('error', 'E_ARG_COUNT', where,
      '@filter needs a mode (bw, sepia, invert, contour, none) or a colour'));
    return false;
  }
  const low = word.toLowerCase();
  if (low === 'bw' || low === 'sepia' || low === 'invert' || low === 'contour' || low === 'none') {
    op.strs = [low, ''];
    return true;
  }
  if (st.args.length === 1 && isColorToken(st.args[0])) {
    op.strs = ['custom', word];
    return true;
  }
  diags.push(makeDiag('error', 'E_UNKNOWN_FILTER', where, `'${word}' is not a filter mode or a colour`));
  return false;
};

export const argsShape = (st, state, locked, op, diags) => {
  const c = cursorOf(st.args);
  const where = whereOf(st, '@line');
  const pts = readPointList(c, state.unit, diags);
  if (!pts) return false;
  if (pts.length / 2 < 2) {
    diags.push(makeDiag('error', 'E_LINE_NEEDS_POINTS', where,
      `${locked ? '@rect' : '@line'} needs at least two points`));
    return false;
  }
  op.strs = [state.style.color, state.style.style, state.style.fillColor, state.style.pointColor];
  op.toks = pts;
  op.nums = [state.style.thickness, state.style.pointSize, locked ? 1 : 0];
  return true;
};

export const argsLayout = (st, op, diags) => {
  const words = [];
  let mode = 'combine';
  for (const t of st.args) {
    if (t.kind === 'punct') continue;
    const low = t.text.toLowerCase();
    if (low === 'combine' || low === 'replace') { mode = low; continue; }
    words.push(t);
  }
  const src = joinWords(words);
  const where = whereOf(st, '@layout');
  if (!src) {
    diags.push(makeDiag('error', 'E_ARG_COUNT', where, '@layout needs a path or URL'));
    return false;
  }
  op.strs = [src, mode];
  op.nums = [SOURCE_KINDS.indexOf(classifySource(src))];
  return true;
};

export const argsSave = (st, op) => {
  op.strs = [joinWords(st.args)];
  return true;
};

export const argsFrame = (st, op, diags) => {
  const where = whereOf(st, '@frame');
  if (st.args.length === 0 || st.args[0].kind !== 'number') {
    diags.push(makeDiag('error', 'E_ARG_COUNT', where, '@frame needs a frame index'));
    return false;
  }
  const n = parseInt(st.args[0].text, 10);
  if (!Number.isFinite(n) || n < 0) {
    diags.push(makeDiag('error', 'E_BAD_TOKEN', st.args[0], 'a frame index cannot be negative'));
    return false;
  }
  op.nums = [n];
  return true;
};
