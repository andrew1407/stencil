// The shared accent row builders (js/ui/accentPicker.js): the preset rows themselves,
// the ACTIVE-preset mark, and the Visuals dropdown built on top of them. The logo's copy
// of the same list is covered by logoAccentMenu.test.js — both go through fillAccentMenu /
// markSelected here, so the two can never drift.
// Minimal element stubs, same style as logoAccentMenu.test.js.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ACCENTS } from '../js/core/accents.js';
import { fillAccentMenu, markSelected, buildAccentPicker } from '../js/ui/accentPicker.js';
import { createStubElement, installDom } from './helpers/dom.js';
import { COMPONENTS_CSS, ANIMATIONS_CSS } from './helpers/css.js';

// ── Stubs ── the shared element factory; contains() must always hit, so the popover
// containment guards see every dispatched target as "inside".
const makeEl = () => createStubElement('div', { contains: () => true });

const installDoc = () => installDom({ createElement: makeEl });

// A bare menu with the shared rows in it — what BOTH copies of the list are.
const menuWithRows = (onPick = () => {}) => {
  installDoc();
  const menu = makeEl();
  fillAccentMenu(menu, onPick);
  return menu;
};

const markedKeys = (menu) =>
  menu.children.filter((li) => li.getAttribute('aria-selected') === 'true').map((li) => li.dataset.key);

// ── The rows ────────────────────────────────────────────────────────────
test('every row is a chip + name, and every chip carries the ✓ glyph the active row shows', () => {
  const menu = menuWithRows();
  assert.equal(menu.children.length, ACCENTS.length, 'one row per preset, nothing else');
  for (const [i, li] of menu.children.entries()) {
    const a = ACCENTS[i];
    assert.equal(li.dataset.key, a.key);
    assert.equal(li.getAttribute('role'), 'option');
    assert.match(li.innerHTML, new RegExp(`background:${a.hex}`), 'the chip is painted with the preset');
    // The mark lives INSIDE the chip (desktop parity) — it ships on every row and CSS
    // reveals it on the selected one, so nothing has to be re-rendered when it moves.
    assert.match(li.innerHTML, /class="ic ic-check accent-check"/, 'chip carries the ✓ glyph');
    assert.ok(li.innerHTML.indexOf('accent-check') < li.innerHTML.indexOf('accent-dd-name'),
      'the ✓ is inside the swatch, not a separate check column');
  }
});

// ── The active-preset mark ──────────────────────────────────────────────
test('exactly the active preset is marked, and the mark MOVES when the accent changes', () => {
  const menu = menuWithRows();
  markSelected(menu, 'violet');
  assert.deepEqual(markedKeys(menu), ['violet']);

  markSelected(menu, 'grass');
  assert.deepEqual(markedKeys(menu), ['grass'], 'the previous row is unmarked, the new one marked');
  assert.equal(menu.children.find((li) => li.dataset.key === 'violet').getAttribute('aria-selected'),
    'false', 'aria-selected tracks it both ways (the list is a listbox)');
});

test('a custom hex accent — or nothing at all — marks no row', () => {
  const menu = menuWithRows();
  markSelected(menu, 'pink');
  for (const value of ['#123456', null, undefined]) {
    markSelected(menu, value);
    assert.deepEqual(markedKeys(menu), [], `custom/absent accent (${value}) selects nothing`);
  }
});

// mount.innerHTML is a template string the stub cannot parse, so querySelector is handed the four
// elements buildAccentPicker looks up.
const buildPicker = (current, onSelect = () => {}) => {
  installDoc();
  const mount = makeEl();
  const trigger = makeEl();
  const menu = makeEl();
  const curSw = makeEl();
  const curName = makeEl();
  mount.querySelector = (sel) => ({
    '.accent-dd-trigger': trigger, '.accent-dd-menu': menu,
    '.js-cur-sw': curSw, '.js-cur-name': curName,
  }[sel] || null);
  const api = buildAccentPicker(mount, { current, onSelect });
  return { api, menu, curName, curSw };
};

test('the Visuals picker marks the active preset on build, and set() moves the mark', () => {
  const { api, menu, curName } = buildPicker('sky');
  assert.equal(menu.children.length, ACCENTS.length, 'the shared rows, same as the logo menu');
  assert.deepEqual(markedKeys(menu), ['sky']);
  assert.equal(curName.textContent, 'Sky blue');

  // Changed from the OTHER surface (logo cycle / another tab) — visualsModal.js re-syncs
  // the picker through set(), which must move the mark, not just the trigger swatch.
  api.set('brown');
  assert.deepEqual(markedKeys(menu), ['brown']);
});

test('picking a row in the Visuals picker marks it, and a custom hex leaves every row unmarked', () => {
  const picked = [];
  const { api, menu, curName } = buildPicker('violet', (k) => picked.push(k));
  menu.children.find((li) => li.dataset.key === 'crimson').dispatch('click');
  assert.deepEqual(picked, ['crimson']);
  assert.deepEqual(markedKeys(menu), ['crimson'], 'the pick becomes the mark immediately');

  api.set('#123456');
  assert.deepEqual(markedKeys(menu), [], 'a custom colour is no preset — nothing is marked');
  assert.equal(curName.textContent, 'Custom', 'only the trigger reports it');
});
