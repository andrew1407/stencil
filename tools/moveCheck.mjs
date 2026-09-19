// ── moveCheck: did a JS refactor MOVE code, or change it? ────
// Hashes every top-level function / class-method / arrow-const body at <gitRef> and in the
// working tree — comments stripped, whitespace collapsed — then diffs the two multisets.
//   node tools/moveCheck.mjs <gitRef> <path...>   (paths: files or dirs; exits 1 on LOST)
//   node tools/moveCheck.mjs --self-test
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';
import path from 'node:path';

const LANG_BY_EXT = {
  '.js': 'js', '.mjs': 'js', '.cs': 'cs', '.go': 'go', '.rs': 'rs', '.zig': 'zig',
  '.cpp': 'cpp', '.hpp': 'cpp', '.h': 'cpp', '.c': 'cpp',
};
export const langOf = (file) => LANG_BY_EXT[path.extname(file).toLowerCase()] || null;

const WORD = /[A-Za-z0-9_$]/;
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

// ── unit extraction ─────────────────────────────────────────────
const DECL_CLASS = /^(?:export\s+)?(?:default\s+)?class\s+([\w$]+)/;
const DECL_FN = /^(?:export\s+)?(?:default\s+)?(?:async\s+)?function\s*\*?\s*([\w$]*)\s*\(/;
const DECL_MEMBER = /^(?:static\s+)?(?:async\s+)?(?:get\s+|set\s+)?\*?\s*([#\w$]+)\s*\(/;
const DECL_BIND = /^(?:export\s+)?(?:const|let|var|static)?\s*([#\w$]+)\s*=\s*(?!=)/;

const skipWs = (m, i) => { let j = i; while (j < m.length && /\s/.test(m[j])) j++; return j; };

const matchPair = (m, i, open, close) => {            // index just past the match for m[i]
  let depth = 0;
  for (let j = i; j < m.length; j++) {
    if (m[j] === open) depth++;
    else if (m[j] === close && --depth === 0) return j + 1;
  }
  return m.length;
};

// A `function …(…)` / `(…) =>` / `x =>` at p, and where its body starts.
const fnAt = (m, p) => {
  let j = skipWs(m, p);
  if (/^async[\s(]/.test(m.slice(j, j + 6))) j = skipWs(m, j + 5);
  const fm = /^function\s*\*?\s*[\w$]*\s*\(/.exec(m.slice(j, j + 200));
  if (fm) {
    const b = skipWs(m, matchPair(m, j + fm[0].length - 1, '(', ')'));
    return m[b] === '{' ? { start: b, block: true } : null;
  }
  let after;
  if (m[j] === '(') after = skipWs(m, matchPair(m, j, '(', ')'));
  else {
    const im = /^[\w$]+/.exec(m.slice(j, j + 200));
    if (!im) return null;
    after = skipWs(m, j + im[0].length);
  }
  if (m.slice(after, after + 2) !== '=>') return null;
  const b = skipWs(m, after + 2);
  return { start: b, block: m[b] === '{' };
};

// An expression-bodied arrow ends at the `;` (or the line break) that closes its statement.
const endOfExpr = (m, p) => {
  let j = p, depth = 0;
  while (j < m.length) {
    const c = m[j];
    if ('([{'.includes(c)) depth++;
    else if (')]}'.includes(c)) { if (depth === 0) return j; depth--; }
    else if (c === ';' && depth === 0) return j;
    else if (c === '\n' && depth === 0 && !/^[.?:+\-*/%&|,)\]}=<>]/.test(m[skipWs(m, j)] || '')) return j;
    j++;
  }
  return m.length;
};

const hashOf = (text) => createHash('sha256').update(text).digest('hex').slice(0, 12);
const lineAt = (src, i) => src.slice(0, i).split('\n').length;

// Every top-level function, arrow-const and class method, as { file, name, line, hash }.
export const unitsOf = (file, src) => {
  const m = mask(src, 'js');
  const units = [];
  const n = m.length;
  let i = 0, depth = 0, classBody = -1, className = '';
  const add = (name, start, end) => units.push({
    file, name: className ? `${className}.${name}` : name,
    line: lineAt(src, start), hash: hashOf(normalize(src.slice(start, end), 'js')),
  });
  while (i < n) {
    const c = m[i];
    if (c === '{') { depth++; i++; continue; }
    if (c === '}') { depth--; if (classBody >= 0 && depth < classBody) { classBody = -1; className = ''; } i++; continue; }
    const here = depth === 0 || depth === classBody;
    if (!here || !/[\w$#]/.test(c) || (i > 0 && WORD.test(m[i - 1]))) { i++; continue; }
    const rest = m.slice(i, i + 400);

    const cm = DECL_CLASS.exec(rest);
    if (cm) {
      const b = m.indexOf('{', i + cm[0].length);
      if (b >= 0) { className = cm[1]; depth++; classBody = depth; i = b + 1; continue; }
    }
    const fm = DECL_FN.exec(rest);
    if (fm) {
      const b = skipWs(m, matchPair(m, i + fm[0].length - 1, '(', ')'));
      if (m[b] === '{') { const e = matchPair(m, b, '{', '}'); add(fm[1] || '(anonymous)', b, e); i = e; continue; }
    }
    const mm = depth === classBody ? DECL_MEMBER.exec(rest) : null;
    if (mm && !/^(?:if|for|while|switch|catch|return)$/.test(mm[1])) {
      const b = skipWs(m, matchPair(m, i + mm[0].length - 1, '(', ')'));
      if (m[b] === '{') { const e = matchPair(m, b, '{', '}'); add(mm[1], b, e); i = e; continue; }
    }
    const bm = DECL_BIND.exec(rest);
    if (bm) {
      const fn = fnAt(m, i + bm[0].length);
      if (fn) {
        const e = fn.block ? matchPair(m, fn.start, '{', '}') : endOfExpr(m, fn.start);
        add(bm[1], fn.start, e);
        i = e;
        continue;
      }
    }
    i++;
  }
  return units;
};

// ── the diff ────────────────────────────────────────────────────
const bucket = (list) => {
  const by = new Map();
  for (const u of list) by.set(u.hash, [...(by.get(u.hash) || []), u]);
  return by;
};

export const diffUnits = (refUnits, headUnits) => {
  const a = bucket(refUnits), b = bucket(headUnits);
  const surplus = (from, other) => [...from].flatMap(([h, us]) => us.slice((other.get(h) || []).length));
  const unchanged = [...a].reduce((s, [h, us]) => s + Math.min(us.length, (b.get(h) || []).length), 0);
  return { lost: surplus(a, b), gained: surplus(b, a), unchanged };
};

// ── git + fs plumbing ───────────────────────────────────────────
const git = (...args) => execFileSync('git', args, { encoding: 'utf8', maxBuffer: 64 << 20 });
const isJs = (p) => ['.js', '.mjs'].includes(path.extname(p).toLowerCase());

// Repo-relative JS paths on both sides: the working tree now (tracked + new, never
// gitignored build output), the tree at <ref> then — so a file the move deleted still counts.
const listNow = (root, rel) => git('-C', root, 'ls-files', '-co', '--exclude-standard', '--', ...rel)
  .split('\n').filter((f) => f && isJs(f) && existsSync(path.join(root, f))).sort();
const listThen = (ref, rel) => git('ls-tree', '-r', '--name-only', ref, '--', ...rel)
  .split('\n').filter((f) => f && isJs(f)).sort();

const run = (ref, args) => {
  const root = git('rev-parse', '--show-toplevel').trim();
  const rel = args.map((a) => path.relative(root, path.resolve(a)) || '.');
  const now = listNow(root, rel);
  const then = listThen(ref, rel);

  const headUnits = now.flatMap((f) => unitsOf(f, readFileSync(path.join(root, f), 'utf8')));
  const refUnits = then.flatMap((f) => unitsOf(f, git('show', `${ref}:${f}`)));
  const { lost, gained, unchanged } = diffUnits(refUnits, headUnits);

  const at = (u) => `${u.hash}  ${u.file}:${u.line} ${u.name}`;
  console.log(`moveCheck ${ref} -> working tree  (${then.length} files then, ${now.length} now)`);
  for (const u of lost) console.log(`  LOST  ${at(u)}`);
  for (const u of gained) console.log(`  NEW   ${at(u)}`);
  console.log(`  unchanged ${unchanged}   LOST ${lost.length}   NEW ${gained.length}`);
  return lost.length ? 1 : 0;
};

// ── self-test ───────────────────────────────────────────────────
const selfTest = async () => {
  const { strict: assert } = await import('node:assert');
  const one = (file, src) => unitsOf(file, src);
  const bare = (src, lang) => stripComments(src, lang).trimEnd();   // a comment leaves blanks

  // A body that only moved file (and gained a comment) is the same body.
  const before = one('a.js', 'export const add = (a, b) => {\n  return a + b;\n};\n');
  const after = one('b.js', '// moved here\nexport const add = (a, b) => {\n  return a + b; // sum\n};\n');
  assert.equal(before.length, 1);
  assert.deepEqual(diffUnits(before, after), { lost: [], gained: [], unchanged: 1 });

  // An edited body is LOST + NEW.
  const edited = one('b.js', 'export const add = (a, b) => {\n  return a - b;\n};\n');
  const d = diffUnits(before, edited);
  assert.equal(d.lost.length, 1);
  assert.equal(d.gained.length, 1);
  assert.equal(d.unchanged, 0);

  // A `//` inside a string, a template and a regex is content, not a comment.
  assert.equal(stripComments('const u = "a // b";', 'js'), 'const u = "a // b";');
  assert.equal(stripComments('const u = `a // ${x ? "y // z" : `n // ${q}`} b`;', 'js'),
    'const u = `a // ${x ? "y // z" : `n // ${q}`} b`;');
  assert.equal(bare('const r = /a\\/\\/b/.test(s); // gone', 'js'), 'const r = /a\\/\\/b/.test(s);');
  assert.match(stripComments('const q = a / b; // gone\nconst w = 1;', 'js'), /^const q = a \/ b; +\nconst w = 1;$/);

  // Whitespace and comments are noise; the string's own spacing is not.
  assert.equal(normalize('f(  1,\n  2 ); /* c */', 'js'), 'f( 1, 2 );');
  assert.notEqual(normalize('g("a  b");', 'js'), normalize('g("a b");', 'js'));

  // Class methods and expression-bodied arrows are units too.
  const cls = one('c.js', 'class R {\n  #n = 1;\n  draw(ctx) {\n    ctx.fill();\n  }\n  static of(x) { return new R(x); }\n}\nexport const pick = (l) => (l.a ? l.a : l.b);\n');
  assert.deepEqual(cls.map((u) => u.name), ['R.draw', 'R.of', 'pick']);
  assert.deepEqual(diffUnits(cls, one('d.js', 'export const pick = (l) => (l.a ? l.a : l.b);\nclass R {\n  draw(ctx) { ctx.fill(); }\n  static of(x) { return new R(x); }\n}\n')),
    { lost: [], gained: [], unchanged: 3 });

  // C-family: block comments (nested in Rust), raw/verbatim strings, Zig multiline text.
  assert.equal(normalize('int a; /* x\ny */ int b;', 'cpp'), 'int a; int b;');
  assert.equal(bare('let s = r#"a // b"#; // gone', 'rs'), 'let s = r#"a // b"#;');
  assert.equal(normalize('let a = 1; /* x /* y */ still */ let b = 2;', 'rs'), 'let a = 1; let b = 2;');
  assert.equal(bare("let t: &'static str = \"// no\"; // gone", 'rs'), "let t: &'static str = \"// no\";");
  assert.equal(bare('const s = `a // b`; // gone', 'go'), 'const s = `a // b`;');
  assert.equal(bare('const s = "a // b"; // gone', 'zig'), 'const s = "a // b";');
  assert.equal(bare('var s = @"a // b"; // gone', 'cs'), 'var s = @"a // b";');
  assert.equal(bare('auto n = 1\'000\'000; // gone', 'cpp'), 'auto n = 1\'000\'000;');

  console.log('moveCheck self-test: ok');
};

if (import.meta.url === pathToFileURL(process.argv[1] || '').href) {
  const args = process.argv.slice(2);
  if (args[0] === '--self-test') await selfTest();
  else if (args.length < 2) { console.error('usage: node tools/moveCheck.mjs <gitRef> <path...>   |   --self-test'); process.exit(2); }
  else process.exit(run(args[0], args.slice(1)));
}
