// The size + comment ratchet for the extension: no new oversized module, no listed file
// growing, no directory getting comment-heavier. The numbers live in sizeBudget.json and
// only ever go DOWN — a refactor that splits a file lowers its entry (or drops it).
// Scope: .js under browser-extension/src and browser-extension/tests. Paths are repo-relative so every
// surface's budget reads the same.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

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
const SCOPE = ['browser-extension/src', 'browser-extension/tests'];
const SKIP_DIRS = new Set(['node_modules', 'build', 'dist', 'third_party']);

const walk = (dir, out = []) => {
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    if (entry.name.startsWith('.')) continue;
    const path = join(dir, entry.name);
    if (entry.isDirectory()) { if (!SKIP_DIRS.has(entry.name)) walk(path, out); }
    // .d.ts too: a shape file is source, and the cap has to reach it.
    else if (entry.name.endsWith('.js') || entry.name.endsWith('.d.ts')) out.push(path);
  }
  return out;
};

// The char scan tracks strings so a `//` or `/*` inside a literal doesn't count (` spans lines,
// ' and " don't), and regex literals are consumed whole — a backtick in one would swallow the file.
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

// A module and its .d.ts are one thing to the reader, so they count once.
const stemsPerDir = () => {
  const dirs = new Map();
  for (const rel of measured.keys()) {
    const at = rel.lastIndexOf('/');
    const dir = rel.slice(0, at);
    const base = rel.slice(at + 1);
    const stem = base.endsWith('.d.ts') ? base.slice(0, -5) : base.slice(0, -3);
    if (!dirs.has(dir)) dirs.set(dir, new Set());
    dirs.get(dir).add(stem);
  }
  return dirs;
};

test('folder fan-out: no directory over the cap, no listed directory grew', () => {
  const cap = budget.maxFilesPerDir;
  const appeared = [], grew = [], shrank = [];
  for (const [dir, stems] of stemsPerDir()) {
    const n = stems.size;
    const allowed = budget.dirs[dir];
    if (allowed === undefined) {
      if (n > cap) appeared.push(`${dir} holds ${n} modules (cap ${cap}) — split it by feature`);
      continue;
    }
    if (n > allowed) grew.push(`${dir} grew to ${n} modules (budget ${allowed})`);
    else if (n < allowed) shrank.push(`'${dir}': ${n}`);
  }
  const gone = Object.keys(budget.dirs).filter((d) => !stemsPerDir().has(d));
  if (shrank.length) console.log(`  note: lower these in sizeBudget.json —\n    ${shrank.join('\n    ')}`);
  assert.deepStrictEqual(appeared, [], 'a new folder over the cap must be split, never recorded');
  assert.deepStrictEqual(grew, [], 'listed folders may shrink, never grow');
  assert.deepStrictEqual(gone, [], 'drop these from sizeBudget.json — the folder is gone');
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

// The floor sits ~3% under today's count — raise it when the suite grows a lot.
// STENCIL_TEST_COUNT_RUN marks the inner run, so it never nests.
const TEST_FLOOR = 1580;
const INNER_RUN = 'STENCIL_TEST_COUNT_RUN';

test('test-count floor: the suite still discovers and runs its whole tree',
  { skip: process.env[INNER_RUN] ? 'inner count run' : false }, () => {
    // NODE_TEST_CONTEXT is this process's own runner marker; inherited, it mutes the inner run.
    const innerEnv = { ...process.env, [INNER_RUN]: '1' };
    delete innerEnv.NODE_TEST_CONTEXT;
    const inner = spawnSync(process.execPath, ['--test', '--test-reporter=tap'], {
      cwd: fileURLToPath(new URL('../', import.meta.url)), encoding: 'utf8',
      maxBuffer: 256 * 1024 * 1024, env: innerEnv,
    });
    const total = /^# tests (\d+)$/m.exec(inner.stdout || '');
    assert.ok(total, `the inner runner printed no test total (exit ${inner.status})`);
    const count = Number(total[1]);
    assert.ok(count >= TEST_FLOOR,
      `browser-extension suite collapsed to ${count} tests, floor is ${TEST_FLOOR}`);
  });
