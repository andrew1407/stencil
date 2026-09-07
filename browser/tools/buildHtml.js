#!/usr/bin/env node
// ── `npm run build [-- <file.html>]` ────────────────────────────
// Bundles the app into ONE self-contained HTML file (default: browser/stencil.html)
// that runs straight off disk — no static server, no sibling assets. All the bundling
// rules live in vite.config.js; this only picks the output name and moves the result
// out of the throwaway build directory.
import { rm, mkdir, readFile, writeFile, stat } from 'node:fs/promises';
import { dirname, resolve, isAbsolute } from 'node:path';
import { fileURLToPath } from 'node:url';
import { assertSelfContained } from './assertSelfContained.js';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');

const { build } = await import('vite').catch(() => {
  console.error('stencil: vite is not installed — run `npm install` in browser/ first (it is the only dev dependency).');
  process.exit(1);
});

// A given name is relative to where you ran the command; the default lands in browser/,
// which is where .gitignore expects it.
const arg = process.argv.slice(2).find(a => !a.startsWith('-'));
const name = arg && (/\.x?html?$/i.test(arg) ? arg : `${arg}.html`);
const outFile = name
  ? (isAbsolute(name) ? name : resolve(process.cwd(), name))
  : resolve(root, 'stencil.html');

const stage = resolve(root, 'node_modules/.stencil-singlefile');
process.env.STENCIL_SINGLEFILE_OUTDIR = stage;

await build({ configFile: resolve(root, 'vite.config.js'), logLevel: 'warn' });

const html = await readFile(resolve(stage, 'index.html'), 'utf8');
await mkdir(dirname(outFile), { recursive: true });
await writeFile(outFile, html);
await rm(stage, { recursive: true, force: true });

// Never hand over a "single" file that still needs siblings, or whose inline module got
// cut short — both look fine until the page is opened.
assertSelfContained(outFile);

const { size } = await stat(outFile);
console.log(`stencil: wrote ${outFile} (${(size / 1024 / 1024).toFixed(2)} MB, self-contained — open it directly)`);
