// A VS Code extension cannot import across subprojects, so browser/js/core/script*.js is
// COPIED into src/parser/script/, at the same depth so every import specifier matches. This is the drift guard, modeled on
// browser-extension/tests/portParity.test.js: byte equality, both directions — no file in
// either tree may appear, vanish or change alone. Nothing is normalized away.
import test from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readFileSync, readdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const BROWSER = fileURLToPath(new URL('../../browser/js/core/script/', import.meta.url));
const COPIES = fileURLToPath(new URL('../src/parser/script/', import.meta.url));
const PARSER = fileURLToPath(new URL('../src/parser/', import.meta.url));
const CORE = fileURLToPath(new URL('../../browser/js/core/', import.meta.url));

// core/script/ is exactly the portable parser; the wasm seam sits a level up and is never
// copied — src/parser/index.js re-composes script.js's JS fallback, pinned below.
const WASM_SEAM = ['script.js', 'script.d.ts', 'scriptHandles.js', 'scriptHandles.d.ts'];

// Data the copies import. Byte-pinned like any other shared table (.claude/rules/architecture.md).
const DATA = [['colorNames.json', '../../browser/js/config/colorNames.json', '../src/config/colorNames.json']];

// The folder IS the parser, so every module in it is copied — no name prefix to filter on.
const scriptFiles = (dir) => readdirSync(dir)
  .filter((f) => f.endsWith('.js') || f.endsWith('.d.ts'))
  .sort();

const bytes = (dir, name) => readFileSync(`${dir}${name}`);

const originals = scriptFiles(BROWSER);
const copies = scriptFiles(COPIES);

test('the browser originals were found', () => {
  assert.ok(originals.filter((f) => f.endsWith('.js')).length >= 12,
    `only ${originals.length} modules in browser/js/core/script — the scan missed the tree`);
});

test('every browser original is copied here, or excluded on the record', () => {
  const missing = originals.filter((f) => !copies.includes(f));
  assert.deepEqual(missing, [],
    'a new browser/js/core/script/ module — copy it into src/parser/script/');
});

test('every copy here has a browser original', () => {
  const orphans = copies.filter((f) => !originals.includes(f));
  assert.deepEqual(orphans, [], 'src/parser/script/ holds a file browser/js/core/script/ does not — delete it');
});

test('the wasm seam stays out of this tree, and still exists upstream', () => {
  for (const name of WASM_SEAM) {
    assert.ok(existsSync(`${CORE}${name}`), `${name} is gone from browser/js/core — the seam moved`);
    assert.ok(!copies.includes(name), `${name} is copied now — there is no wasm in an editor extension`);
    assert.ok(!originals.includes(name), `${name} is back in browser/js/core/script — copy it or move it out`);
  }
});

for (const name of copies) {
  test(`src/parser/script/${name} is byte-identical to browser/js/core/script/${name}`, () => {
    assert.ok(bytes(BROWSER, name).equals(bytes(COPIES, name)),
      `${name} drifted from its browser original — change one, change the other, byte for byte`);
  });
}

for (const [name, browserPath, copyPath] of DATA) {
  test(`src/config/${name} is byte-identical to its browser original`, () => {
    const original = readFileSync(new URL(browserPath, import.meta.url));
    assert.ok(original.equals(readFileSync(new URL(copyPath, import.meta.url))),
      `${name} drifted from browser/js/config/`);
  });
}

// ── The one re-composed file: src/parser/index.js drops the wasm binding ──
// Each declaration it keeps must still match browser/js/core/script.js's, line for line.
const declaration = (src, name) => {
  const lines = src.split('\n');
  const start = lines.findIndex((l) => new RegExp(`^(?:export )?(?:const|function) ${name}\\b`).test(l));
  if (start < 0) return null;
  let depth = 0;
  for (let i = start; i < lines.length; i++) {
    const line = lines[i];
    for (let j = 0, quote = ''; j < line.length; j++) {
      const c = line[j];
      if (quote) { if (c === '\\') j++; else if (c === quote) quote = ''; continue; }
      if (c === '"' || c === "'" || c === '`') { quote = c; continue; }
      if (c === '/' && line[j + 1] === '/') break;
      if ('([{'.includes(c)) depth++;
      else if (')]}'.includes(c)) depth--;
    }
    if (depth <= 0 && /[;}]\s*$/.test(line)) return lines.slice(start, i + 1).join('\n');
  }
  return null;
};

test('index.js keeps script.js\'s parse composition and dump wrappers verbatim', () => {
  const original = readFileSync(`${CORE}script.js`, 'utf8');
  const copy = readFileSync(`${PARSER}index.js`, 'utf8');
  for (const name of ['parseScriptJS', 'scriptDump', 'scriptDiagnostics']) {
    const mine = declaration(copy, name);
    const theirs = declaration(original, name);
    assert.ok(theirs, `browser/js/core/script.js no longer declares ${name}`);
    assert.ok(mine, `src/parser/index.js no longer declares ${name}`);
    assert.equal(mine, theirs, `${name} drifted from browser/js/core/script.js`);
  }
});

test('index.js exports the parser the rest of the extension asks for, and no wasm binding', () => {
  const copy = readFileSync(`${PARSER}index.js`, 'utf8');
  assert.match(copy, /export const parseScript = parseScriptJS;/);
  assert.ok(!copy.includes('stencilCore'), 'the wasm loader has no place in this tree');
});

test('the copies are ESM, scoped by their own package.json', () => {
  const scope = JSON.parse(readFileSync(`${PARSER}package.json`, 'utf8'));
  assert.equal(scope.type, 'module');
  const root = JSON.parse(readFileSync(new URL('../package.json', import.meta.url), 'utf8'));
  assert.equal(root.type, undefined, 'the extension entry point is CommonJS');
});
