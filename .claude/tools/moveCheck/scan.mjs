// The string-aware scanner moveCheck and commentOnlyDiff share: cuts source into code, comment
// and literal spans per language, so a `//` inside a string, template or regex stays content.

import path from 'node:path';

const LANG_BY_EXT = Object.freeze({
  '.js': 'js', '.mjs': 'js', '.cs': 'cs', '.go': 'go', '.rs': 'rs', '.zig': 'zig',
  '.cpp': 'cpp', '.hpp': 'cpp', '.h': 'cpp', '.c': 'cpp',
});
export const langOf = (file) => LANG_BY_EXT[path.extname(file).toLowerCase()] || null;

export const WORD = /[A-Za-z0-9_$]/;
// A `/` right after one of these words opens a regex, not a division.
const REGEX_AFTER = new Set(['return', 'typeof', 'instanceof', 'in', 'of', 'new', 'delete',
  'void', 'throw', 'case', 'do', 'else', 'yield', 'await']);
// A real char literal — tells 'a' apart from a Rust lifetime and a C++ digit separator.
const CHAR_LIT = /^'(?:\\(?:x[0-9a-fA-F]{1,2}|u\{[0-9a-fA-F]{1,6}\}|u[0-9a-fA-F]{4}|U[0-9a-fA-F]{8}|[0-7]{1,3}|.)|[^\\'])'/;

// ── the scanner ─────────────────────────────────────────────────
const endOfQuoted = (src, i, quote, escapes = true, singleLine = false) => {
  for (let j = i + 1; j < src.length; j++) {
    if (escapes && src[j] === '\\') { j++; continue; }
    if (src[j] === quote) return j + 1;
    if (singleLine && src[j] === '\n') return j;
  }
  return src.length;
};

const endOfBlock = (src, i, nested) => {
  let j = i + 2, depth = 1;
  while (j < src.length) {
    if (nested && src[j] === '/' && src[j + 1] === '*') { depth++; j += 2; continue; }
    if (src[j] === '*' && src[j + 1] === '/') { j += 2; if (--depth === 0) return j; continue; }
    j++;
  }
  return src.length;
};

// Past the `}` closing a ${…}: it holds code, so strings and nested templates count.
const endOfInterp = (src, i) => {
  let j = i, depth = 0;
  while (j < src.length) {
    const c = src[j], d = src[j + 1];
    if (c === '}') { if (depth === 0) return j + 1; depth--; j++; continue; }
    if (c === '{') { depth++; j++; continue; }
    if (c === '`') { j = endOfTemplate(src, j); continue; }
    if (c === '"' || c === "'") { j = endOfQuoted(src, j, c, true, true); continue; }
    if (c === '/' && d === '/') { const e = src.indexOf('\n', j); j = e < 0 ? src.length : e; continue; }
    if (c === '/' && d === '*') { j = endOfBlock(src, j, false); continue; }
    j++;
  }
  return src.length;
};

const endOfTemplate = (src, i) => {
  let j = i + 1;
  while (j < src.length) {
    const c = src[j];
    if (c === '\\') { j += 2; continue; }
    if (c === '`') return j + 1;
    if (c === '$' && src[j + 1] === '{') { j = endOfInterp(src, j + 2); continue; }
    j++;
  }
  return src.length;
};

const endOfRegex = (src, i) => {
  let j = i + 1, klass = false;
  while (j < src.length) {
    const c = src[j];
    if (c === '\\') { j += 2; continue; }
    if (c === '\n') return i + 1;                     // unterminated: it was a division
    if (c === '[') klass = true;
    else if (c === ']') klass = false;
    else if (c === '/' && !klass) { j++; while (j < src.length && WORD.test(src[j])) j++; return j; }
    j++;
  }
  return i + 1;
};

// `/` after a value is division; after an operator, a keyword or nothing it opens a regex.
const regexAllowed = (src, i) => {
  let j = i - 1;
  while (j >= 0 && /\s/.test(src[j])) j--;
  if (j < 0) return true;
  const c = src[j];
  if (!WORD.test(c)) return !')]"\'`'.includes(c);
  let k = j;
  while (k >= 0 && WORD.test(src[k])) k--;
  return REGEX_AFTER.has(src.slice(k + 1, j + 1));
};

const endOfCppRaw = (src, i) => {                     // src[i] is the `"` of R"delim(…)delim"
  const open = src.indexOf('(', i);
  if (open < 0) return endOfQuoted(src, i, '"');
  const close = `)${src.slice(i + 1, open)}"`;
  const e = src.indexOf(close, open);
  return e < 0 ? src.length : e + close.length;
};

const endOfVerbatim = (src, i) => {                   // C# @"…", where "" is the escape
  let j = i + 1;
  while (j < src.length) {
    if (src[j] === '"') { if (src[j + 1] !== '"') return j + 1; j += 2; continue; }
    j++;
  }
  return src.length;
};

// Cuts the source into 'code' / 'comment' / 'literal' spans. Literals are opaque: a `//`
// inside a string, template or regex is content, never a comment.
export const scan = (src, lang) => {
  const spans = [];
  const n = src.length;
  let i = 0, from = 0;
  const cut = (kind, start, end) => {
    if (start > from) spans.push({ kind: 'code', start: from, end: start });
    spans.push({ kind, start, end });
    from = end;
    i = end;
  };
  while (i < n) {
    const c = src[i], d = src[i + 1];
    if (c === '/' && d === '/') { const e = src.indexOf('\n', i); cut('comment', i, e < 0 ? n : e); continue; }
    if (c === '/' && d === '*' && lang !== 'zig') { cut('comment', i, endOfBlock(src, i, lang === 'rs')); continue; }
    if (c === '\\' && d === '\\' && lang === 'zig') { const e = src.indexOf('\n', i); cut('literal', i, e < 0 ? n : e); continue; }
    if (c === '"') {
      if (lang === 'cpp' && src[i - 1] === 'R') { cut('literal', i, endOfCppRaw(src, i)); continue; }
      if (lang === 'cs' && src[i - 1] === '@') { cut('literal', i, endOfVerbatim(src, i)); continue; }
      cut('literal', i, endOfQuoted(src, i, '"', true, true));
      continue;
    }
    if (c === "'") {
      if (lang === 'js') { cut('literal', i, endOfQuoted(src, i, "'", true, true)); continue; }
      const m = CHAR_LIT.exec(src.slice(i, i + 14));
      if (m) { cut('literal', i, i + m[0].length); continue; }
      i++;                                            // Rust lifetime / C++ digit separator
      continue;
    }
    if (c === '`' && lang === 'js') { cut('literal', i, endOfTemplate(src, i)); continue; }
    if (c === '`' && lang === 'go') { const e = src.indexOf('`', i + 1); cut('literal', i, e < 0 ? n : e + 1); continue; }
    if (c === 'r' && lang === 'rs' && !WORD.test(src[i - 1] || ' ')) {
      const m = /^r(#*)"/.exec(src.slice(i, i + 20));
      if (m) {
        const close = `"${m[1]}`;
        const e = src.indexOf(close, i + m[0].length);
        cut('literal', i, e < 0 ? n : e + close.length);
        continue;
      }
    }
    if (c === '/' && lang === 'js' && regexAllowed(src, i)) { cut('literal', i, endOfRegex(src, i)); continue; }
    i++;
  }
  if (from < n) spans.push({ kind: 'code', start: from, end: n });
  return spans;
};

const blank = (t) => t.replace(/[^\n]/g, ' ');        // same length, same line count

export const stripComments = (src, lang) => scan(src, lang)
  .map((s) => (s.kind === 'comment' ? blank(src.slice(s.start, s.end)) : src.slice(s.start, s.end)))
  .join('');

// Same length as the source, with literal innards and comments blotted out — so a brace or
// a `(` found in it is real code, and its offsets still index the original.
export const mask = (src, lang) => scan(src, lang).map((s) => {
  const t = src.slice(s.start, s.end);
  if (s.kind === 'code') return t;
  if (s.kind === 'comment') return blank(t);
  return t.length < 2 ? t : t[0] + t.slice(1, -1).replace(/[^\n]/g, 'x') + t[t.length - 1];
}).join('');

// Comment-free, whitespace-collapsed — but never inside a literal. Rescanning the stripped
// text is what lets the code around a dropped comment close up into one run.
export const normalize = (src, lang = 'js') => {
  const bare = stripComments(src, lang);
  return scan(bare, lang)
    .map((s) => (s.kind === 'code' ? bare.slice(s.start, s.end).replace(/\s+/g, ' ') : bare.slice(s.start, s.end)))
    .join('')
    .trim();
};

