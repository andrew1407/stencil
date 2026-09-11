// The .d.ts shape files beside src/ modules: what a cross-context payload actually carries,
// written down where an editor (and a reader) can find it. Nothing builds or emits from
// them — jsconfig.json is editor configuration only — so this is what keeps them honest:
// a declaration that names an export the module no longer has is drift, not documentation.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const SRC = fileURLToPath(new URL('../src/', import.meta.url));
const read = (p) => readFileSync(p, 'utf8');

const walk = (dir, out = []) => {
  for (const e of readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p, out);
    else if (e.name.endsWith('.d.ts')) out.push(p);
  }
  return out;
};
const FILES = walk(SRC).sort();
const rel = (p) => path.relative(SRC, p);

// The modules that carry a shape file. Pinned so adding one is a deliberate act — and so
// a rename cannot quietly drop a contract from the documented set.
const DOCUMENTED = [
  'content/editorApiMain.d.ts',
  'lib/editorTabs.d.ts',
  'lib/imageScan.d.ts',
  'lib/messages.d.ts',
  'llm/chatController.d.ts',
  'llm/llmClient.d.ts',
  'llm/llmSettings.d.ts',
  'llm/llmSurface.d.ts',
  'llm/opPlan.d.ts',
  'llm/opSchema.d.ts',
];

test('the documented set is exactly the pinned list', () => {
  assert.deepEqual(FILES.map(rel), DOCUMENTED);
});

// `export declare const|function|class X` — the names a .d.ts claims the module exports.
const declaredValues = (src) =>
  [...src.matchAll(/^export declare (?:const|function|class) (\w+)/gm)].map((m) => m[1]);
// `export interface|type X` — documentation that emits nothing.
const declaredTypes = (src) =>
  [...src.matchAll(/^export (?:interface|type) (\w+)/gm)].map((m) => m[1]);

// What a module actually exports: direct declarations, re-exports, and `export {…}` lists.
const moduleExports = (src) => {
  const names = new Set();
  for (const m of src.matchAll(/^export (?:const|let|function|async function|class) (\w+)/gm)) names.add(m[1]);
  for (const m of src.matchAll(/^export\s*\{([^}]*)\}/gm)) {
    for (const part of m[1].split(',')) {
      const as = part.trim().split(/\s+as\s+/);
      if (as.length && as[as.length - 1]) names.add(as[as.length - 1].trim());
    }
  }
  return names;
};

for (const file of FILES) {
  const name = rel(file);
  const src = read(file);

  test(`${name}: stands beside its module and declares real shapes`, () => {
    const js = file.replace(/\.d\.ts$/, '.js');
    assert.ok(existsSync(js), `${name} documents no module — ${path.basename(js)} is gone`);
    assert.ok(declaredTypes(src).length + declaredValues(src).length,
      `${name} declares nothing — it documents nothing`);
    // A shape file declares; it never assigns, and so ships no behaviour.
    assert.ok(!/^export declare .*=[^>]/m.test(src),
      `${name} assigns a value — a .d.ts only declares`);
  });

  test(`${name}: every value it declares is still exported`, () => {
    const js = file.replace(/\.d\.ts$/, '.js');
    if (!existsSync(js)) return;
    const actual = moduleExports(read(js));
    const stale = declaredValues(src).filter((n) => !actual.has(n));
    assert.deepEqual(stale, [], `${name} declares exports the module no longer has`);
  });

  test(`${name}: every type it imports resolves to a shape file`, () => {
    for (const m of src.matchAll(/^import type .*? from '([^']+)'/gm)) {
      const spec = m[1];
      assert.match(spec, /^\.{1,2}\//, `${name}: "${spec}" is not a relative import`);
      const target = path.resolve(path.dirname(file), spec.replace(/\.js$/, '.d.ts'));
      assert.ok(existsSync(target), `${name}: "${spec}" has no shape file`);
    }
  });
}

test('jsconfig.json is editor configuration only, and reads the shape files', () => {
  const cfg = JSON.parse(read(fileURLToPath(new URL('../jsconfig.json', import.meta.url))));
  assert.equal(cfg.compilerOptions.noEmit, true, 'nothing may be emitted — this repo has no build step');
  assert.equal(cfg.compilerOptions.checkJs, false, 'the .js is not type-checked; the shapes are documentation');
  assert.equal(cfg.compilerOptions.allowJs, true);
  assert.ok(cfg.include.includes('src/**/*.d.ts'), 'the shape files must be in scope');
  assert.ok(cfg.include.includes('src/**/*.js'));
});
