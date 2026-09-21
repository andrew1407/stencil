// Import-direction lint for browser-extension/src (.claude/rules/architecture.md): lib/ → config/ →
// llm/ → background/ → content/ → popup, options, crop, devtools. A layer may import from
// everything to its left and nothing to its right; today's crossings are a frozen allowance.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdirSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, relative, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const SRC = resolve(dirname(fileURLToPath(import.meta.url)), '../src');
const LAYERS = [['lib'], ['config'], ['llm'], ['background'], ['content'],
  ['popup', 'options', 'crop', 'devtools']];
const RANK = new Map(LAYERS.flatMap((names, i) => names.map((n) => [n, i])));
const TOP = new Set(LAYERS[LAYERS.length - 1]);

// Shrink this list; never add to it. Each entry is "file → target" as reported below.
const ALLOWANCE = new Map([
  ['lib/dust/flight.js → config/motion.json',
    'lib reads a config table; the rule puts config/ right of lib/ (browser has it leftmost). The'
    + ' cloud family funnels it through this one file, so dustGrain/dustCloud take TUNE from here'],
  ['lib/image/videoFrames.js → llm/chatController.js',
    'MAX_IMAGE_EDGE lives in llm/controller.js; it belongs in a lib/ constants module'],
]);

const IMPORT = /^\s*(?:import|export)\b[^'"]*?\bfrom\s*['"]([^'"]+)['"]|^\s*import\s*['"]([^'"]+)['"]|\bimport\(\s*['"]([^'"]+)['"]\s*\)/gm;

const walk = (dir, out = []) => {
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const path = join(dir, entry.name);
    if (entry.isDirectory()) walk(path, out);
    else if (entry.name.endsWith('.js')) out.push(path);
  }
  return out;
};

// Every relative import in `root`, as { from, to, edge } with layer names and "a → b" text.
export const scanEdges = (root) => {
  const edges = [];
  for (const file of walk(root)) {
    const rel = relative(root, file);
    const source = readFileSync(file, 'utf8');
    for (const m of source.matchAll(IMPORT)) {
      const spec = m[1] ?? m[2] ?? m[3];
      if (!spec.startsWith('.')) continue;
      const target = relative(root, resolve(dirname(file), spec));
      const from = rel.split('/')[0], to = target.split('/')[0];
      if (from !== to) edges.push({ from, to, edge: `${rel} → ${target}` });
    }
  }
  return edges;
};

export const findViolations = (edges) => {
  const rightward = [], sibling = [], unranked = [];
  for (const { from, to, edge } of edges) {
    if (!RANK.has(from) || !RANK.has(to)) { unranked.push(edge); continue; }
    if (RANK.get(to) > RANK.get(from)) rightward.push(edge);
    else if (TOP.has(from) && TOP.has(to)) sibling.push(edge);
  }
  return { rightward, sibling, unranked };
};

const edges = scanEdges(SRC);
const { rightward, sibling, unranked } = findViolations(edges);

test('the scan saw the whole tree', () => {
  assert.ok(walk(SRC).length > 150, 'browser-extension/src was read');
  assert.ok(edges.length > 100, 'cross-directory imports were found');
  assert.ok(edges.some((e) => e.from === 'popup' && e.to === 'lib'), 'popup → lib is indexed');
  assert.ok(edges.some((e) => e.from === 'llm' && e.to === 'config'), 'llm → config is indexed');
});

test('every src/ directory that imports or is imported has a layer', () => {
  assert.deepEqual(unranked, []);
});

test('no file imports from a layer to its right', () => {
  const unlisted = rightward.filter((e) => !ALLOWANCE.has(e));
  assert.deepEqual(unlisted, [], 'a new right-pointing import; move the code left instead');
});

test('the top-level pages do not import each other', () => {
  assert.deepEqual(sibling, [], 'popup, options, crop and devtools share code via lib/');
});

test('every allowance is still needed', () => {
  const seen = new Set(rightward);
  const stale = [...ALLOWANCE.keys()].filter((e) => !seen.has(e));
  assert.deepEqual(stale, [], 'the violation is gone; delete its allowance');
});

test('manifest entry points live in their layer', () => {
  const manifest = JSON.parse(readFileSync(resolve(SRC, '../manifest.json'), 'utf8'));
  assert.match(manifest.background.service_worker, /^src\/background\//);
  for (const cs of manifest.content_scripts) {
    for (const js of cs.js) assert.match(js, /^src\/content\//);
  }
});

test('an injected right-pointing import is caught', () => {
  const root = mkdtempSync(join(tmpdir(), 'stencil-layers-'));
  try {
    for (const dir of ['lib', 'llm', 'popup', 'options']) mkdirSync(join(root, dir));
    writeFileSync(join(root, 'lib/a.js'), "import { x } from '../llm/b.js';\nexport const y = x;\n");
    writeFileSync(join(root, 'llm/b.js'), "export * from '../lib/a.js';\nexport const x = 1;\n");
    writeFileSync(join(root, 'popup/p.js'), "const m = await import('../options/o.js');\n");
    writeFileSync(join(root, 'options/o.js'), "import '../lib/a.js';\n");
    const found = findViolations(scanEdges(root));
    assert.deepEqual(found.rightward, ['lib/a.js → llm/b.js']);
    assert.deepEqual(found.sibling, ['popup/p.js → options/o.js']);
    assert.deepEqual(found.unranked, []);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});
