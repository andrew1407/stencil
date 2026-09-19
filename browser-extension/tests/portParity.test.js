// The extension can't import across subprojects, so a few browser modules are duplicated
// into src/ as rule-for-rule PORTS. Their behavioural cases live in browser/tests/; this
// is the drift guard that lets the extension drop those duplicated suites. Two manifests:
// whole-file pairs below, then per-function ports further down. Body differences are never
// normalized away — a rewording in either copy fails the pair.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync, readFileSync } from 'node:fs';

// name → [browser copy, extension copy], both relative to this file.
const MANIFEST = [
  // The one HTML escaper both surfaces (and tipContent below) re-export.
  ['escapeHtml', '../../browser/js/ui/escapeHtml.js', '../src/lib/escapeHtml.js'],
  ['tipContent', '../../browser/js/ui/tipContent.js', '../src/lib/tipContent.js'],
  ['numericInput', '../../browser/js/ui/numericInput.js', '../src/lib/numericInput.js'],
  ['dropdownMenu', '../../browser/js/ui/dropdownMenu.js', '../src/lib/dropdownMenu.js'],
  ['controlTooltip', '../../browser/js/ui/controlTooltip.js', '../src/lib/controlTooltip.js'],
  ['scrollbarHover', '../../browser/js/ui/scrollbarHover.js', '../src/lib/scrollbarHover.js'],
  // The one-canvas dust cloud every element-sized flight rides: pure flight table +
  // painter, so the copy is the whole file.
  ['dustCloud', '../../browser/js/ui/dustCloud.js', '../src/lib/dustCloud.js'],
  // The motion modes' glyphs: pure SVG strings, so the copy is the whole file.
  ['motionIcons', '../../browser/js/ui/motionIcons.js', '../src/lib/motionIcons.js'],
  // The crop rect's flight between two shapes: a pure rAF ramp, so the copy is the whole file.
  ['rectTween', '../../browser/js/ui/motion/rectTween.js', '../src/lib/rectTween.js'],
  // The shared LLM client: per-surface wording/token defaults live in llmSurface.js,
  // so the client itself differs only in its header + providers.json import path.
  ['llmClient', '../../browser/js/llm/llmClient.js', '../src/llm/llmClient.js'],
  // The registry-driven validation engine: pure, registry-in/verdict-out, so the copy
  // is the whole file.
  ['opSchema', '../../browser/js/llm/opSchema.js', '../src/llm/opSchema.js'],
  // The un-persisted "Swap message sides" preference: pure module state, so the copy is
  // the whole file.
  ['chatLayoutPrefs', '../../browser/js/ui/chatLayoutPrefs.js', '../src/lib/chatLayoutPrefs.js'],
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

// ── Partial ports: one function at a time ───────────────────────────────────
// A module that ports only PART of a browser module lists the functions instead: each
// must match its browser original verbatim, mid-body comments included. A browser
// function routed to the wasm core keeps its JS reference under a `JS` suffix — that is
// the original. Either side may be a file or a directory, which is scanned whole.
const FUNCTIONS = [
  ['popover', '../../browser/js/ui/popover.js', '../src/lib/popover.js', ['popoverPosition']],
  ['cropGeometry', '../../browser/js/core/cropGeometry.js', '../src/lib/cropGeometry.js',
    ['isAlbumOrientation', 'cropAspect', 'centeredCrop', 'resizeCropFromCorner',
     'moveCropClamped', 'scaleCropCentered']],
  // motion/ is the extension's own implementation, split along the browser's own file
  // boundaries; only what it shares to the letter with the app — the pure ramps, the tile
  // maths, the class names — is listed.
  ['motion', '../../browser/js/ui/motion/', '../src/lib/motion/', [
    'REVEAL_ITEM_CLASS', 'REVEAL_IN_CLASS', 'REVEAL_ENTERING_CLASS', 'REVEAL_MASKED_CLASS',
    'revealDissolve', 'revealGrain',
    'LEAVING_CLASS', 'createListHold', 'emptyStateVisible',
    'TILE_GATHER_SHARE', 'TILE_JITTER_SHARE', 'tileNoise', 'tileWaypoint', 'reshapeGrid',
    'reintegrate', 'rectCenter',
    'MATERIALIZE_CLASS', 'MATERIALIZE_VEIL_CLASS', 'CHAT_ENTERING_CLASS', 'CHAT_SLIDE_CLASS',
    'SURFACE_FORMING_CLASS', 'SURFACE_LEAVING_CLASS', 'SURFACE_DRIVEN_CLASS',
  ]],
  // opPlan.js parses to the SAME §1 mechanics as the app, then applies the extension's
  // own §8/§11.2 rules — so only the shared mechanics are pinned. `validateAsk` and
  // `parseOpPlan` are deliberately per-surface and stay out.
  ['planParser', '../../browser/js/llm/planParser.js', '../src/llm/opPlan.js',
    ['firstJsonObject', 'askAnswerText']],
];

// One top-level `const NAME = …` / `function NAME …` statement — exported or not, since a
// shared helper may be module-private on either side — from its first line to the line
// that closes it: brackets balance and the line ends the statement.
const declaration = (src, name) => {
  const lines = src.split('\n');
  const start = lines.findIndex((l) => new RegExp(`^(?:export )?(?:const|function) ${name}\\b`).test(l));
  if (start < 0) return null;
  let depth = 0;
  for (let i = start; i < lines.length; i++) {
    const line = lines[i];
    // Brackets inside a string literal or a trailing `//` comment are text, not structure
    // (`text.indexOf('{')` would otherwise leave the count permanently open).
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

// A port as one string: a file, or every .js in a directory.
const sourceOf = (rel) => {
  if (!rel.endsWith('/')) return read(rel);
  const dir = new URL(rel, import.meta.url);
  return readdirSync(dir).filter((f) => f.endsWith('.js')).sort()
    .map((f) => readFileSync(new URL(f, dir), 'utf8')).join('\n');
};

for (const [name, browserPath, extPath, fns] of FUNCTIONS) {
  test(`${name}: every ported function matches its browser original`, () => {
    const browser = sourceOf(browserPath);
    const ext = sourceOf(extPath);
    for (const fn of fns) {
      const mine = declaration(ext, fn);
      assert.ok(mine, `${name}: the extension no longer exports ${fn}`);
      const theirs = declaration(browser, `${fn}JS`)?.replace(`${fn}JS`, fn) ?? declaration(browser, fn);
      assert.ok(theirs, `${name}: ${fn} has no browser original (nor a ${fn}JS reference)`);
      assert.equal(mine, theirs,
        `${name}.${fn} drifted from its browser original — change one, change the other`);
    }
  });
}
