// The modulepreload block in index.html (tools/preloadList.js) lists the static module graph from
// js/index.js, breadth first, each module once and nothing else: a new import that was not re-listed
// fails here, before a load would discover it a depth late.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { moduleGraph, importsOf } from '../tools/graphManifest.js';
import { withBlock } from '../tools/preloadList.js';

const html = readFileSync(new URL('../index.html', import.meta.url), 'utf8');

test('index.html preloads exactly the static module graph, in breadth-first order', () => {
  assert.equal(withBlock(html), html, 'stale preload block: run `node tools/preloadList.js --write`');
  const listed = [...html.matchAll(/<link rel="modulepreload" href="([^"]+)">/g)].map((m) => m[1]);
  const { modules } = moduleGraph();
  assert.deepEqual(listed, modules);
  assert.equal(listed[0], 'js/index.js');
  assert.ok(listed.length > 400, 'the graph walk found the app');
});

test('the walk reads static imports only: comments, dynamic import() and JSON stay out of the list', () => {
  const src = [
    "import { a } from './a.js';",
    "export { b } from \"./b.js\";",
    "import './side.js';",
    "// import { x } from './commented.js';",
    "/* import y from './blocked.js'; */",
    "const lazy = () => import('./lazy.js');",
    "import data from '../c.json' with { type: 'json' };",
    "import {\n  many,\n  names,\n} from './multi.js';",
  ].join('\n');
  assert.deepEqual(importsOf(src).map(({ spec, json }) => [spec, json]), [
    ['./a.js', false], ['./b.js', false], ['./side.js', false], ['../c.json', true], ['./multi.js', false]]);
});
