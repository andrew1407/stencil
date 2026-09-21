// The .d.ts shape files beside js/ modules are the C++-header equivalent of each module:
// its exported surface, written where an editor and a reader can find it. Nothing builds
// or emits from them — jsconfig.json is editor configuration only — so this keeps them
// honest in both directions: a declared name the module no longer exports is drift, and
// an export the shape file omits is an undocumented contract.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const JS = fileURLToPath(new URL('../js', import.meta.url));
const read = (p) => readFileSync(p, 'utf8');

const walk = (dir, out = []) => {
  for (const e of readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) { if (e.name !== 'wasm') walk(p, out); }
    else if (e.name.endsWith('.d.ts')) out.push(p);
  }
  return out;
};
const FILES = walk(JS).sort();
const rel = (p) => path.relative(JS, p);

// Names listed in an `export { a, b as c }` clause; `type` re-exports emit nothing.
const listedNames = (clause) => clause.split(',').map((part) => {
  const words = part.trim().split(/\s+/);
  if (!words[0] || words[0] === 'type') return '';
  return words[words.length - 1];
}).filter(Boolean);

// The names a .d.ts claims the module exports: `declare` is implicit inside a .d.ts, so
// both spellings count, as does a value re-export clause.
const declaredValues = (src) => {
  const names = new Set();
  for (const m of src.matchAll(/^export (?:declare )?(?:const|let|var|function|class|enum) (\w+)/gm)) names.add(m[1]);
  for (const m of src.matchAll(/^export\s*\{([^}]*)\}/gm)) for (const n of listedNames(m[1])) names.add(n);
  return names;
};
const declaredTypes = (src) =>
  [...src.matchAll(/^export (?:declare )?(?:interface|type) (\w+)/gm)].map((m) => m[1]);

// What a module actually exports: direct declarations, re-exports and `export {…}` lists.
// `export *` cannot be enumerated by text, so a barrel with one has no shape file.
const moduleExports = (src) => {
  const names = new Set();
  for (const m of src.matchAll(/^export (?:const|let|var|function|async function|class) (\w+)/gm)) names.add(m[1]);
  for (const m of src.matchAll(/^export\s*\{([^}]*)\}/gm)) for (const n of listedNames(m[1])) names.add(n);
  if (/^export default\b/m.test(src)) names.add('default');
  return names;
};

const sorted = (set) => [...set].sort();

// A shape file without a sibling module may only carry shared vocabulary (types), never
// a value claim — a declared value with no module behind it is a contract nobody keeps.
test('every shape file stands beside a module, or declares types only', () => {
  const orphans = FILES.filter((f) => !existsSync(f.replace(/\.d\.ts$/, '.js')) && declaredValues(read(f)).size).map(rel);
  assert.deepEqual(orphans, [], 'these .d.ts declare values for a module that is gone');
});

for (const file of FILES) {
  const name = rel(file);
  const src = read(file);
  const js = file.replace(/\.d\.ts$/, '.js');
  if (!existsSync(js)) continue;
  const declared = declaredValues(src);

  test(`${name}: declares real shapes and ships no behaviour`, () => {
    assert.ok(declaredTypes(src).length + declared.size, `${name} declares nothing — it documents nothing`);
    assert.ok(!/^export (?:declare )?(?:const|let|var) \w+\s*=/m.test(src), `${name} assigns a value — a .d.ts only declares`);
    assert.ok(!/\bany\b/.test(src.replace(/\/\/.*|\/\*[\s\S]*?\*\//g, '')), `${name} uses any — name the shape`);
  });

  test(`${name}: declares exactly what the module exports`, () => {
    const actual = moduleExports(read(js));
    assert.ok(!/^export \*/m.test(read(js)), `${name}: a barrel with export * cannot be checked — drop the shape file`);
    const stale = sorted(declared).filter((n) => !actual.has(n));
    const missing = sorted(actual).filter((n) => !declared.has(n));
    assert.deepEqual(stale, [], `${name} declares exports the module no longer has`);
    assert.deepEqual(missing, [], `${name} omits exports the module has`);
  });

  test(`${name}: every type it imports resolves to a shape file`, () => {
    for (const m of src.matchAll(/^import type .*? from '([^']+)'/gm)) {
      const spec = m[1];
      assert.match(spec, /^\.{1,2}\//, `${name}: "${spec}" is not a relative import`);
      const target = path.resolve(path.dirname(file), spec.replace(/\.js$/, '') + '.d.ts');
      assert.ok(existsSync(target), `${name}: "${spec}" has no shape file`);
    }
  });
}

test('jsconfig.json is editor configuration only, and reads the shape files', () => {
  const cfg = JSON.parse(read(fileURLToPath(new URL('../jsconfig.json', import.meta.url))));
  assert.equal(cfg.compilerOptions.noEmit, true, 'nothing may be emitted — this app has no build step');
  assert.equal(cfg.compilerOptions.checkJs, false, 'the .js is not type-checked; the shapes are documentation');
  assert.equal(cfg.compilerOptions.allowJs, true);
  assert.ok(cfg.include.includes('js/**/*.d.ts'), 'the shape files must be in scope');
  assert.ok(cfg.include.includes('js/**/*.js'));
});
