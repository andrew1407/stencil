// ── commentOnlyDiff: did a comment sweep touch only comments? ────
// Strips comments and collapses whitespace on both sides — the file at <gitRef> and the one
// in the working tree — then compares what is left, exactly. `.py` counts only `#`; a
// docstring is a statement, kept.
//   node tools/commentOnlyDiff.mjs <gitRef> <path...>   (exits 1 if any file CHANGED)
import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';
import path from 'node:path';
import { langOf, scan, stripComments } from './moveCheck.mjs';

const NL = '\u0000';                         // a newline inside a literal, parked

// ── Python ──────────────────────────────────────────────────────
const endOfPyQuoted = (src, i, quote) => {
  for (let j = i + 1; j < src.length; j++) {
    if (src[j] === '\\') { j++; continue; }           // escapes still delimit in r'' strings
    if (src[j] === quote || src[j] === '\n') return j + 1;
  }
  return src.length;
};

// Only `#` is a comment. Every string stays — a docstring is a statement, not a comment.
export const scanPy = (src) => {
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
    const c = src[i];
    if (c === '#') { const e = src.indexOf('\n', i); cut('comment', i, e < 0 ? n : e); continue; }
    if (c === '"' || c === "'") {
      const triple = c.repeat(3);
      if (src.startsWith(triple, i)) {
        const e = src.indexOf(triple, i + 3);
        cut('literal', i, e < 0 ? n : e + 3);
        continue;
      }
      cut('literal', i, endOfPyQuoted(src, i, c));
      continue;
    }
    i++;
  }
  if (from < n) spans.push({ kind: 'code', start: from, end: n });
  return spans;
};

const spansOf = (src, lang) => (lang === 'py' ? scanPy(src) : scan(src, lang));

const bareOf = (src, lang) => (lang === 'py'
  ? scanPy(src).map((s) => (s.kind === 'comment' ? ' '.repeat(s.end - s.start) : src.slice(s.start, s.end))).join('')
  : stripComments(src, lang));

// ── the comparison: comment-free, blank lines dropped, spaces squeezed ──
// Never inside a literal, whose own spacing is code.
export const normalizeLines = (src, lang) => {
  const bare = bareOf(src, lang);
  const text = spansOf(bare, lang).map((s) => {
    const t = bare.slice(s.start, s.end);
    return s.kind === 'code' ? t.replace(/[^\S\n]+/g, ' ') : t.replaceAll('\n', NL);
  }).join('');
  return text.split('\n').map((l) => l.trim()).filter(Boolean).map((l) => l.replaceAll(NL, '\n'));
};

// The verdict is on the whole normalized text — dropping a block comment can rejoin two
// lines into one, and that is still comment-only. The lines are for pointing at the change.
export const compare = (refSrc, headSrc, lang) => {
  const a = normalizeLines(refSrc, lang);
  const b = normalizeLines(headSrc, lang);
  if (a.join(' ') === b.join(' ')) return { same: true };
  const at = a.findIndex((l, i) => l !== b[i]);
  const i = at < 0 ? Math.min(a.length, b.length) : at;
  return { same: false, at: i + 1, then: a[i] ?? '(end of file)', now: b[i] ?? '(end of file)' };
};

// ── git plumbing ────────────────────────────────────────────────
const git = (...args) => execFileSync('git', args, { encoding: 'utf8', maxBuffer: 64 << 20 });

const langFor = (f) => langOf(f) || (path.extname(f).toLowerCase() === '.py' ? 'py' : null);

const run = (ref, args) => {
  const root = git('rev-parse', '--show-toplevel').trim();
  const rel = args.map((a) => path.relative(root, path.resolve(a)) || '.');
  const files = git('-C', root, 'ls-files', '-co', '--exclude-standard', '--', ...rel)
    .split('\n').filter((f) => f && existsSync(path.join(root, f))).sort();

  console.log(`commentOnlyDiff ${ref} -> working tree`);
  let changed = 0, skipped = 0;
  for (const f of files) {
    const lang = langFor(f);
    if (!lang) { skipped++; continue; }
    let refSrc;
    try {
      refSrc = execFileSync('git', ['show', `${ref}:${f}`], { encoding: 'utf8', maxBuffer: 64 << 20, stdio: ['ignore', 'pipe', 'ignore'] });
    } catch {
      changed++;
      console.log(`  CHANGED  ${f}  (absent at ${ref})`);
      continue;
    }
    const r = compare(refSrc, readFileSync(path.join(root, f), 'utf8'), lang);
    if (r.same) { console.log(`  OK       ${f}`); continue; }
    changed++;
    console.log(`  CHANGED  ${f}`);
    console.log(`             then #${r.at}  ${r.then}`);
    console.log(`             now  #${r.at}  ${r.now}`);
  }
  console.log(`  ${changed} changed of ${files.length - skipped} compared (${skipped} skipped)`);
  return changed ? 1 : 0;
};

// ── self-test ───────────────────────────────────────────────────
const selfTest = async () => {
  const { strict: assert } = await import('node:assert');
  const ok = (a, b, lang) => assert.equal(compare(a, b, lang).same, true, `${lang}: expected OK`);
  const changed = (a, b, lang) => {
    const r = compare(a, b, lang);
    assert.equal(r.same, false, `${lang}: expected CHANGED`);
    return r;
  };

  // A comment sweep: rewritten, added, dropped, reflowed — and the code untouched.
  ok('// old note\nconst a = 1;\nconst b = 2; // trailing\n',
    'const a = 1;\n\n// a fresh note\n// over two lines\nconst b = 2;\n', 'js');
  ok('const a = 1; /* mid\n   line */ const b = 2;\n', 'const a = 1; const b = 2;\n', 'js');

  // A code edit is not a comment sweep, and the first differing line says where.
  const r = changed('const a = 1;\nconst b = 2;\n', '// note\nconst a = 1;\nconst b = 3;\n', 'js');
  assert.equal(r.at, 2);
  assert.equal(r.then, 'const b = 2;');
  assert.equal(r.now, 'const b = 3;');

  // A `//` inside a string, a template or a regex is content, not a comment.
  ok('const u = "http://x"; // note\n', 'const u = "http://x";\n', 'js');
  changed('const u = "a // b";\n', 'const u = "a";\n', 'js');
  changed('const t = `a // ${x} b`;\n', 'const t = `a ${x} b`;\n', 'js');
  changed('const r = /a\\/\\/b/;\n', 'const r = /ab/;\n', 'js');
  // Runs of whitespace are noise; the whitespace inside a literal is not.
  ok('f(1,\n  2);\n', 'f(1, 2);\n', 'js');
  changed('f("a  b");\n', 'f("a b");\n', 'js');

  // The other C-family surfaces.
  ok('int a = 1;  // note\n', '/* note */ int a = 1;\n', 'cpp');
  ok('let s = r#"a // b"#; // note\n', 'let s = r#"a // b"#;\n', 'rs');
  changed('let s = r#"a // b"#;\n', 'let s = r#"a b"#;\n', 'rs');
  ok('const s = `a // b`; // note\n', 'const s = `a // b`;\n', 'go');
  ok('const s = "a"; // note\n', '// note\nconst s = "a";\n', 'zig');
  ok('var s = @"a // b"; // note\n', 'var s = @"a // b";\n', 'cs');

  // Python: `#` goes, the docstring stays — it is a statement.
  ok('# note\ndef f():\n    """Doc."""\n    return 1  # trailing\n',
    'def f():\n    """Doc."""\n    # a fresh note\n    return 1\n', 'py');
  changed('def f():\n    """Doc."""\n    return 1\n', 'def f():\n    """Other."""\n    return 1\n', 'py');
  ok('s = "a # b"  # note\n', 's = "a # b"\n', 'py');
  changed('s = "a # b"\n', 's = "a"\n', 'py');

  console.log('commentOnlyDiff self-test: ok');
};

if (import.meta.url === pathToFileURL(process.argv[1] || '').href) {
  const args = process.argv.slice(2);
  if (args[0] === '--self-test') await selfTest();
  else if (args.length < 2) { console.error('usage: node tools/commentOnlyDiff.mjs <gitRef> <path...>   |   --self-test'); process.exit(2); }
  else process.exit(run(args[0], args.slice(1)));
}
