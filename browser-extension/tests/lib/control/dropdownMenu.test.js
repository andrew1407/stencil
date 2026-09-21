// src/lib/dropdownMenu.js is a rule-for-rule PORT of browser/js/ui/control/dropdownMenu.js. Its
// behavioural cases (placement, flip, clamp, height cap, portal + put-back) live in
// browser/tests/dropdownMenu.test.js; portParity.test.js pins the two sources identical,
// so the extension no longer duplicates that suite. What remains here is the
// extension-specific wiring: OUR pages and CSS have to use the module.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { themeCss } from '../../helpers/sources.js';

test('every page replaces its native selects, and the list is themed + portaled', () => {
  const cs = readFileSync(new URL('../../../src/lib/control/customSelect.js', import.meta.url), 'utf8');
  assert.match(cs, /showMenu\(menu, trigger\)/, 'customSelect opens through dropdownMenu');
  assert.match(cs, /hideMenu\(menu\)/, 'and closes through it');
  // The menu is on <body> while open, so an outside press must test it too.
  assert.match(cs, /!wrap\.contains\(e\.target\) && !menu\.contains\(e\.target\)/,
    'a press inside its own portaled menu does not close it');
  // Pages toggle `select.hidden` at runtime (popup.js hides the server filter): the
  // wrapper has to follow, or an empty trigger is left behind.
  assert.match(cs, /attributeFilter: \['hidden'\]/, 'the wrapper mirrors the select\'s hidden state');

  // Every page that owns a <select> must enhance it — one native list left behind is one
  // OS-drawn popup covering the panel.
  for (const [page, script] of [['options/options.html', 'options/options.js'],
    ['popup/popup.html', 'popup/popup.js'], ['crop/crop.html', 'crop/crop.js']]) {
    const html = readFileSync(new URL(`../../../src/${page}`, import.meta.url), 'utf8');
    if (!/<select/.test(html)) continue;
    const src = readFileSync(new URL(`../../../src/${script}`, import.meta.url), 'utf8');
    assert.match(src, /enhanceSelect\(/, `${script} enhances its selects`);
  }
  const css = themeCss();
  assert.match(css, /\.cs-native \{ display: none; \}/, 'the native control is hidden, not removed');
  assert.match(css, /\.accent-dd-menu\.dd-portal \{[^}]*position: fixed/, 'the portaled menu is viewport-positioned');
  assert.match(css, /\.accent-dd-menu\.dd-portal \{[^}]*right: auto/, 'and anchored from the left it was given');
  // An in-flow list spans its TRIGGER, the way a native <select>'s does (user report). Only the
  // logo's badge menu, which hangs off a 26px mark, sizes to its rows instead.
  assert.match(css, /\.accent-dd-menu \{[^}]*left: 0; right: 0/, 'the list spans its trigger');
  assert.ok(!/\.accent-swatch-menu/.test(css), 'no per-picker width override survives');
  const options = readFileSync(new URL('../../../src/options/options.js', import.meta.url), 'utf8');
  assert.ok(!/accent-swatch-menu/.test(options), '…and nothing asks for one');
  assert.match(css, /\.logo-accent-menu \{[^}]*min-width: 168px/, 'the logo badge menu keeps its own size');
});
