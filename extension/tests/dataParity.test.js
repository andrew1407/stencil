// ── Data-copy drift guard ────────────────────────────────────────────────────
// The extension ships self-contained (MV3): it never reads ../browser at runtime, so the
// canonical data tables in browser/js/config/*.json live here as CHECKED-IN copies — JS
// literals, because lib/accent.js is a pre-paint classic script with no module graph, and
// lib/icons.js / lib/cropGeometry.js carry deliberate subsets of larger canonical files.
// Nothing at runtime enforces the sync; this manifest does, the way portParity.test.js
// pins the ported modules. Two modes:
//   full   — the extension copy equals the canonical table entry-for-entry, in order.
//   subset — every extension entry byte-matches its canonical entry; extra names must be
//            declared extensionOnly, and an extensionOnly name must NOT exist canonically
//            (so the browser later adding a same-named entry cannot drift silently).

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

import { loadAccent } from './helpers/accentSandbox.js';
import { ICONS } from '../src/lib/icons.js';
import { PAGE_SIZES, DEFAULT_PAGE } from '../src/lib/cropGeometry.js';

const canonical = (rel) => JSON.parse(readFileSync(new URL(rel, import.meta.url), 'utf8'));

// One row per extension data copy: where the extension copy lives, which canonical
// browser JSON pins it, and how strictly.
const MANIFEST = [
  {
    name: 'accents: lib/accent.js ACCENTS ↔ config/accents.json',
    mode: 'full',
    // Sandbox values cross a realm boundary; the JSON round-trip normalizes prototypes.
    extension: () => JSON.parse(JSON.stringify(loadAccent().accent.list)),
    canonical: () => canonical('../../browser/js/config/accents.json'),
  },
  {
    name: 'icons: lib/icons.js ICONS ⊂ config/icons.json',
    mode: 'subset',
    extensionOnly: ['sidebar', 'type'],   // extension-UI glyphs with no browser twin
    extension: () => ICONS,
    canonical: () => canonical('../../browser/js/config/icons.json'),
  },
  {
    name: 'page sizes: lib/cropGeometry.js PAGE_SIZES ↔ config/constants.json',
    mode: 'full',
    extension: () => PAGE_SIZES,
    canonical: () => canonical('../../browser/js/config/constants.json').PAGE_SIZES,
  },
  {
    name: 'providers: src/config/providers.json ↔ config/llm/providers.json',
    mode: 'full',
    extension: () => canonical('../src/config/providers.json'),
    canonical: () => canonical('../../browser/js/config/llm/providers.json'),
  },
  {
    name: 'op registry: src/config/opRegistry.json ↔ config/llm/opRegistry.json',
    mode: 'full',
    extension: () => canonical('../src/config/opRegistry.json'),
    canonical: () => canonical('../../browser/js/config/llm/opRegistry.json'),
  },
  {
    name: 'system prompt: src/config/systemPrompt.json ↔ config/llm/systemPrompt.json extension keys',
    mode: 'full',
    extension: () => canonical('../src/config/systemPrompt.json'),
    canonical: () => {
      const { extensionHead, extensionTail } = canonical('../../browser/js/config/llm/systemPrompt.json');
      return { extensionHead, extensionTail };
    },
  },
];

for (const { name, mode, extension, canonical: canon, extensionOnly = [] } of MANIFEST) {
  test(`${name} (${mode})`, () => {
    const ext = extension();
    const ref = canon();
    if (mode === 'full') {
      assert.deepEqual(ext, ref, `${name} drifted from the canonical JSON`);
      // deepEqual ignores key order, but canonical order is part of the contract
      // (it drives selector/option ordering on every surface).
      if (!Array.isArray(ref))
        assert.deepEqual(Object.keys(ext), Object.keys(ref), `${name}: order drifted`);
    } else {
      for (const [key, value] of Object.entries(ext)) {
        if (extensionOnly.includes(key)) {
          assert.equal(key in ref, false,
            `"${key}" is declared extension-only but now exists canonically — sync or rename it`);
          continue;
        }
        assert.ok(key in ref, `"${key}" is not in the canonical table — declare it extensionOnly?`);
        assert.equal(value, ref[key], `"${key}" drifted from its canonical entry`);
      }
    }
  });
}

// The scalar riding along with PAGE_SIZES: the crop dialog's default format name.
test('DEFAULT_PAGE matches the canonical default format', () => {
  assert.equal(DEFAULT_PAGE,
    canonical('../../browser/js/config/constants.json').DEFAULT_PAGE.size);
});
