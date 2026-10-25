// Function lengths for the 80-line function cap (caps.mjs): every outermost function body of a
// source file — a nested lambda or local function counts toward the one it sits in — found by one
// brace walk over the comment- and string-masked source, with a per-language test of the header
// the brace closes.
import { langOf, mask } from '../moveCheck.mjs';

export const MAX_FUNCTION_LINES = 80;

const CONTROL = /^(?:if|for|while|switch|catch|else|do|try|finally|with|unsafe|loop|match|defer|errdefer|comptime|select|lock|using|fixed|checked|unchecked|foreach)\b/;
// What a header must look like, per language, to open a function body rather than a block, class or literal.
const HEADER = {
  js: [/\bfunction\b[^{]*$/, /=>\s*$/, /^(?:(?:static|async|get|set)\s+)*\*?\s*[#\w$]+\s*\([^()]*(?:\([^()]*\)[^()]*)*\)\s*$/],
  cpp: [/\)\s*(?:const|noexcept(?:\([^)]*\))?|override|final|mutable|volatile|&{1,2}|->\s*[^;{}]+?|\s)*$/],
  cs: [/\)\s*(?:where\s[^{]*)?$/, /=>\s*$/],
  go: [/^func\b[^{]*$/],
  rs: [/\bfn\b[^{]*$/],
  zig: [/\bfn\b[^{]*$/],
};
const NAME = /([A-Za-z_$#~][\w$]*(?:::[~\w$]+)*)\s*(?:<[^()]*>)?\s*\(/g;

// The text a `{` at `at` closes the header of: back to the last `;`, `{` or `}` at this depth.
const headerBefore = (m, at) => {
  let j = at - 1, depth = 0;
  for (; j >= 0; j--) {
    const c = m[j];
    if (c === ')' || c === ']') depth++;
    else if (c === '(' || c === '[') depth--;
    else if (depth <= 0 && (c === ';' || c === '{' || c === '}')) break;
  }
  return { text: m.slice(j + 1, at), start: j + 1 };
};

const nameOf = (header, lang) => {
  const bind = /(?:const|let|var)\s+([\w$]+)\s*=|([#\w$]+)\s*[:=]\s*(?:async\s*)?(?:function\b|\()/.exec(header);
  if (lang === 'js' && bind && /=>\s*$|function/.test(header)) return bind[1] || bind[2];
  let last = null;
  for (const m of header.matchAll(NAME)) if (!CONTROL.test(m[1])) last = m[1];
  return last || '(anonymous)';
};

const lineAt = (src, i) => {
  let n = 1;
  for (let k = 0; k < i; k++) if (src.charCodeAt(k) === 10) n++;
  return n;
};

// [{ name, line, lines }] for one file; [] for a language the walk does not read.
export const functionsOf = (file, src) => {
  const lang = langOf(file);
  if (!lang || !HEADER[lang]) return [];
  const m = mask(src, lang);
  const out = [];
  const stack = [];   // { fn, start, name } per open brace
  for (let i = 0; i < m.length; i++) {
    const c = m[i];
    if (c === '{') {
      const outer = stack.some((s) => s.fn);
      const { text, start } = headerBefore(m, i);
      const h = text.replace(/\s+/g, ' ').trim();
      const fn = !outer && h !== '' && !CONTROL.test(h) && HEADER[lang].some((re) => re.test(h));
      const at = start + (text.length - text.trimStart().length);
      stack.push({ fn, start: at, name: fn ? nameOf(h, lang) : '' });
    } else if (c === '}') {
      const top = stack.pop();
      if (top?.fn) {
        const line = lineAt(src, top.start);
        out.push({ name: top.name, line, lines: lineAt(src, i) - line + 1 });
      }
    }
  }
  return out;
};

// `file#name` for each function over the cap, a repeated name numbered in file order (`name~2`).
export const overCap = (file, src, cap = MAX_FUNCTION_LINES) => {
  const seen = new Map();
  const out = [];
  for (const f of functionsOf(file, src)) {
    const k = (seen.get(f.name) || 0) + 1;
    seen.set(f.name, k);
    if (f.lines > cap) out.push({ key: `${file}#${f.name}${k > 1 ? `~${k}` : ''}`, lines: f.lines, line: f.line });
  }
  return out;
};
