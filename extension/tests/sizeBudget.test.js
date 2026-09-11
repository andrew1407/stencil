// The size + comment ratchet for the extension: no new oversized module, no listed file
// growing, no directory getting comment-heavier. The numbers live in sizeBudget.json and
// only ever go DOWN — a refactor that splits a file lowers its entry (or drops it).
// Scope: .js under extension/src and extension/tests. Paths are repo-relative so every
// surface's budget reads the same.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = (() => {
  let dir = dirname(fileURLToPath(import.meta.url));
  while (!existsSync(join(dir, 'core', 'CMakeLists.txt'))) {
    const up = dirname(dir);
    assert.notEqual(up, dir, 'the repo root is above this test');
    dir = up;
  }
  return dir;
})();

const budget = JSON.parse(readFileSync(new URL('./sizeBudget.json', import.meta.url), 'utf8'));
const SCOPE = ['extension/src', 'extension/tests'];
const SKIP_DIRS = new Set(['node_modules', 'build', 'dist', 'third_party']);

const walk = (dir, out = []) => {
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    if (entry.name.startsWith('.')) continue;
    const path = join(dir, entry.name);
    if (entry.isDirectory()) { if (!SKIP_DIRS.has(entry.name)) walk(path, out); }
    else if (entry.name.endsWith('.js')) out.push(path);
  }
  return out;
};

// Total lines, plus lines that are only a comment: a line whose first non-whitespace opens
// a comment, and every line inside a block comment. The char scan tracks strings so a
// `//` or `/*` inside a literal doesn't count (` spans lines, ' and " don't) — and
// regex literals are consumed whole, or a backtick inside one (/[{`]/) reads as a template
// opening and every comment to the end of the file is silently skipped.
const REGEX_AFTER = /[(,=:[!&|?{};+\-*%^~<>]\s*$|\b(?:return|typeof|case|in|of|new|do|else|void|delete|instanceof|yield|await)\s*$/;
const endOfRegex = (line, i) => {
  for (let j = i + 1, klass = false; j < line.length; j++) {
    if (line[j] === '\\') { j++; continue; }
    if (klass) { if (line[j] === ']') klass = false; continue; }
    if (line[j] === '[') { klass = true; continue; }
    if (line[j] === '/') return j;
  }
  return -1;
};
const measure = (src) => {
  const lines = src.split('\n');
  if (lines.length && lines[lines.length - 1] === '') lines.pop();
  let comment = 0, inBlock = false, inTemplate = false;
  for (const line of lines) {
    const head = line.trimStart();
    const isComment = inBlock || (!inTemplate && (head.startsWith('//') || head.startsWith('/*')));
    let quote = inTemplate ? '`' : '';
    for (let i = 0; i < line.length; i++) {
      const c = line[i], next = line[i + 1];
      if (inBlock) { if (c === '*' && next === '/') { inBlock = false; i++; } continue; }
      if (quote) {
        if (c === '\\') i++;
        else if (c === quote) quote = '';
        continue;
      }
      if (c === '/' && next === '*') { inBlock = true; i++; continue; }
      if (c === '/' && next === '/') break;
      if (c === '/' && REGEX_AFTER.test(line.slice(0, i))) {
        const end = endOfRegex(line, i);
        if (end > 0) { i = end; continue; }
      }
      if (c === '"' || c === "'" || c === '`') quote = c;
    }
    inTemplate = quote === '`';
    if (isComment) comment++;
  }
  return { total: lines.length, comment };
};

const measured = new Map();
for (const dir of SCOPE) {
  for (const path of walk(join(repoRoot, dir))) {
    measured.set(relative(repoRoot, path), measure(readFileSync(path, 'utf8')));
  }
}
const inScope = [...measured.keys()].sort();
const excepted = (path) => Object.hasOwn(budget.exceptions, path);

test('no unlisted file is over the line cap', () => {
  const over = inScope.filter(
    (p) => !excepted(p) && !(p in budget.files) && measured.get(p).total > budget.maxNewFileLines);
  assert.deepEqual(over, [],
    `over ${budget.maxNewFileLines} lines and unlisted — split it, or record its size in `
    + 'tests/sizeBudget.json (exceptions are for generated bundles and byte-pinned ports only)');
});

test('listed files did not grow', () => {
  const grew = [], shrank = [], gone = [];
  for (const [path, cap] of Object.entries(budget.files)) {
    if (excepted(path)) continue;
    const found = measured.get(path);
    if (!found) { gone.push(path); continue; }
    if (found.total > cap) grew.push(`${path}: ${found.total} > ${cap}`);
    else if (found.total < cap * 0.9) shrank.push(`${path}: ${cap} → ${found.total}`);
  }
  for (const note of shrank) console.log(`  ratchet down: ${note}`);
  for (const note of gone) console.log(`  stale entry (file is gone): ${note}`);
  assert.deepEqual(grew, [], 'listed files may only shrink — split the growth out instead');
});

test('comment share per directory did not rise', () => {
  const risen = [];
  for (const [dir, cap] of Object.entries(budget.commentPct)) {
    let total = 0, comment = 0;
    for (const path of inScope) {
      if (dirname(path) !== dir) continue;
      total += measured.get(path).total;
      comment += measured.get(path).comment;
    }
    if (!total) continue;
    const pct = Math.floor(comment * 100 / total);
    if (pct > cap) risen.push(`${dir}: ${pct}% > ${cap}%`);
  }
  assert.deepEqual(risen, [], 'comments outgrew the code — trim the prose, keep the code');
});
