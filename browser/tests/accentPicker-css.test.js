// The accent list's CSS pins and the stolen-hit-test rescue (js/ui/accentPicker.js): a press
// delivered to <html> mid-wipe is still resolved against the rows. From accentPicker.test.js.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { fillAccentMenu } from '../js/ui/accentPicker.js';
import { createStubElement, installDom } from './helpers/dom.js';
import { COMPONENTS_CSS, ANIMATIONS_CSS } from './helpers/css.js';

const makeEl = () => createStubElement('div', { contains: () => true });

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

// A view transition's snapshot owns the document's hit test while it plays, so a press mid-wipe is
// delivered to <html>: fillAccentMenu resolves it against the rows' boxes in a capture listener (user report).
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
