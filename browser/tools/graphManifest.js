// The app's static ES-module graph from js/index.js, breadth first: what a cold load fetches before
// anything runs. Dynamic import() is not followed (it loads later, on demand), and a JSON module is
// listed apart, since a modulepreload fills the script slot a `with { type: 'json' }` import never reads.
import { readFileSync } from 'node:fs';
import { dirname, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

export const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
export const ENTRY = 'js/index.js';

// Comments out, strings kept: a specifier is the string after `from`, or after a bare `import`.
const stripComments = (src) => {
  let out = '';
  for (let i = 0; i < src.length; i++) {
    const c = src[i];
    if (c === '/' && src[i + 1] === '/') { while (i < src.length && src[i] !== '\n') i++; out += '\n'; continue; }
    if (c === '/' && src[i + 1] === '*') { const end = src.indexOf('*/', i + 2); i = end < 0 ? src.length : end + 1; continue; }
    if (c === '"' || c === "'" || c === '`') {
      const q = c;
      let j = i + 1;
      while (j < src.length && src[j] !== q) j += src[j] === '\\' ? 2 : 1;
      out += src.slice(i, j + 1);
      i = j;
      continue;
    }
    out += c;
  }
  return out;
};

const STATIC = /(?:^|[;}\n])\s*(?:import|export)\s+(?:[\w*{}\s,$]+?\s+from\s+)?(['"])([^'"\n]+)\1/g;

// Every static specifier of a module, with whether it is a JSON import.
export const importsOf = (src) => {
  const code = stripComments(src);
  const out = [];
  for (const m of code.matchAll(STATIC)) {
    const tail = code.slice(m.index + m[0].length, m.index + m[0].length + 40);
    out.push({ spec: m[2], json: /^\s*with\s*\{\s*type\s*:\s*['"]json['"]/.test(tail) });
  }
  return out;
};

// { modules, json }: paths relative to browser/, BFS order from ENTRY, each once. A specifier that
// leaves browser/ (../common/…) is kept as written relative to it.
export const moduleGraph = (root = ROOT, entry = ENTRY) => {
  const seen = new Set([entry]);
  const modules = [entry];
  const json = [];
  for (let i = 0; i < modules.length; i++) {
    const file = resolve(root, modules[i]);
    for (const { spec, json: isJson } of importsOf(readFileSync(file, 'utf8'))) {
      if (!spec.startsWith('.')) continue;
      const rel = relative(root, resolve(dirname(file), spec)).split('\\').join('/');
      if (seen.has(rel)) continue;
      seen.add(rel);
      (isJson ? json : modules).push(rel);
    }
  }
  return { modules, json };
};
