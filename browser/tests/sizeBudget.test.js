// Size + comment ratchet for browser/ (.js under js/, tests/, tools/).
// Nothing new may land over maxNewFileLines, nothing listed may grow, and no
// directory's comment share may rise. Budget lives in sizeBudget.json.
import test from 'node:test';
import assert from 'node:assert';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

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
const SCAN = ['browser/js', 'browser/tests', 'browser/tools'];
const SKIP_DIRS = new Set(['node_modules', 'wasm']); // js/wasm/ is generated
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

// Total lines + comment lines. A line counts as a comment when it opens with // or /*,
// or sits inside a block comment. The scanner tracks strings so a // or /* inside a
// literal opens nothing; ' and " cannot span lines, ` can.
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
