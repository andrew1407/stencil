// Port of core/script/lineStyle.cpp — `@use`: a unit, an order-free line style, or
// the `@use stencil` call the lowerer expands.
import { whereOf } from './args.js';
import { makeDiag } from './diagnostics.js';
import { isUnitWord } from './types.js';
import { cursorOf, isColorToken, isPunct, readLengthRaw } from './values.js';

const isStyleWord = (w) => w === 'solid' || w === 'dashed' || w === 'dotted';

// One comma group: a colour, a style word, a width, `fill <c>`, `point [<c>] [<size>]`,
// or a bare unit. Order between groups never matters.
const applyGroup = (group, state, seen, diags) => {
  const c = cursorOf(group);
  while (c.i < c.args.length) {
    const t = c.args[c.i];
    const low = t.text.toLowerCase();

    if (t.kind === 'number') {
      const raw = readLengthRaw(c);
      state.style.thickness = parseFloat(raw.number);
      continue;
    }
    if (isStyleWord(low)) {
      state.style.style = low;
      c.i += 1;
      continue;
    }
    if (isUnitWord(low)) {
      state.unit = low;
      c.i += 1;
      continue;
    }
    if (low === 'fill') {
      c.i += 1;
      if (c.i >= c.args.length || !isColorToken(c.args[c.i])) {
        diags.push(makeDiag('error', 'E_BAD_TOKEN', t, "'fill' needs a colour"));
        return false;
      }
      state.style.fillColor = c.args[c.i].text;
      c.i += 1;
      continue;
    }
    if (low === 'point') {
      c.i += 1;
      if (c.i < c.args.length && isColorToken(c.args[c.i])) {
        state.style.pointColor = c.args[c.i].text;
        c.i += 1;
      }
      if (c.i < c.args.length && c.args[c.i].kind === 'number') {
        const raw = readLengthRaw(c);
        state.style.pointSize = parseFloat(raw.number);
      }
      continue;
    }
    if (isColorToken(t)) {
      if (seen.color) {
        diags.push(makeDiag('error', 'E_DUP_LINE_COLOR', t, "'@use line' already has a stroke colour"));
        return false;
      }
      state.style.color = t.text;
      seen.color = true;
      c.i += 1;
      continue;
    }
    diags.push(makeDiag('error', 'E_BAD_TOKEN', t, `'${t.text}' is not a line-style word`));
    return false;
  }
  return true;
};

// Mutates `state`; the lowerer has already expanded every `@use stencil` call.
export const argsUse = (st, state, diags) => {
  const where = whereOf(st, '@use');
  if (st.args.length === 0) {
    diags.push(makeDiag('error', 'E_ARG_COUNT', where,
      "@use needs a unit, 'line …' or 'stencil <name>'"));
    return false;
  }

  const first = st.args[0].text.toLowerCase();
  if (first === 'stencil') return true;
  if (isUnitWord(first) && st.args.length === 1) {
    state.unit = first;
    return true;
  }
  if (first !== 'line') {
    diags.push(makeDiag('error', 'E_BAD_TOKEN', st.args[0],
      `'@use ${st.args[0].text}' — expected a unit, 'line' or 'stencil'`));
    return false;
  }

  let group = [];
  const seen = { color: false };
  for (let i = 1; i < st.args.length; i += 1) {
    if (isPunct(st.args[i], ',')) {
      if (!applyGroup(group, state, seen, diags)) return false;
      group = [];
      continue;
    }
    group.push(st.args[i]);
  }
  return applyGroup(group, state, seen, diags);
};
