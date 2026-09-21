// Port of core/script/crop.cpp — the two `@crop` forms, keys and positional insets.
import { whereOf } from './args.js';
import { makeDiag } from './diagnostics.js';
import { cursorOf, isPunct, readLength, skipPunct } from './values.js';

const CROP_KEYS = Object.freeze(['x1', 'x2', 'y1', 'y2', 'aspect']);

// '-10%' flips to the far edge; an inset is the same distance from either side.
const mirror = (tok) => (tok.startsWith('-') ? tok.slice(1) : `-${tok}`);

export const argsCrop = (st, state, op, diags) => {
  const c = cursorOf(st.args);
  const where = whereOf(st, '@crop');
  const edges = ['', '', '', '']; // x1, x2, y1, y2
  let aspect = '';
  let sawKey = false;
  let sawPositional = false;
  const positional = [];
  let album = 0;

  while (c.i < c.args.length) {
    skipPunct(c, ',');
    if (c.i >= c.args.length) break;
    const t = c.args[c.i];
    const slot = t.kind === 'ident' ? CROP_KEYS.indexOf(t.text.toLowerCase()) : -1;

    if (slot >= 0) {
      sawKey = true;
      c.i += 1;
      skipPunct(c, '=');
      if (slot === 4) { // aspect takes a raw 'W:H' word, not a length
        if (c.i >= c.args.length) {
          diags.push(makeDiag('error', 'E_BAD_TOKEN', t, "'aspect' needs a W:H ratio"));
          return false;
        }
        aspect = c.args[c.i].text;
        c.i += 1;
        // The lexer splits '3:2' on the ':'; re-join the far side.
        if (c.i < c.args.length && isPunct(c.args[c.i], ':')) {
          c.i += 1;
          if (c.i < c.args.length) { aspect += `:${c.args[c.i].text}`; c.i += 1; }
        }
        continue;
      }
      const tok = readLength(c, state.unit);
      if (tok === null) {
        diags.push(makeDiag('error', 'E_BAD_TOKEN', c.args[c.i] ?? t,
          `'${t.text.toLowerCase()}' needs a length`));
        return false;
      }
      edges[slot] = tok;
      continue;
    }
    if (t.kind === 'number') {
      sawPositional = true;
      positional.push(readLength(c, state.unit));
      continue;
    }
    if (t.kind === 'ident' && t.text.toLowerCase() === 'album') {
      album = 1;
      c.i += 1;
      continue;
    }
    diags.push(makeDiag('error', 'E_CROP_UNKNOWN_KEY', t,
      `'${t.text}' is not a crop key (x1, x2, y1, y2, aspect)`));
    return false;
  }

  if (sawKey && sawPositional) {
    diags.push(makeDiag('error', 'E_CROP_MIXED_FORM', where,
      '@crop takes either key=value pairs or bare insets, not both'));
    return false;
  }
  if (sawPositional) {
    if (positional.length === 1) {
      edges[0] = positional[0];
      edges[1] = mirror(positional[0]);
      edges[2] = positional[0];
      edges[3] = mirror(positional[0]);
    } else if (positional.length === 2) {
      edges[0] = positional[0];
      edges[1] = mirror(positional[0]);
      edges[2] = positional[1];
      edges[3] = mirror(positional[1]);
    } else if (positional.length === 4) {
      edges[0] = positional[0];
      edges[2] = positional[1];
      edges[1] = positional[2];
      edges[3] = positional[3];
    } else {
      diags.push(makeDiag('error', 'E_CROP_ARITY', where,
        `@crop takes 1, 2 or 4 insets, got ${positional.length}`));
      return false;
    }
  }
  if (!sawKey && !sawPositional) {
    diags.push(makeDiag('error', 'E_ARG_COUNT', where, '@crop needs an argument'));
    return false;
  }

  op.strs = [aspect];
  op.toks = edges;
  op.nums = [album];
  return true;
};
