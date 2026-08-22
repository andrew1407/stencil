// The extension can't import across subprojects, so a few browser/js/ui modules are
// duplicated into src/lib as rule-for-rule PORTS. The behavioural cases for them live in
// browser/tests/<name>.test.js; this file is the drift guard that lets the extension
// drop its duplicated copies of those suites: each manifest pair's two SOURCE files must
// be identical once ONLY the header comment block and import specifiers are normalized.
//
// Converge-then-pin plan: today the copies differ only in their header comments (each
// surface introduces the module in its own words) and import paths, so the guard is
// "normalized-identical". A later phase converges the headers too and pins full
// byte-equality, at which point the normalization here shrinks to nothing.
//
// controlTooltip.js joined the manifest after its one mid-body comment (the hover-popup
// opt-out note) was converged to the browser's wording. Body differences are never
// normalized away — a rewording in either copy fails the pair.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

// name → [browser copy, extension copy], both relative to this file.
const MANIFEST = [
  ['tipContent', '../../browser/js/ui/tipContent.js', '../src/lib/tipContent.js'],
  ['numericInput', '../../browser/js/ui/numericInput.js', '../src/lib/numericInput.js'],
  ['dropdownMenu', '../../browser/js/ui/dropdownMenu.js', '../src/lib/dropdownMenu.js'],
  ['controlTooltip', '../../browser/js/ui/controlTooltip.js', '../src/lib/controlTooltip.js'],
  // The shared LLM client: per-surface wording/token defaults live in llmSurface.js,
  // so the client itself differs only in its header + providers.json import path.
  ['llmClient', '../../browser/js/llm/llmClient.js', '../src/llm/llmClient.js'],
];

const read = (rel) => readFileSync(new URL(rel, import.meta.url), 'utf8');

// Drop the header: the leading run of `//` lines (and blanks) before the first line of
// code. Comments in the body are NOT touched — a divergence there must fail.
const stripHeader = (src) => {
  const lines = src.split('\n');
  let i = 0;
  while (i < lines.length && (/^\s*\/\//.test(lines[i]) || lines[i].trim() === '')) i++;
  return lines.slice(i).join('\n');
};

// Reduce import specifiers to their basename, so './popover.js' compares equal from
// either directory layout.
const normalizeImports = (src) =>
  src.replace(/(from\s+['"])([^'"]+)(['"])/g, (_, pre, spec, post) => pre + spec.split('/').pop() + post);

const normalize = (src) => normalizeImports(stripHeader(src));

for (const [name, browserPath, extPath] of MANIFEST) {
  test(`${name}: the extension port matches the browser module rule-for-rule`, () => {
    const browser = normalize(read(browserPath));
    const ext = normalize(read(extPath));
    assert.ok(browser.trim() && ext.trim(), 'both copies have a body');
    assert.equal(ext, browser,
      `${name} drifted from its browser original — change one, change the other `
      + '(only the header comment and import paths may differ)');
  });
}
