// Port of core/script/types.hpp. The caps are the SAME numbers the C++ uses, so wasm
// and this fallback reject the same input; browser/tests/wasm/wasm-parity-script.test.js proves it.

export const MAX_LINES = 20000;
export const MAX_TOKENS = 200000;
export const MAX_OPS = 5000;
export const MAX_BLOCKS = 256;
export const MAX_TEMPLATES = 256;
export const MAX_TEMPLATE_DEPTH = 16;
// Bounds a fan-out that yields no op, which MAX_OPS alone cannot see.
export const MAX_TEMPLATE_EXPANSIONS = MAX_OPS * MAX_TEMPLATE_DEPTH;
export const MAX_POINTS_PER_LINE = 200;
export const MAX_SOURCE_CHARS = 1024;

// Crossing the ABI as ints: never reorder, only append.
// 'keyword' holds its slot for the ABI; no token is lexed as one.
export const TOKEN_KINDS = Object.freeze([
  'comment', 'directive', 'keyword', 'number', 'unit',
  'color', 'string', 'param', 'punct', 'ident', 'error',
]);

export const OP_KINDS = Object.freeze([
  'open', 'frame', 'crop', 'filter', 'line', 'rect', 'layout', 'save', 'undo', 'redo',
]);

export const SOURCE_KINDS = Object.freeze(['project', 'file', 'url', 'dir', 'glob']);

export const DIRECTIVES = Object.freeze([
  'source', 'stencil', 'use', 'crop', 'filter', 'line', 'rect', 'layout', 'save', 'frame',
  'undo', 'redo',
]);

// The style `@use line` accumulates, applied to every @line / @rect after it.
export const defaultLineStyle = () => ({
  color: '#FFFF00',
  style: 'solid',
  fillColor: 'transparent',
  pointColor: '',
  thickness: 2,
  pointSize: 4,
});

export const isEditDirective = (d) =>
  d === 'crop' || d === 'filter' || d === 'line' || d === 'rect' || d === 'layout';

// A `@source` spec, classified for whichever adapter opens it.
export const classifySource = (spec) => {
  if (!spec) return 'project';
  const low = spec.toLowerCase();
  if (low.startsWith('http://') || low.startsWith('https://')) return 'url';
  if (spec.includes('*') || spec.includes('?') || spec.includes('[')) return 'glob';
  if (spec.endsWith('/')) return 'dir';
  return 'file';
};

// UTF-8 bytes of s[from, to): a column, a length and MAX_SOURCE_CHARS all count bytes, as the core does.
export const utf8Length = (s, from = 0, to = s.length) => {
  let n = 0;
  for (let k = from; k < to; k += 1) {
    const c = s.charCodeAt(k);
    if (c < 0x80) n += 1;
    else if (c < 0x800) n += 2;
    else if (c >= 0xd800 && c < 0xdc00 && k + 1 < to && (s.charCodeAt(k + 1) & 0xfc00) === 0xdc00) {
      n += 4;
      k += 1;
    } else n += 3;
  }
  return n;
};

// The longest prefix of `s` within `max` UTF-8 bytes; a character is never split.
export const utf8Truncate = (s, max) => {
  let bytes = 0;
  let k = 0;
  while (k < s.length) {
    const step = s.codePointAt(k) > 0xffff ? 2 : 1;
    const size = utf8Length(s, k, k + step);
    if (bytes + size > max) break;
    bytes += size;
    k += step;
  }
  return s.slice(0, k);
};

// A 1-based byte column on `line` as a 0-based UTF-16 index, for an editor that counts units.
export const unitIndexOfColumn = (line, col) => {
  let bytes = 0;
  let k = 0;
  while (k < line.length && bytes < col - 1) {
    const step = line.codePointAt(k) > 0xffff ? 2 : 1;
    bytes += utf8Length(line, k, k + step);
    k += step;
  }
  return k;
};

export const unquoteWord = (s) =>
  s.length >= 2 && s.startsWith('"') && s.endsWith('"') ? s.slice(1, -1) : s;

export const isUnitWord = (w) => {
  const u = String(w).toLowerCase();
  return u === 'px' || u === 'cm' || u === 'mm' || u === 'in' || u === '%';
};

// `@use stencil <name>`: the template call the lowerer expands, not a unit or a line style.
export const isStencilUse = (st) =>
  st.args.length > 0 && unquoteWord(st.args[0].text).toLowerCase() === 'stencil';
