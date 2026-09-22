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

const walk = (dir, ext = '.d.ts', out = []) => {
  for (const e of readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) { if (e.name !== 'wasm') walk(p, ext, out); }
    else if (e.name.endsWith(ext)) out.push(p);
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

// Rule 5 — every public module has a sibling .d.ts. Out of scope: a module that exports
// nothing documents nothing, and a barrel with `export *` may not have one (above), so
// `utils.js` and `ui/motion.js` are exempt by that rule, not frozen. The rest is the debt
// this ratchet freezes: a new module without a shape file fails, and writing one for a
// listed module means deleting its line in the same commit.
const NO_SHAPE_YET = [
  'pwa.js', 'ui/bindings/canvasPointer.js', 'ui/bindings/controls/blankColorButton.js',
  'ui/bindings/controls/formula.js', 'ui/bindings/controls/pageAndDisplay.js',
  'ui/bindings/controls/projectColorButton.js', 'ui/bindings/controls/projectNameField.js',
  'ui/bindings/controls/styleControls.js', 'ui/bindings/controls/toolbarButtons.js',
  'ui/bindings/dropPaste.js', 'ui/bindings/index.js', 'ui/bindings/keys/hotkeyActions.js',
  'ui/bindings/keys/hotkeyRules.js', 'ui/bindings/keys/keyboard.js',
  'ui/bindings/selectionPanel.js', 'ui/bindings/theme.js', 'ui/bindings/viewport/arrowPan.js',
  'ui/bindings/viewport/holdZoom.js', 'ui/bindings/viewport/scrollPersist.js',
  'ui/bindings/viewport/smoothZoom.js', 'ui/bindings/viewport/zoom.js',
  'ui/motion/control/canvasFx.js', 'ui/motion/control/chatFx.js', 'ui/motion/control/fx.js',
  'ui/motion/control/revealControls.js', 'ui/motion/control/tips.js', 'ui/motion/disintegrate.js',
  'ui/motion/dust/canvasDustDraw.js', 'ui/motion/dust/canvasDustGrid.js',
  'ui/motion/dust/canvasDustStage.js', 'ui/motion/dust/swapDust.js', 'ui/motion/enterLeave.js',
  'ui/motion/flip.js', 'ui/motion/reveal.js', 'ui/motion/strokeFly.js',
  'ui/motion/surface/marks.js', 'ui/motion/surface/motion.js', 'ui/motion/surface/painters.js',
  'ui/motion/surface/surfaces.js', 'ui/motion/surface/themeSwap.js',
  'ui/motion/surface/themeSwapPlay.js', 'ui/motion/surface/tiles.js', 'ui/motion/tune.js',
  'utils/appQueries.js', 'utils/color.js', 'utils/dom.js', 'utils/geometry.js', 'utils/keys.js',
  'utils/math.js', 'utils/nameEditor.js', 'utils/panelResizer.js', 'utils/viewportMetrics.js',
  'utils/zoomOverlay.js',
];

test('every exporting module has a sibling shape file, or is on the frozen list', () => {
  const bare = walk(JS, '.js')
    .filter((f) => !existsSync(f.replace(/\.js$/, '.d.ts')))
    .map((f) => ({ rel: rel(f), src: read(f) }))
    .filter(({ src }) => /^export\b/m.test(src) && !/^export \*/m.test(src))
    .map(({ rel: r }) => r).sort();
  const appeared = bare.filter((r) => !NO_SHAPE_YET.includes(r));
  const gone = NO_SHAPE_YET.filter((r) => !bare.includes(r));
  assert.deepEqual(appeared, [], 'a new module needs its .d.ts — this list only shrinks');
  assert.deepEqual(gone, [], 'these have a shape file now (or are gone) — drop them from NO_SHAPE_YET');
});

test('jsconfig.json is editor configuration only, and reads the shape files', () => {
  const cfg = JSON.parse(read(fileURLToPath(new URL('../jsconfig.json', import.meta.url))));
  assert.equal(cfg.compilerOptions.noEmit, true, 'nothing may be emitted — this app has no build step');
  assert.equal(cfg.compilerOptions.checkJs, false, 'the .js is not type-checked; the shapes are documentation');
  assert.equal(cfg.compilerOptions.allowJs, true);
  assert.ok(cfg.include.includes('js/**/*.d.ts'), 'the shape files must be in scope');
  assert.ok(cfg.include.includes('js/**/*.js'));
});
