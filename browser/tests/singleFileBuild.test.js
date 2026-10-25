// Guards the OPTIONAL single-file build (vite.config.js → `npm run build`). That build
// rewrites a handful of exact strings in index.html and two loaders; if one of those
// sources is edited so a pattern stops matching, the emitted stencil.html would quietly
// need sibling files again. These tests catch it without installing vite or running a
// build — so they hold in the normal `npm test` run.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, existsSync } from 'node:fs';
import {
  REWRITES, FALLBACKS, NO_SIBLINGS, PRE_PAINT_TAG, MANIFEST_LINK, FAVICON_HREF, MODULEPRELOAD_BLOCK,
} from '../tools/singleFilePatterns.js';

const read = (f) => readFileSync(new URL(`../${f}`, import.meta.url), 'utf8');

for (const [file, patterns] of Object.entries(REWRITES)) {
  for (const pattern of patterns) {
    test(`single-file build: ${file} still matches ${pattern}`, () => {
      assert.ok(read(file).match(pattern), `${file} no longer matches ${pattern} — update the pattern in tools/singleFilePatterns.js alongside the source`);
    });
  }
}

test('single-file build: the index.html rewrites leave no sibling references', () => {
  const out = read('index.html')
    .replace(MODULEPRELOAD_BLOCK, '')
    .replace(MANIFEST_LINK, '')
    .replace(PRE_PAINT_TAG, '<script>/* inlined */</script>')
    .replace(FAVICON_HREF, 'href="data:image/svg+xml,inlined"');
  for (const stale of NO_SIBLINGS) {
    assert.ok(!out.includes(stale), `index.html still references ${stale} after the single-file rewrites`);
  }
});

test('single-file build: index.html loads nothing the single-file build ignores', () => {
  // Every relative src/href must be something the build accounts for — vite folds in the module entry and the
  // stylesheets, vite.config.js rewrites the rest — or it ships as a sibling file next to stencil.html.
  const handled = new Set([
    'js/index.js',                                                                   // vite: the module graph
    'css/theme.css',                                                                 // vite: the stylesheets
    'js/prePaintTheme.js', '../common/icons/favicon.svg', 'manifest.webmanifest',    // vite.config.js: inlined or dropped
  ]);
  const refs = [...read('index.html').replace(MODULEPRELOAD_BLOCK, '').matchAll(/(?:src|href)="([^"]+)"/g)]
    .map(m => m[1])
    .filter(v => !/^(?:https?:|data:|#|\/)/.test(v));
  for (const ref of refs) {
    if (/^css\/(?:layout|components|animations|webcore)\//.test(ref)) continue;   // the split stylesheets — vite folds them all in
    assert.ok(handled.has(ref), `index.html loads "${ref}", which the single-file build does not handle — teach vite.config.js about it`);
  }
});

test('single-file build: every fallback names a loader that exists, and vite.config.js picks by that name', () => {
  for (const file of Object.keys(FALLBACKS)) {
    assert.ok(existsSync(new URL(`../${file}`, import.meta.url)), `FALLBACKS names ${file}, which is gone — a moved loader must move its key`);
  }
  assert.doesNotMatch(read('vite.config.js'), /endsWith\(['"][^'"]*\/[^'"]*\.js['"]\)/,
    'vite.config.js matches a loader by a literal path — key it in FALLBACKS so a move breaks this test, not the build');
});
