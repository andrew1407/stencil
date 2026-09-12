// The functions the background hands to chrome.scripting.executeScript({ func }) run in
// ANOTHER page's realm: Chrome serialises the function and nothing else, so the module
// graph is gone by the time it runs. Two things have to hold, and neither shows up in a
// unit test that never injects: the call site must actually have the name in scope, and
// the injected function must use nothing it imported.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const SRC = fileURLToPath(new URL('../src/', import.meta.url));
const walk = (dir, out = []) => {
  for (const e of readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p, out);
    else if (e.name.endsWith('.js')) out.push(p);
  }
  return out;
};
const FILES = walk(SRC).sort();
const read = (p) => readFileSync(p, 'utf8');
const rel = (p) => path.relative(SRC, p);

// The bindings a module pulls in: `import { a, b as c }` and `import d from`.
const importedNames = (src) => {
  const names = new Set();
  for (const m of src.matchAll(/^import\s+(?:([\w$]+)\s*,?\s*)?(?:\{([^}]*)\})?\s*from/gm)) {
    if (m[1]) names.add(m[1]);
    for (const part of (m[2] ?? '').split(',')) {
      const as = part.trim().split(/\s+as\s+/);
      if (as.length && as[as.length - 1]) names.add(as[as.length - 1].trim());
    }
  }
  return names;
};
// Top-level declarations, exported or not.
const declaredNames = (src) => new Set(
  [...src.matchAll(/^(?:export\s+)?(?:async\s+)?(?:const|let|var|function|class)\s+([\w$]+)/gm)].map((m) => m[1]));

// `func: name` / `func: name,` inside an executeScript options object.
const injectedAt = (src) => [...src.matchAll(/\bfunc:\s*([A-Za-z_$][\w$]*)\s*(?=[,}])/g)].map((m) => m[1]);

const CALL_SITES = FILES.map((f) => ({ file: f, src: read(f) }))
  .filter(({ src }) => injectedAt(src).length);

test('every injected func is a name its call site really has in scope', () => {
  assert.ok(CALL_SITES.length >= 2, 'the scanner found no executeScript({ func }) call at all');
  for (const { file, src } of CALL_SITES) {
    const have = new Set([...importedNames(src), ...declaredNames(src)]);
    for (const name of injectedAt(src))
      assert.ok(have.has(name),
        `${rel(file)} injects "${name}" but neither imports nor declares it — it would throw in the page`);
  }
});

// One top-level `const NAME = …` / `function NAME` body, exported or not, brace-counted.
const bodyOf = (src, name) => {
  const start = src.search(new RegExp(`^(?:export )?(?:const|function|async function) ${name}\\b`, 'm'));
  if (start < 0) return null;
  const open = src.indexOf('{', start);
  if (open < 0) return null;
  let depth = 0;
  for (let i = open; i < src.length; i++) {
    if (src[i] === '{') depth++;
    else if (src[i] === '}' && --depth === 0) return src.slice(open, i + 1);
  }
  return null;
};

test('every injected func is self-contained — it closes over nothing it imported', () => {
  const injected = new Set(CALL_SITES.flatMap(({ src }) => injectedAt(src)));
  const found = new Set();
  for (const file of FILES) {
    const src = read(file);
    const imported = [...importedNames(src)];
    for (const name of injected) {
      const body = bodyOf(src, name);
      if (!body) continue;
      found.add(name);
      for (const dep of imported)
        assert.ok(!new RegExp(`\\b${dep}\\b`).test(body),
          `${rel(file)}: ${name} is injected into a page but uses the imported "${dep}" — inline it`);
    }
  }
  assert.deepEqual([...injected].filter((n) => !found.has(n)), [],
    'an injected function has no top-level definition to check — the scanner or the name moved');
});
