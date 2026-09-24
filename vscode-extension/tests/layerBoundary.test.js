// Import-direction lint for vscode-extension/src (.claude/rules/architecture.md):
// config/ → parser/ → lib/ → src/*.js → extension.js. A layer may import from everything to
// its left and nothing to its right; extension.js is the only root file that imports a
// sibling root file, because it is the wiring.
import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, relative, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const SRC = resolve(dirname(fileURLToPath(import.meta.url)), '../src');
const ROOT = '';   // src/*.js — the feature modules and the entry point
const LAYERS = [['config'], ['parser'], ['lib'], [ROOT]];
const RANK = new Map(LAYERS.flatMap((names, i) => names.map((n) => [n, i])));
const ENTRY = 'extension.js';

const IMPORT = /^\s*(?:import|export)\b[^'"]*?\bfrom\s*['"]([^'"]+)['"]|^\s*import\s*['"]([^'"]+)['"]|\bimport\(\s*['"]([^'"]+)['"]\s*\)/gm;

const walk = (dir, out = []) => {
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const path = join(dir, entry.name);
    if (entry.isDirectory()) walk(path, out);
    else if (entry.name.endsWith('.js')) out.push(path);
  }
  return out;
};

// The layer a repo-relative path belongs to: its first directory, or ROOT for a src/*.js.
const layerOf = (rel) => (rel.includes('/') ? rel.split('/')[0] : ROOT);

export const scanEdges = (root) => {
  const edges = [];
  for (const file of walk(root)) {
    const rel = relative(root, file);
    for (const m of readFileSync(file, 'utf8').matchAll(IMPORT)) {
      const spec = m[1] ?? m[2] ?? m[3];
      if (!spec.startsWith('.')) continue;
      const target = relative(root, resolve(dirname(file), spec));
      edges.push({ from: layerOf(rel), to: layerOf(target), file: rel, edge: `${rel} → ${target}` });
    }
  }
  return edges;
};

export const findViolations = (edges) => {
  const rightward = [], sibling = [], unranked = [];
  for (const { from, to, file, edge } of edges) {
    if (from === to && from !== ROOT) continue;
    if (!RANK.has(from) || !RANK.has(to)) { unranked.push(edge); continue; }
    if (RANK.get(to) > RANK.get(from)) rightward.push(edge);
    else if (from === ROOT && to === ROOT && file !== ENTRY) sibling.push(edge);
  }
  return { rightward, sibling, unranked };
};

const edges = scanEdges(SRC);
const { rightward, sibling, unranked } = findViolations(edges);

test('the scan saw the whole tree', () => {
  assert.ok(walk(SRC).length >= 18, 'vscode-extension/src was read');
  assert.ok(edges.some((e) => e.edge.startsWith('extension.js →')), 'the entry point is indexed');
  assert.ok(edges.some((e) => e.from === 'lib' && e.to === 'parser'), 'lib → parser is indexed');
});

test('every src/ directory that imports or is imported has a layer', () => {
  assert.deepEqual(unranked, []);
});

test('no file imports from a layer to its right', () => {
  assert.deepEqual(rightward, [], 'a right-pointing import; move the code left instead');
});

test('extension.js is the only root file that imports a sibling root file', () => {
  assert.deepEqual(sibling, [], 'a feature module reached across; wire it in extension.js');
});

test('the parser copies import nothing outside config/ and themselves', () => {
  const strays = edges.filter((e) => e.from === 'parser' && !['parser', 'config'].includes(e.to));
  assert.deepEqual(strays.map((e) => e.edge), [], 'a copy would stop being byte-equal');
});

test('only lib/parserHost.js reaches the parser copies', () => {
  const reachers = [...new Set(edges.filter((e) => e.to === 'parser' && e.from !== 'parser')
    .map((e) => e.file))];
  assert.deepEqual(reachers, ['lib/parserHost.js'], 'one memoized import(), not many');
});

test('an injected right-pointing import is caught', () => {
  const root = mkdtempSync(join(tmpdir(), 'stencil-vsce-layers-'));
  try {
    for (const dir of ['parser', 'lib']) mkdirSync(join(root, dir));
    writeFileSync(join(root, 'parser/a.js'), "import { x } from '../lib/b.js';\nexport const y = x;\n");
    writeFileSync(join(root, 'lib/b.js'), "export const x = 1;\n");
    writeFileSync(join(root, 'diagnostics.js'), "import * as c from './commands.js';\n");
    writeFileSync(join(root, 'commands.js'), "export {};\n");
    writeFileSync(join(root, 'extension.js'), "import './diagnostics.js';\n");
    const found = findViolations(scanEdges(root));
    assert.deepEqual(found.rightward, ['parser/a.js → lib/b.js']);
    assert.deepEqual(found.sibling, ['diagnostics.js → commands.js']);
    assert.deepEqual(found.unranked, []);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test('src/ is ES modules only: no require() and no module.exports', () => {
  const cjs = walk(SRC).filter((f) => /\brequire\(|\bmodule\.exports\b/.test(readFileSync(f, 'utf8')));
  assert.deepEqual(cjs.map((f) => relative(SRC, f)), []);
});
