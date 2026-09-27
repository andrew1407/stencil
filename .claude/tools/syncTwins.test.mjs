// .claude/tools/twins.json against the parity tests that enforce it: every pair in step, and the manifest
// naming exactly the pairs those tests read (their tables are parsed, never run). Run:
//   node --test .claude/tools/syncTwins.test.mjs
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdirSync, mkdtempSync, readdirSync, readFileSync, writeFileSync, existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { hookNote, itemsOf, loadManifest, normalizePort, regeneratePort } from './syncTwins.mjs';

const ROOT = fileURLToPath(new URL('../..', import.meta.url));
const src = (rel) => readFileSync(path.join(ROOT, rel), 'utf8');
const rel = (from, spec) => path.posix.normalize(path.posix.join(path.posix.dirname(from), spec));
const between = (text, open, close = '\n];') => text.slice(text.indexOf(open), text.indexOf(close, text.indexOf(open)));
const manifest = loadManifest();
const key = (r) => `${r.from} -> ${r.to}`;

// ── the pair tables, read out of each parity test ───────────────
const PORT = 'browser-extension/tests/portParity.test.js';
const DATA = 'browser-extension/tests/dataParity.test.js';
const PARSER = 'vscode-extension/tests/parserParity.test.js';
const PY = 'pystencil/tests/test_canonical_drift.py';

const portRows = () => [...between(src(PORT), 'const MANIFEST = [').matchAll(/\['(\w+)', '([^']+)', '([^']+)'\]/g)]
  .map(([, name, a, b]) => ({ name, from: rel(PORT, a), to: rel(PORT, b) }));

const functionRows = () => {
  const port = [...between(src(PORT), 'const FUNCTIONS = [').matchAll(/\['(\w+)', '([^']+)', '([^']+)',\s*\[([^\]]*)\]\]/g)]
    .map(([, name, a, b, list]) => ({ name, from: rel(PORT, a), to: rel(PORT, b), names: [...list.matchAll(/'(\w+)'/g)].map((m) => m[1]) }));
  const t = src(PARSER);
  const dir = (v) => rel(PARSER, new RegExp(`const ${v} = fileURLToPath\\(new URL\\('([^']+)'`).exec(t)[1]);
  const index = { name: 'parserIndex', from: dir('CORE') + /\$\{CORE\}(\w+\.js)/.exec(t)[1],
    to: dir('PARSER') + /\$\{PARSER\}(\w+\.js)/.exec(t)[1],
    names: [.../for \(const name of \[([^\]]+)\]\)/.exec(t)[1].matchAll(/'(\w+)'/g)].map((m) => m[1]) };
  return [...port, index];
};

const goCopies = () => {
  const walk = (d) => readdirSync(path.join(ROOT, d), { withFileTypes: true })
    .flatMap((e) => (e.isDirectory() ? walk(`${d}/${e.name}`) : e.name.endsWith('_test.go') ? [`${d}/${e.name}`] : []));
  return walk('server').filter((f) => src(f).includes('"browser", "js", "config"')).map((f) => {
    const t = src(f);
    return { from: [...(/"browser"((?:, "[^"]+")+)\)/.exec(t)[1].matchAll(/"([^"]+)"/g))].map((m) => m[1]).join('/').replace(/^/, 'browser/'),
      to: `${path.posix.dirname(f)}/${/(assets\/[\w.]+) differs/.exec(t)[1]}` };
  });
};

const pyCopies = () => {
  const t = src(PY);
  const segs = (s) => [...s.matchAll(/"([^"]+)"/g)].map((m) => m[1]).join('/');
  const canon = Object.fromEntries([...t.matchAll(/_CANON_(\w+) = _CONSTANTS\.parent((?: \/ "[^"]+")+)/g)].map(([, n, s]) => [n, `browser/js/config/${segs(s)}`]));
  return [...t.matchAll(/_DATA_(\w+) = _PKG_ROOT((?: \/ "[^"]+")+)/g)].map(([, n, s]) => ({ from: canon[n], to: `pystencil/${segs(s)}` }));
};

const dataCopies = () => {
  const t = src(DATA);
  const copies = [...t.matchAll(/extension: \(\) => canonical\('([^']+)'\),\s*canonical: \(\) => canonical\('([^']+)'\)/g)]
    .map(([, ext, canon]) => ({ from: rel(DATA, canon), to: rel(DATA, ext) }));
  const picks = [...t.matchAll(/extension: \(\) => canonical\('([^']+)'\),\s*canonical: \(\) => \{\s*const \{ ([\w, ]+) \} = canonical\('([^']+)'\)/g)]
    .map(([, ext, keys, canon]) => ({ from: rel(DATA, canon), to: rel(DATA, ext), keys: keys.split(/,\s*/) }));
  return { copies, picks };
};

const parserCopies = () => {
  const t = src(PARSER);
  const tree = { from: rel(PARSER, /const BROWSER = fileURLToPath\(new URL\('([^']+)'/.exec(t)[1]),
    to: rel(PARSER, /const COPIES = fileURLToPath\(new URL\('([^']+)'/.exec(t)[1]) };
  const data = [...between(t, 'const DATA = [', '];').matchAll(/\['[\w.]+', '([^']+)', '([^']+)'\]/g)]
    .map(([, a, b]) => ({ from: rel(PARSER, a), to: rel(PARSER, b) }));
  return { tree, data };
};

// ── the manifest is the union of those tables, no more and no less ──
test('every copy and pick the drift tests pin is in the manifest, and nothing else', () => {
  const d = dataCopies();
  const tested = [...d.copies, ...pyCopies(), ...goCopies(), ...parserCopies().data].map(key).sort();
  assert.ok(tested.length >= 10, `only ${tested.length} copies parsed — a table's shape changed`);
  assert.deepEqual(manifest.copies.map(key).sort(), tested);
  assert.deepEqual(manifest.picks.map((r) => ({ from: r.from, to: r.to, keys: r.keys })), d.picks);
});

test('the copied parser tree is the one parserParity pins', () => {
  assert.deepEqual(manifest.trees.map(key), [key(parserCopies().tree)]);
});

test('every whole-file port and per-function port portParity pins is in the manifest', () => {
  assert.deepEqual(manifest.ports.map(({ name, from, to }) => ({ name, from, to })), portRows());
  assert.deepEqual(manifest.functions.map(({ name, from, to, names }) => ({ name, from, to, names })), functionRows());
});

test('every pair in the manifest is in step now', () => {
  const drifted = itemsOf(manifest).map((i) => [i.label, i.drift()]).filter(([, d]) => d.length);
  assert.deepEqual(drifted, [], 'run `node .claude/tools/syncTwins.mjs` to re-copy from the originals');
});

// ── the sync itself, on a scratch tree ──────────────────────────
test('a port keeps its header and its own import spellings; the body follows the original', () => {
  const original = '// browser header\nimport { a } from \'../../utils/a.js\';\nexport const x = a + 2;\n';
  const copy = '// extension header\n// second line\n\nimport { a } from \'../a.js\';\nexport const x = a + 1;\n';
  const { text, missing } = regeneratePort(original, copy);
  assert.deepEqual(missing, []);
  assert.equal(text, '// extension header\n// second line\n\nimport { a } from \'../a.js\';\nexport const x = a + 2;\n');
  assert.equal(normalizePort(text), normalizePort(original));
  assert.deepEqual(regeneratePort('import { b } from \'./b.js\';\n', copy).missing, ['./b.js']);
});

test('sync brings every kind of row back in step, and --check sees it first', () => {
  const root = mkdtempSync(path.join(tmpdir(), 'syncTwins-'));
  const put = (p, t) => { mkdirSync(path.dirname(path.join(root, p)), { recursive: true }); writeFileSync(path.join(root, p), t); };
  put('a/c.json', '{"k":1}\n'); put('b/c.json', '{"k":0}\n');
  put('a/p.json', '{"x":"1","y":"2"}\n'); put('b/p.json', '{}\n');
  put('a/t/one.js', 'one\n'); put('b/t/one.js', 'old\n'); put('b/t/gone.js', 'orphan\n');
  put('a/f.js', 'export const f = () => {\n  return 2;\n};\n');
  put('b/f.js', '// mine\nexport const f = () => {\n  return 1;\n};\nexport const g = 0;\n');
  const m = { copies: [{ from: 'a/c.json', to: 'b/c.json' }], picks: [{ from: 'a/p.json', to: 'b/p.json', keys: ['y'] }],
    trees: [{ from: 'a/t/', to: 'b/t/', ext: ['.js'] }], ports: [],
    functions: [{ name: 'f', from: 'a/f.js', to: 'b/f.js', names: ['f'] }] };
  const items = itemsOf(m, root);
  assert.equal(items.filter((i) => i.drift().length).length, 4);
  items.forEach((i) => i.sync());
  assert.deepEqual(items.map((i) => i.drift()), [[], [], [], []]);
  assert.equal(readFileSync(path.join(root, 'b/p.json'), 'utf8'), '{\n  "y": "2"\n}\n');
  assert.equal(existsSync(path.join(root, 'b/t/gone.js')), false);
  assert.match(readFileSync(path.join(root, 'b/f.js'), 'utf8'), /^\/\/ mine\n[\s\S]*return 2;[\s\S]*export const g = 0;\n$/);
});

test('the PostToolUse note names a drifted original, a hand-edited copy, and stays quiet otherwise', () => {
  const root = mkdtempSync(path.join(tmpdir(), 'syncTwinsHook-'));
  const put = (p, t) => { mkdirSync(path.dirname(path.join(root, p)), { recursive: true }); writeFileSync(path.join(root, p), t); };
  put('.claude/tools/twins.json', JSON.stringify({ copies: [{ from: 'a/c.json', to: 'b/c.json' }], picks: [], trees: [], ports: [], functions: [] }));
  put('a/c.json', '1\n'); put('b/c.json', '1\n'); put('a/other.js', '');
  const note = (file) => hookNote({ tool_input: { file_path: file } }, root);
  assert.equal(note(path.join(root, 'a/c.json')), null);
  put('a/c.json', '2\n');
  assert.match(note(path.join(root, 'a/c.json')), /^a\/c\.json has twins out of step \(b\/c\.json\)/);
  assert.match(note('b/c.json'), /^b\/c\.json is a copy of a\/c\.json/);
  assert.equal(note(path.join(root, 'a/other.js')), null);
  assert.equal(note('/elsewhere/x.js'), null);
  assert.equal(hookNote({}, root), null);
});
