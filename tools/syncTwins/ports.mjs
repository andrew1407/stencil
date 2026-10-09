// syncTwins' text rules (../syncTwins.mjs): how a port's body is told from its header and
// import spellings, and how one declaration is cut out of a source and put back in place.

// ── the port rules, as browser-extension/tests/portParity.test.js states them ──
// The header is the leading run of `//` lines and blanks; only it and import specifiers may differ.
const headerLength = (lines) => {
  let i = 0;
  while (i < lines.length && (/^\s*\/\//.test(lines[i]) || lines[i].trim() === '')) i++;
  return i;
};
const stripHeader = (src) => { const l = src.split('\n'); return l.slice(headerLength(l)).join('\n'); };
const SPEC = /(from\s+['"])([^'"]+)(['"])/g;
export const normalizePort = (src) => stripHeader(src).replace(SPEC, (_, a, s, b) => a + s.split('/').pop() + b);

// The copy keeps its own header and its own spelling of each import, found by basename.
export const regeneratePort = (original, copy) => {
  const lines = copy.split('\n');
  const spelled = new Map([...copy.matchAll(SPEC)].map((m) => [m[2].split('/').pop(), m[2]]));
  const missing = [];
  const body = stripHeader(original).replace(SPEC, (all, a, s, b) => {
    const mine = spelled.get(s.split('/').pop());
    if (!mine) missing.push(s);
    return mine ? a + mine + b : all;
  });
  return { text: [...lines.slice(0, headerLength(lines)), body].join('\n'), missing };
};

// A `/` after one of these opens a regex literal (a quote or bracket inside it is not structure).
const REGEX_AFTER = /(?:[(,=:[!&|?{};+\-*%<>~^]|\breturn|\btypeof)$/;
const regexEnd = (line, from) => {
  let cls = false;
  for (let j = from + 1; j < line.length; j++) {
    const c = line[j];
    if (c === '\\') j++;
    else if (cls) { if (c === ']') cls = false; } else if (c === '[') cls = true; else if (c === '/') return j;
  }
  return line.length;
};

// One `const NAME` / `function NAME` statement, to the line that balances it: the top-level one,
// else the first indented one (an inline copy inside an injected function or an IIFE).
export const declaration = (src, name) => {
  const lines = src.split('\n');
  const find = (lead) => lines.findIndex((l) => new RegExp(`^${lead}(?:const|function) ${name}\\b`).test(l));
  const top = find('(?:export )?');
  const start = top >= 0 ? top : find('\\s+');
  if (start < 0) return null;
  let depth = 0;
  for (let i = start; i < lines.length; i++) {
    const line = lines[i];
    for (let j = 0, quote = ''; j < line.length; j++) {
      const c = line[j];
      if (quote) { if (c === '\\') j++; else if (c === quote) quote = ''; continue; }
      if (c === '"' || c === "'" || c === '`') { quote = c; continue; }
      if (c === '/' && line[j + 1] === '/') break;
      if (c === '/' && line[j + 1] === '*') { const end = line.indexOf('*/', j + 2); j = end < 0 ? line.length : end + 1; continue; }
      if (c === '/' && REGEX_AFTER.test(line.slice(0, j).trimEnd() || '(')) {
        j = regexEnd(line, j);
        continue;
      }
      if ('([{'.includes(c)) depth++;
      else if (')]}'.includes(c)) depth--;
    }
    if (depth <= 0 && /[;}]\s*$/.test(line)) return lines.slice(start, i + 1).join('\n');
  }
  return null;
};

// What a declaration says, apart from where it sits: its indent and an `export`.
export const declarationShape = (decl) => {
  if (!decl) return decl;
  const pad = /^\s*/.exec(decl)[0];
  return decl.split('\n').map((l) => (l.startsWith(pad) ? l.slice(pad.length) : l)).join('\n').replace(/^export /, '');
};
// `want` placed where `mine` sits: mine's indent and mine's `export`.
export const placed = (want, mine) => {
  const pad = /^\s*/.exec(mine)[0];
  const lead = mine.trimStart().startsWith('export ') ? 'export ' : '';
  return (lead + declarationShape(want)).split('\n').map((l) => (l ? pad + l : l)).join('\n');
};
