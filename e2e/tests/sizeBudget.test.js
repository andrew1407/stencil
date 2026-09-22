// Size + comment ratchet for e2e/ (.js under helpers/, tests/ and the config), the twin of
// browser/tests/sizeBudget.test.js. Nothing new may land over maxNewFileLines, nothing listed
// may grow, and no directory's comment share may rise. Budget lives in sizeBudget.json.
import test from 'node:test';
import assert from 'node:assert';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import config from '../playwright.config.js';

const HERE = path.dirname(fileURLToPath(import.meta.url));

// Walk up to the repo root so every recorded path reads the same from anywhere.
const repoRoot = () => {
  let dir = HERE;
  while (!fs.existsSync(path.join(dir, '.git'))) {
    const up = path.dirname(dir);
    assert.notStrictEqual(up, dir, 'repo root not found above ' + HERE);
    dir = up;
  }
  return dir;
};

const ROOT = repoRoot();
const SCAN = ['e2e'];
const SKIP_DIRS = new Set(['node_modules', 'test-results', 'playwright-report', 'blob-report', '.playwright']);
const budget = JSON.parse(fs.readFileSync(path.join(HERE, 'sizeBudget.json'), 'utf8'));
const MAX = budget.maxNewFileLines;

const walk = (rel, out = []) => {
  const entries = fs.readdirSync(path.join(ROOT, rel), { withFileTypes: true });
  for (const e of entries.sort((a, b) => (a.name < b.name ? -1 : 1))) {
    const child = `${rel}/${e.name}`;
    if (e.isDirectory()) { if (!SKIP_DIRS.has(e.name)) walk(child, out); }
    else if (e.name.endsWith('.js')) out.push(child);
  }
  return out;
};

// A line counts as a comment when it opens with // or /*, or sits inside a block comment; the scanner tracks
// strings, so a // or /* inside a literal opens nothing (' and " cannot span lines, ` can).
const measure = (rel) => {
  const lines = fs.readFileSync(path.join(ROOT, rel), 'utf8').split('\n');
  if (lines.length && lines[lines.length - 1] === '') lines.pop();
  let block = false, tmpl = false, comments = 0;
  for (const line of lines) {
    const trimmed = line.trimStart();
    if (block || (!tmpl && (trimmed.startsWith('//') || trimmed.startsWith('/*')))) comments++;
    let quote = tmpl ? '`' : '';
    for (let i = 0; i < line.length; i++) {
      const c = line[i], n = line[i + 1];
      if (block) { if (c === '*' && n === '/') { block = false; i++; } continue; }
      if (quote) { if (c === '\\') i++; else if (c === quote) quote = ''; continue; }
      if (c === '/' && n === '/') break;
      if (c === '/' && n === '*') { block = true; i++; continue; }
      if (c === '"' || c === "'" || c === '`') quote = c;
    }
    tmpl = quote === '`';
  }
  return { total: lines.length, comments };
};

const files = SCAN.flatMap((rel) => walk(rel));
const sizes = new Map(files.map((rel) => [rel, measure(rel)]));

test('size budget: no new file over the cap, no listed file grew', () => {
  const appeared = [], grew = [], shrank = [];
  for (const [rel, { total }] of sizes) {
    if (rel in budget.exceptions) continue;
    const cap = budget.files[rel];
    if (cap === undefined) {
      if (total > MAX) appeared.push(`${rel} is ${total} lines (cap ${MAX})`);
      continue;
    }
    if (total > cap) grew.push(`${rel} grew to ${total} lines (budget ${cap})`);
    else if (total < cap * 0.9) shrank.push(`${rel}: ${cap} → ${total}`);
  }
  if (shrank.length) console.log(`  note: ratchet these down in sizeBudget.json —\n    ${shrank.join('\n    ')}`);
  assert.deepStrictEqual(appeared, [], 'new oversized files must be split, or recorded in sizeBudget.json');
  assert.deepStrictEqual(grew, [], 'listed files may shrink, never grow');
});

test('size budget: every recorded path still exists and is in scope', () => {
  const stale = [...Object.keys(budget.files), ...Object.keys(budget.exceptions)].filter((rel) => !sizes.has(rel));
  assert.deepStrictEqual(stale, [], 'drop these from sizeBudget.json');
  for (const [rel, why] of Object.entries(budget.exceptions)) assert.ok(why, `exception ${rel} needs a reason`);
});

const stemsPerDir = () => {
  const dirs = new Map();
  for (const rel of sizes.keys()) {
    const dir = path.posix.dirname(rel);
    if (!dirs.has(dir)) dirs.set(dir, new Set());
    dirs.get(dir).add(path.posix.basename(rel).slice(0, -3));
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
  const dirs = new Map();
  for (const [rel, { total, comments }] of sizes) {
    const dir = path.posix.dirname(rel);
    const acc = dirs.get(dir) || { total: 0, comments: 0 };
    acc.total += total; acc.comments += comments;
    dirs.set(dir, acc);
  }
  const over = [];
  for (const [dir, cap] of Object.entries(budget.commentPct)) {
    const acc = dirs.get(dir);
    assert.ok(acc, `${dir} is in sizeBudget.json but holds no .js files`);
    const pct = Math.floor(acc.comments * 100 / acc.total);
    if (pct > cap) over.push(`${dir} is ${pct}% comments (budget ${cap}%)`);
  }
  assert.deepStrictEqual(over, [], 'trim the prose, or split the code out of these dirs');
});

// Test-count floor: Playwright can only be asked what it runs by running it, so this counts
// `test(` declarations instead — it catches a deleted spec, not an unexecuted one.
const TEST_FLOOR = 260;
const DECL = /^\s*test(?:\.\w+)*\s*\(/gm;

test('test-count floor: the spec tree still declares its cases', () => {
  let count = 0;
  for (const rel of sizes.keys()) {
    if (!rel.endsWith('.spec.js')) continue;
    count += (fs.readFileSync(path.join(ROOT, rel), 'utf8').match(DECL) || []).length;
  }
  assert.ok(count >= TEST_FLOOR, `e2e suite collapsed to ${count} declared tests, floor is ${TEST_FLOOR}`);
});

// The runner's own blind spot: every project sets its own testMatch, so a spec in a directory
// no project names is collected by nothing and reports neither pass nor skip.
test('every spec is claimed by a playwright project', () => {
  const orphans = [...sizes.keys()].filter((rel) => rel.endsWith('.spec.js'))
    .filter((rel) => !config.projects.some((p) => p.testMatch.test(rel.slice('e2e/'.length))));
  assert.deepStrictEqual(orphans, [], 'add a project testMatch in playwright.config.js, or move the spec');
});
