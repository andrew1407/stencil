// src/lib/tip.js — how a control describes itself, and the rule it exists to enforce:
// the extension's own UI never sets a native `title`, because Chrome's popup is slow,
// unstyled, and would show ON TOP of lib/controlTooltip.js's. Everything goes through
// `data-title`, which that controller already reads.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { animationsCss, themeCss } from './helpers/sources.js';
import { setTip, tipLabel } from '../src/lib/tip/tip.js';
import { stubEl } from './helpers/domStub.js';

test('setTip writes data-title and never a title attribute', () => {
  const el = setTip(stubEl(), 'Rescan page (Alt+R)');
  assert.equal(el.attrs['data-title'], 'Rescan page (Alt+R)');
  assert.equal(el.attrs.title, undefined);
  assert.equal(el.attrs['aria-label'], undefined, 'a labelled control opts in');
});

test('{ label: true } gives an icon-only control the accessible name `title` used to', () => {
  assert.equal(setTip(stubEl(), 'Rescan page (Alt+R)', { label: true }).attrs['aria-label'], 'Rescan page');
  // The heading only: the shortcut hint, the em-dash gloss and the extra lines are prose.
  assert.equal(tipLabel('More — attach, clear, settings\nChecking the configured LLM…'), 'More');
  assert.equal(tipLabel('Remove connection — forgets its saved token'), 'Remove connection');
  assert.equal(tipLabel('Unpin'), 'Unpin');
  assert.equal(tipLabel(''), '');
});

test('empty text clears both attributes, and a missing element is a no-op', () => {
  const el = setTip(stubEl(), 'Send', { label: true });
  setTip(el, '', { label: true });
  assert.deepEqual(el.attrs, {});
  assert.equal(setTip(null, 'x'), null);
  assert.equal(setTip({}, 'x').tag, undefined, 'an element with no setAttribute is skipped');
});

// lib/overlay.js is the one exception: it is injected into the HOST page (executeScript({func}), so
// it cannot import), which means controlTooltip.js never runs over it.
const NATIVE_TITLE_OK = new Set(['src/lib/drop/overlay.js']);
// controlTooltip.js blanks/restores a native title it finds (a port pinned byte-for-byte
// against its browser twin); content.js's `tip.title` is a plain object field.
const NOT_A_CALL_SITE = new Set(['src/lib/tip/controlTooltip.js', 'src/lib/tip/content.js']);

const walk = (dir, out = []) => {
  for (const name of readdirSync(dir)) {
    const p = `${dir}/${name}`;
    if (statSync(p).isDirectory()) walk(p, out);
    else if (/\.(js|html)$/.test(name)) out.push(p);
  }
  return out;
};
const srcFiles = () => walk(new URL('../src', import.meta.url).pathname)
  .map((p) => p.slice(p.indexOf('/src/') + 1));

test('no control in the extension carries a native `title`', () => {
  const root = new URL('../', import.meta.url).pathname;
  let scanned = 0;
  for (const rel of srcFiles()) {
    const src = readFileSync(root + rel, 'utf8');
    scanned++;
    if (!NATIVE_TITLE_OK.has(rel))
      assert.ok(!/\stitle="/.test(src), `${rel} sets a title="" attribute — use data-title`);
    if (NOT_A_CALL_SITE.has(rel) || NATIVE_TITLE_OK.has(rel)) continue;
    // `el.title = …` on a DOM node. dataset.title is exactly the right thing, so it stays.
    const bad = src.split('\n').filter((l) => /[\w$)\]]\.title\s*=[^=]/.test(l) && !/dataset\.title/.test(l));
    assert.deepEqual(bad, [], `${rel} assigns .title on an element — use setTip()`);
  }
  assert.ok(scanned > 20, 'the walk actually found the sources');
});

test('the custom tooltip fades and rises instead of blinking', () => {
  const css = themeCss();
  // A TRANSITION, not keyframes: a fast sweep re-points the one shared tooltip many
  // times a second, and a transition simply re-aims from wherever it is.
  assert.match(css, /#app-tooltip \{[\s\S]*?transition: opacity 190ms cubic-bezier\(0\.55, 0, 1, 0\.45\)/);
  assert.match(css, /#app-tooltip \{[\s\S]*?transform: translate\(3px, 6px\) scale\(0\.96\)/);
  assert.match(css, /#app-tooltip \{[\s\S]*?transform-origin: top left/, 'it grows away from the cursor it is anchored to');
  assert.match(css, /#app-tooltip\.visible \{[\s\S]*?opacity: 1;[\s\S]*?transform: none;[\s\S]*?--dissolve: 0;/);
  // The sand, in its lightest form: a MASK on the box (three coprime dot grids ramped by
  // --dissolve), never a mote layer — the one shared tooltip re-points many times a second.
  assert.match(css, /#app-tooltip \{[\s\S]*?--dissolve: 1;/);
  assert.equal((css.match(/mask-size: 4px 4px, 7px 7px, 11px 11px;/g) || []).length, 2,
    'both the prefixed and unprefixed mask-size are declared');
  // The keycap shake controlTooltip.js plays on a matching keystroke.
  assert.match(css, /@keyframes keycapShake/);
  assert.match(css, /\.tip-key\.key-shake \{[\s\S]*?animation: keycapShake/);
  const anims = animationsCss();
  assert.match(anims, /#app-tooltip \{\s*\n\s*transition: none; transform: none; --dissolve: 0;/,
    'reduced motion lands it where it is, at full size and solid');
});
