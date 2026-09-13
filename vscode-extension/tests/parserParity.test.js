// A VS Code extension cannot import across subprojects, so browser/js/core/script*.js is
// COPIED into src/parser/. This is the drift guard, modeled on
// browser-extension/tests/portParity.test.js: byte equality, both directions — no file in
// either tree may appear, vanish or change alone. Nothing is normalized away.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const BROWSER = fileURLToPath(new URL('../../browser/js/core/', import.meta.url));
const COPIES = fileURLToPath(new URL('../src/parser/', import.meta.url));

// Not copied, each for a reason the extension cannot work around.
const EXCLUDED = new Map([
  ['script.js', 'the entry point binds the wasm loader and the app\'s units module; '
    + 'src/parser/index.js re-composes the JS fallback and is pinned declaration-by-declaration below'],
  ['script.d.ts', 'the sibling of script.js; src/parser/index.d.ts describes the narrower surface'],
  ['scriptHandles.js', 'marshals the wasm core\'s handles; there is no wasm in an editor extension'],
]);

// Data the copies import. Byte-pinned like any other shared table (.claude/rules/architecture.md).
const DATA = [['colorNames.json', '../../browser/js/config/colorNames.json', '../src/config/colorNames.json']];

const scriptFiles = (dir) => readdirSync(dir)
  .filter((f) => f.startsWith('script') && (f.endsWith('.js') || f.endsWith('.d.ts')))
  .sort();

const bytes = (dir, name) => readFileSync(`${dir}${name}`);

const originals = scriptFiles(BROWSER);
const copies = scriptFiles(COPIES);

test('the browser originals were found', () => {
  assert.ok(originals.filter((f) => f.endsWith('.js')).length >= 12,
    `only ${originals.length} script* files in browser/js/core — the scan missed the tree`);
});

test('every browser original is copied here, or excluded on the record', () => {
  const missing = originals.filter((f) => !copies.includes(f) && !EXCLUDED.has(f));
  assert.deepEqual(missing, [],
    'a new browser/js/core/script* file — copy it into src/parser/, or record why not');
});

test('every copy here has a browser original', () => {
  const orphans = copies.filter((f) => !originals.includes(f));
  assert.deepEqual(orphans, [], 'src/parser/ holds a file browser/js/core/ does not — delete it');
});

test('no excluded file was quietly copied after all, and each still exists upstream', () => {
  for (const [name, why] of EXCLUDED) {
    assert.ok(originals.includes(name), `${name} is gone from browser/js/core — drop the exclusion`);
    assert.ok(!copies.includes(name), `${name} is copied now — drop its exclusion (${why})`);
  }
});

for (const name of copies) {
  test(`src/parser/${name} is byte-identical to browser/js/core/${name}`, () => {
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

// ── The one re-composed file ────────────────────────────────────────────────
// src/parser/index.js is not a copy: it drops the wasm binding browser/js/core/script.js
// carries. Each declaration it keeps must still match that file's, line for line.
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
  const original = readFileSync(`${BROWSER}script.js`, 'utf8');
  const copy = readFileSync(`${COPIES}index.js`, 'utf8');
  for (const name of ['parseScriptJS', 'scriptDump', 'scriptDiagnostics']) {
    const mine = declaration(copy, name);
    const theirs = declaration(original, name);
    assert.ok(theirs, `browser/js/core/script.js no longer declares ${name}`);
    assert.ok(mine, `src/parser/index.js no longer declares ${name}`);
    assert.equal(mine, theirs, `${name} drifted from browser/js/core/script.js`);
  }
});

test('index.js exports the parser the rest of the extension asks for, and no wasm binding', () => {
  const copy = readFileSync(`${COPIES}index.js`, 'utf8');
  assert.match(copy, /export const parseScript = parseScriptJS;/);
  assert.ok(!copy.includes('stencilCore'), 'the wasm loader has no place in this tree');
});

test('the copies are ESM, scoped by their own package.json', () => {
  const scope = JSON.parse(readFileSync(`${COPIES}package.json`, 'utf8'));
  assert.equal(scope.type, 'module');
  const root = JSON.parse(readFileSync(new URL('../package.json', import.meta.url), 'utf8'));
  assert.equal(root.type, undefined, 'the extension entry point is CommonJS');
});
