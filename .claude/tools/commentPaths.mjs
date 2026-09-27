// ── commentPaths: does every path a code comment names still exist? ────
// docPaths' rules applied to the comments of every tracked source file (the scanners
// commentOnlyDiff uses, plus CSS). A comment word counts as a path only with a file extension.
//   node .claude/tools/commentPaths.mjs [--check] [file...]   (--check exits 1 on a dead path)
import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';
import path from 'node:path';
import { BUILD_OUTPUT, EXT, dropIgnored, isCandidate, makeUniverse, resolves } from './docPaths.mjs';
import { langOf, scan } from './moveCheck.mjs';
import { scanPy } from './commentOnlyDiff.mjs';

// Words that read as paths but name something else — an example, a fixture's own file — keyed by
// the file whose comment says them.
export const ALLOW = Object.freeze({
  'cli/src/console/llm/planOps.zig': ['../x.png'],
  'cli/test_root.zig': ['../src/*.zig'],
  'desktop/tests/layerBoundary.headless.cpp': ['../app/x.hpp'],
  'vscode-extension/tests/manifest.test.js': ['.vscode/settings.json'],
});

const VENDORED = /(^|\/)(vendor|third_party)\/|(^|\/)stb_[\w]+\.h$/;

// ── comments ────────────────────────────────────────────────────
// CSS has block comments only: a `//` there is part of a URL.
export const scanCss = (src) => {
  const spans = [];
  for (let i = 0; i < src.length; i++) {
    const c = src[i];
    if (c === '"' || c === "'") {
      for (i++; i < src.length && src[i] !== c && src[i] !== '\n'; i++) if (src[i] === '\\') i++;
    } else if (c === '/' && src[i + 1] === '*') {
      const end = src.indexOf('*/', i + 2);
      const stop = end < 0 ? src.length : end + 2;
      spans.push({ kind: 'comment', start: i, end: stop });
      i = stop - 1;
    }
  }
  return spans;
};

const langFor = (file) => {
  const ext = path.extname(file).toLowerCase();
  if (ext === '.css' || ext === '.qss') return 'css';
  if (ext === '.py') return 'py';
  if (ext === '.ts') return 'js';
  return langOf(file);
};

const commentsOf = (src, lang) => (lang === 'css' ? scanCss(src) : lang === 'py' ? scanPy(src) : scan(src, lang))
  .filter((s) => s.kind === 'comment');

// Wrapping punctuation goes, and so does a `:line` or `#anchor` tail; a brace run stays whole.
export const wordOf = (raw) => {
  let w = raw.replace(/^[(["'`<*]+/, '');
  for (let prev = ''; prev !== w;) {
    prev = w;
    w = w.replace(/[)\]"'`>,.;:!?]+$/, '').replace(/:\d+(-\d+)?$/, '').replace(/#[\w-]*$/, '');
    if (w.endsWith('}') && w.split('{').length < w.split('}').length) w = w.slice(0, -1);
  }
  return w;
};

export const refsOf = (src, lang) => {
  const refs = [];
  let line = 1, at = 0;
  for (const c of commentsOf(src, lang)) {
    for (; at < c.start; at++) if (src[at] === '\n') line++;
    src.slice(c.start, c.end).split('\n').forEach((text, k) => {
      for (const raw of text.split(/\s+/)) {
        const token = wordOf(raw);
        if (token.includes('/') && /\w/.test(token)) refs.push({ token, line: line + k });
      }
    });
  }
  return refs;
};

// A `<placeholder>` is whole or it is prose: `<dir>/<name>.json` split at its space is no path.
const halfPlaceholder = (token) => (token.match(/</g) || []).length !== (token.match(/>/g) || []).length;

export const isPath = (token, file, u) => !halfPlaceholder(token)
  && EXT.test(token.replace(/[{}*]/g, '').replace(/\.\{[^}]*$/, '.x')) && isCandidate(token, file, u);

export const deadRefs = (file, src, u) => {
  const lang = langFor(file);
  if (!lang) return [];
  return refsOf(src, lang).filter(({ token }) => !(ALLOW[file] || []).includes(token)
    && isPath(token, file, u) && !resolves(token, file, u) && !BUILD_OUTPUT.test(token));
};

// ── the scan ────────────────────────────────────────────────────
const git = (root, ...args) => execFileSync('git', ['-C', root, ...args], { encoding: 'utf8', maxBuffer: 64 << 20 });

export const scanTree = (root, only = []) => {
  const files = git(root, 'ls-files', '-co', '--exclude-standard').split('\n')
    .filter((f) => f && existsSync(path.join(root, f)));
  const u = makeUniverse(files);
  const sources = (only.length ? only : files).filter((f) => langFor(f) && !VENDORED.test(f));
  const dead = sources.flatMap((file) => deadRefs(file, readFileSync(path.join(root, file), 'utf8'), u)
    .map((ref) => ({ doc: file, file, ...ref })));
  return dropIgnored(root, dead).map(({ doc, ...ref }) => ref);
};

if (import.meta.url === pathToFileURL(process.argv[1] || '').href) {
  const args = process.argv.slice(2);
  const root = git(process.cwd(), 'rev-parse', '--show-toplevel').trim();
  const only = args.filter((a) => a !== '--check').map((a) => path.relative(root, path.resolve(a)));
  const dead = scanTree(root, only);
  for (const d of dead) console.log(`${d.file}:${d.line}  ${d.token}`);
  console.log(`commentPaths: ${dead.length} dead path${dead.length === 1 ? '' : 's'}`);
  process.exit(args.includes('--check') && dead.length ? 1 : 0);
}
