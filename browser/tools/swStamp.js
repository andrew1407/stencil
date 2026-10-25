#!/usr/bin/env node
// The deploy stamp: every file a page of THIS build can load (the shell, the stylesheets, the module
// graph and every module a dynamic import or a worker reaches, the wasm pair when built) and a VERSION
// of their bytes for sw.js, which then serves one deploy cache-first; and, with the wasm built, its
// preload hints in index.html. The tracked files stay unstamped.   node tools/swStamp.js --write | --print
import { createHash } from 'node:crypto';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, relative, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { ROOT, ENTRY, moduleGraph } from './graphManifest.js';

export const START = '// stamp:start';
export const END = '// stamp:end';
const SW = resolve(ROOT, 'sw.js');
const HTML = resolve(ROOT, 'index.html');
export const WASM_START = '<!-- wasm-preload:start';
export const WASM_END = '<!-- wasm-preload:end -->';
const SHELL = ['./', 'index.html', 'manifest.webmanifest'];
// Built by `npm run build-wasm`, gitignored: precached when present, as the deploys build it first.
const WASM = ['js/wasm/stencilCore.js', 'js/wasm/stencilCore.wasm'];

const rel = (root, file) => relative(root, file).split('\\').join('/');

// `import('…')` and `new URL('…', import.meta.url)` targets: loaded later, by a worker or on demand.
const lateTargets = (src) => [
  ...src.matchAll(/\bimport\(\s*(['"])(\.[^'"]+)\1\s*\)/g),
  ...src.matchAll(/new URL\(\s*(['"])(\.[^'"]+)\1\s*,\s*import\.meta\.url\s*\)/g),
].map((m) => m[2]);

// Every module a page can load: the static graph, then each late target's own graph, breadth first.
export const allModules = (root = ROOT) => {
  const seen = new Set();
  const modules = [];
  const json = [];
  const entries = [ENTRY];
  for (let i = 0; i < entries.length; i++) {
    if (seen.has(entries[i])) continue;
    const graph = moduleGraph(root, entries[i]);
    for (const m of graph.modules) {
      if (seen.has(m)) continue;
      seen.add(m);
      modules.push(m);
      const file = resolve(root, m);
      for (const t of lateTargets(readFileSync(file, 'utf8'))) {
        const target = rel(root, resolve(dirname(file), t));
        if (target.endsWith('.js') && existsSync(resolve(root, target))) entries.push(target);
      }
    }
    for (const j of graph.json) if (!json.includes(j)) json.push(j);
  }
  return { modules, json };
};

// The stylesheets and classic scripts index.html loads, and the icons it and its manifest name.
const shellAssets = (root) => {
  const html = readFileSync(resolve(root, 'index.html'), 'utf8');
  const sheets = [...html.matchAll(/<link\b[^>]*\brel="stylesheet"[^>]*\bhref="([^"]+)"/g)].map((m) => m[1]);
  const icons = [...html.matchAll(/<link\b[^>]*\brel="(?:icon|apple-touch-icon)"[^>]*\bhref="([^"]+)"/g)].map((m) => m[1]);
  const scripts = [...html.matchAll(/<script\b[^>]*\bsrc="([^"]+)"/g)].map((m) => m[1]);
  const manifest = JSON.parse(readFileSync(resolve(root, 'manifest.webmanifest'), 'utf8'));
  const named = (manifest.icons || []).map((i) => i.src);
  return [...sheets, ...scripts, ...icons, ...named].filter((u) => !/^[a-z]+:/i.test(u));
};

// The precache list (paths relative to sw.js) and the VERSION hashed from every listed file.
export const stampOf = (root = ROOT) => {
  const { modules, json } = allModules(root);
  const wasm = WASM.filter((w) => existsSync(resolve(root, w)));
  const files = [...new Set([...SHELL.slice(1), ...shellAssets(root), ...modules, ...json, ...wasm])];
  const hash = createHash('sha256');
  for (const f of files) {
    hash.update(`${f}\n`);
    hash.update(readFileSync(resolve(root, f)));
  }
  return { version: hash.digest('hex').slice(0, 16), precache: ['./', ...files] };
};

// index.html with the core's glue, its script wrappers and its binary fetched beside the module
// graph instead of after it (the core is the boot's slowest wait); untouched without the wasm.
export const htmlWithWasm = (html, root = ROOT) => {
  if (!WASM.every((w) => existsSync(resolve(root, w)))) return html;
  const a = html.indexOf(WASM_START);
  const b = html.indexOf(WASM_END);
  if (a < 0 || b < a) throw new Error('index.html has no wasm-preload block');
  const head = html.slice(a, html.indexOf('\n', a) + 1);
  const hints = [
    `  <link rel="modulepreload" href="${WASM[0]}">`,
    '  <link rel="modulepreload" href="js/core/scriptHandles.js">',
    `  <link rel="preload" href="${WASM[1]}" as="fetch" type="application/wasm" crossorigin>`,
  ].join('\n');
  return html.slice(0, a) + head + hints + '\n  ' + html.slice(b);
};

export const stampBlock = ({ version, precache }) => [
  START,
  `const VERSION = '${version}';`,
  `const PRECACHE = ${JSON.stringify(precache, null, 2).replace(/"/g, "'")};`,
  END,
].join('\n');

// sw.js with its stamp block replaced.
export const stamped = (src, stamp) => {
  const a = src.indexOf(START);
  const b = src.indexOf(END);
  if (a < 0 || b < a) throw new Error('sw.js has no stamp block');
  return src.slice(0, a) + stampBlock(stamp) + src.slice(b + END.length);
};

if (import.meta.url === pathToFileURL(process.argv[1] || '').href) {
  if (process.argv[2] === '--write') {
    writeFileSync(HTML, htmlWithWasm(readFileSync(HTML, 'utf8')));   // first: VERSION hashes the page as served
    const stamp = stampOf();
    writeFileSync(SW, stamped(readFileSync(SW, 'utf8'), stamp));
    console.log(`swStamp: ${stamp.version}, ${stamp.precache.length} files precached`);
  } else if (process.argv[2] === '--print') {
    console.log(stampBlock(stampOf()));
  } else {
    console.error('usage: node tools/swStamp.js --write | --print');
    process.exit(2);
  }
}
