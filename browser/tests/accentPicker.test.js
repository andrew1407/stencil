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

// ── The Visuals dropdown, built on the same rows ────────────────────────
// mount.innerHTML is a template string the stub can't parse, so hand querySelector the
// four elements buildAccentPicker looks up.
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

// ── Hover, held through the flood ───────────────────────────────────────
// A preview-wired list — what BOTH copies build: resting on a row paints that accent,
// leaving puts the committed one back. Every preview plays the palette flood, a View
// Transition that drops real :hover, and the rows must not react to that.
const previewMenu = ({ closeOnPick = false } = {}) => {
  const doc = installDoc();
  const menu = makeEl();
  const shown = [];
  const picked = [];
  // The returned reset is what a dismissal calls — toolbar.js for the logo copy,
  // buildAccentPicker's own close() for the Visuals one.
  menu.__reset = fillAccentMenu(menu,
    (k) => {
      picked.push(k);
      doc.documentElement.classList.add('theme-instant');
      if (closeOnPick) menu.__reset();
    },
    { on: (k) => shown.push(k), off: () => shown.push(null) });
  return { doc, menu, shown, picked, row: (k) => menu.children.find((li) => li.dataset.key === k) };
};
const held = (doc, menu) => [menu.classList.contains('dd-preview-hold'),
                             doc.documentElement.classList.contains('dd-preview-cursor')];

test('the resting row LATCHES its hover, and a preview holds the replays + the cursor', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { doc, menu, shown, row } = previewMenu();
  row('pink').dispatch('pointerenter');
  assert.ok(row('pink').classList.contains('dd-hover'), 'the slide is a class, not just :hover');
  assert.deepEqual(held(doc, menu), [false, false], 'nothing is held before a preview shows');

  t.mock.timers.tick(300);                    // the rested-intent delay
  assert.deepEqual(shown, ['pink'], 'resting previews it');
  assert.deepEqual(held(doc, menu), [true, true], 'the flood cannot replay the sweep or blink the cursor');

  // The transition's own synthetic re-enter on the row already rested on changes nothing.
  doc.documentElement.classList.add('theme-instant');
  row('pink').dispatch('pointerenter');
  t.mock.timers.tick(300);
  assert.deepEqual(shown, ['pink'], 'no second preview of the same key');
  assert.deepEqual(held(doc, menu), [true, true], 'and the hold survives it');

  // The cursor hold ends WITH the flood, on its own — only its snapshot steals the row's
  // hit test. The replay freeze stays for as long as the preview shows.
  doc.documentElement.classList.remove('theme-instant');
  t.mock.timers.tick(300);
  assert.deepEqual(held(doc, menu), [true, false]);
  assert.ok(row('pink').classList.contains('dd-hover'), 'the row is still the one being previewed');
});

test('a dismissal takes the latch and the hold with it — the reset the owner calls on close', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { doc, menu, shown, row } = previewMenu();
  const reset = menu.__reset;
  row('pink').dispatch('pointerenter');
  t.mock.timers.tick(300);
  reset();                                       // Escape / an outside press closed the list
  assert.deepEqual(shown, ['pink', null], 'the committed accent is back');
  assert.deepEqual(held(doc, menu), [false, false]);
  assert.ok(!row('pink').classList.contains('dd-hover'), 'nothing held over to the next open');
});

test('hopping to another row lifts the hold — the new row plays its own hover once', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { doc, menu, shown, row } = previewMenu();
  row('pink').dispatch('pointerenter');
  t.mock.timers.tick(300);
  row('grass').dispatch('pointerenter');       // a real hop (the pointer moved off pink)
  assert.deepEqual(held(doc, menu), [false, false], 'the hop is a real hover — let it play');
  assert.ok(row('grass').classList.contains('dd-hover'), 'the latch moves with the pointer');
  assert.ok(!row('pink').classList.contains('dd-hover'), 'and only one row wears it');
  t.mock.timers.tick(300);
  assert.deepEqual(shown, ['pink', 'grass']);
  assert.deepEqual(held(doc, menu), [true, true], 'the new preview holds again');
});

test('leaving the list drops the latch and the hold with the preview', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { doc, menu, shown, row } = previewMenu();
  row('pink').dispatch('pointerenter');
  t.mock.timers.tick(300);
  menu.dispatch('pointerleave');
  t.mock.timers.tick(300);
  assert.deepEqual(shown, ['pink', null], 'the committed accent is back');
  assert.deepEqual(held(doc, menu), [false, false]);
  assert.ok(!row('pink').classList.contains('dd-hover'));
});

test('a pick frees the cursor at once and holds the replays until the COMMIT flood is over', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { doc, menu, picked, row } = previewMenu();
  row('pink').dispatch('pointerenter');
  t.mock.timers.tick(300);
  row('pink').dispatch('click');                 // commits — and floods again
  assert.deepEqual(picked, ['pink']);
  assert.deepEqual(held(doc, menu), [true, false], 'the cursor goes with the closing list');

  t.mock.timers.tick(300);                       // the commit flood is still running
  assert.deepEqual(held(doc, menu), [true, false], 'the rows stay frozen under it');
  doc.documentElement.classList.remove('theme-instant');
  t.mock.timers.tick(300);
  assert.deepEqual(held(doc, menu), [false, false], 'and are released once it settles');
  assert.ok(!row('pink').classList.contains('dd-hover'), 'the latch goes with it');
});

test('a pick that CLOSES the list keeps the rows frozen under the commit flood', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  // The logo menu's own shape: onPick applies the accent and closes, running the same
  // reset a dismissal does — which must not lift the hold from under the pick's own wipe.
  const { doc, menu, row } = previewMenu({ closeOnPick: true });
  row('pink').dispatch('pointerenter');
  t.mock.timers.tick(300);
  row('pink').dispatch('click');
  assert.deepEqual(held(doc, menu), [true, false], 'the dissolving rows cannot replay their hover');
  doc.documentElement.classList.remove('theme-instant');
  t.mock.timers.tick(300);
  assert.deepEqual(held(doc, menu), [false, false], 'released once the commit flood settles');
});

test('a row passing under the pointer as the list closes cannot start a new preview', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { doc, shown, row } = previewMenu({ closeOnPick: true });
  row('pink').dispatch('pointerenter');
  t.mock.timers.tick(300);
  row('pink').dispatch('click');
  // The list is leaving under the pointer: whatever it drags past must not paint.
  row('grass').dispatch('pointerenter');
  t.mock.timers.tick(300);
  assert.deepEqual(shown, ['pink', null],
    'the close reverts its own preview, and the row dragged past starts no new one');
  doc.documentElement.classList.remove('theme-instant');
  t.mock.timers.tick(300);
});

// ── CSS contract pins (same style as logoAccentMenu.test.js) ────────────
test('components.css hides the ✓ by default and reveals it on the selected row only', () => {
  const css = COMPONENTS_CSS;
  const base = /\.accent-check \{[^}]*\}/.exec(css)?.[0] || '';
  assert.match(base, /opacity: 0/, 'the glyph ships on every row, hidden');
  assert.doesNotMatch(base, /color:/, 'the ink is the chip\'s own on-accent one, set per preset in accentPicker.js');
  assert.match(css, /\.accent-dd-opt\[aria-selected="true"\] \.accent-check \{ opacity: 1; \}/,
    'the listbox state IS the mark — one hook for both copies of the list');
  assert.match(/\.accent-swatch \{[^}]*\}/.exec(css)?.[0] || '', /justify-content: center/,
    'the chip centres the tick (without this it hangs off the 16px swatch)');
});

test('animations.css latches the row slide and the cursor across the flood', () => {
  const css = ANIMATIONS_CSS;
  assert.match(css, /\.accent-dd-opt:hover, \.accent-dd-opt\.dd-hover \{ transform: translateX\(2px\); \}/,
    'the JS latch wears the same 2px slide as :hover — the flood drops :hover mid-preview');
  assert.match(css, /\.accent-dd-menu\.dd-preview-hold \.accent-dd-opt::after \{ animation: none/,
    'and the sweep cannot replay under it');
  assert.match(css, /html\.dd-preview-cursor[^{]*\{ cursor: pointer; \}/,
    'the hand is held on :root, the one ancestor the transition snapshot inherits from');
});

// ── The press the wipe swallows ─────────────────────────────────────────
// A view transition's snapshot owns the document's hit test while it plays: a press on a
// row mid-wipe is delivered to <html>, so the row's own click never fires and the list's
// outside-press check reads it as a dismissal — picking a colour while its own hover
// preview was still wiping closed the list and put the old accent back (user report).
// fillAccentMenu answers that with a window-level capture listener that resolves the
// press against the rows' boxes.
const stolenPressHandler = () => {
  const handlers = [];
  const win = { addEventListener: (type, fn, capture) => { if (type === 'pointerdown' && capture) handlers.push(fn); } };
  installDom({ createElement: makeEl }, { window: win });
  const picked = [];
  // contains() answers false: the press went to <html>, not to anything inside the list.
  const menu = createStubElement('div', { contains: () => false });
  fillAccentMenu(menu, (key) => picked.push(key));
  // Rows stacked 30px apart, the way they are drawn.
  menu.children.forEach((li, i) => {
    li.getBoundingClientRect = () => ({ left: 100, right: 280, top: 200 + i * 30, bottom: 230 + i * 30, width: 180, height: 30 });
  });
  assert.equal(handlers.length, 1, 'exactly one window-level rescue per list');
  return { fire: handlers[0], menu, picked };
};

test('a press over a row while the wipe owns the hit test still picks that row', () => {
  const { fire, menu, picked } = stolenPressHandler();
  const third = menu.children[2];
  let prevented = false, stopped = false;
  fire({ target: {}, clientX: 150, clientY: 200 + 2 * 30 + 15,
         preventDefault: () => { prevented = true; }, stopImmediatePropagation: () => { stopped = true; } });
  assert.deepEqual(picked, [third.dataset.key], 'the row under the press is the one committed');
  assert.ok(prevented && stopped, 'and no dismissal may see the press');
});

test('…but a press that missed the rows is left alone, so an outside press still dismisses', () => {
  const { fire, picked } = stolenPressHandler();
  let stopped = false;
  const ev = (x, y) => ({ target: {}, clientX: x, clientY: y,
                          preventDefault: () => {}, stopImmediatePropagation: () => { stopped = true; } });
  fire(ev(150, 100));    // above the list
  fire(ev(600, 260));    // beside it
  assert.deepEqual(picked, [], 'nothing was picked');
  assert.equal(stopped, false, 'and the press travels on to the dismissal');
});

test('a hidden list rescues nothing — its rows are still in the DOM with stale boxes', () => {
  const { fire, menu, picked } = stolenPressHandler();
  menu.hidden = true;
  fire({ target: {}, clientX: 150, clientY: 215, preventDefault: () => {}, stopImmediatePropagation: () => {} });
  assert.deepEqual(picked, []);
});
