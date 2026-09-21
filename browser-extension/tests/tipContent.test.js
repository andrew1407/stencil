// src/lib/tipContent.js is a rule-for-rule PORT of browser/js/ui/tip/tipContent.js. Its
// behavioural cases (parse/render, keycaps, platform key vocabulary) live in
// browser/tests/tipContent.test.js; portParity.test.js pins the two sources identical,
// so the extension no longer duplicates that suite. What remains here is the
// extension-specific wiring: OUR controller/CSS/pages have to use the module.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { themeCss } from './helpers/sources.js';

test('the tooltip controller renders the structure, and the CSS styles every part', () => {
  const js = readFileSync(new URL('../src/lib/tip/controlTooltip.js', import.meta.url), 'utf8');
  assert.match(js, /renderTip/, 'the controller goes through the content model');
  assert.ok(!/\.textContent\s*=\s*txt/.test(js), 'and no longer prints the title flat');
  const css = themeCss();
  for (const cls of ['.tip-head', '.tip-title', '.tip-keys', '.tip-key', '.tip-plus',
    '.tip-rows', '.tip-term', '.tip-desc', '.tip-bullets', '.tip-hint', '.tip-note']) {
    assert.ok(css.includes(cls), `${cls} is styled`);
  }
  assert.ok(!/#app-tooltip\s*\{[^}]*white-space:\s*pre-line/.test(css),
    'the flat-text fallback is gone — the tooltip is real markup now');
  // Every page of the extension has to wire it up, or its controls keep the native popup.
  for (const p of ['popup/popup.js', 'crop/crop.js', 'options/options.js']) {
    const src = readFileSync(new URL(`../src/${p}`, import.meta.url), 'utf8');
    assert.match(src, /initTooltips\(\);/, `${p} initialises the tooltips`);
  }
});
