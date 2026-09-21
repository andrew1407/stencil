// The logo's right-click preset menu (js/ui/toolbar.js wireLogoColorPicker): the shared rows,
// what a pick applies and closes, and the exit animation Escape or an outside press plays.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ACCENTS } from '../js/core/settings/accents.js';
import { LINGER_CLOSE_MS } from '../js/ui/tip/popover.js';
import { rig, marked } from './helpers/logoAccentMenuRig.js';

// ── The right-click menu ────────────────────────────────────────────────
test('right-click opens the preset menu — the shared rows, no Custom entry', () => {
  const { wrap, menu } = rig();
  let prevented = false;
  wrap.dispatch('contextmenu', { preventDefault: () => { prevented = true; } });
  assert.ok(prevented, 'the native context menu is suppressed');
  assert.equal(menu.hidden, false, 'the menu is open');
  assert.equal(menu.children.length, ACCENTS.length, 'one row per preset, nothing else');
  const keys = menu.children.map((li) => li.dataset.key);
  assert.deepEqual(keys, ACCENTS.map((a) => a.key), 'rows are exactly the ACCENTS presets');
  // Selection reflects the active preset — aria-selected, which is what paints the ✓ in
  // the row's chip (accentPicker.test.js pins the shared rows + the CSS).
  const selected = menu.children.filter((li) => li.getAttribute('aria-selected') === 'true');
  assert.equal(selected.length, 1);
  assert.equal(selected[0].dataset.key, 'violet');
  assert.match(selected[0].innerHTML, /accent-check/, 'the marked row carries the ✓ glyph');
});

test('a custom accent selects no row, and picking one routes through setAccent with the logo origin', () => {
  const { logo, wrap, menu, calls } = rig({ customAccent: '#123456' });
  wrap.dispatch('contextmenu', { preventDefault: () => {} });
  assert.ok(menu.children.every((li) => li.getAttribute('aria-selected') === 'false'),
    'custom colour ⇒ no preset row selected');
  menu.children.find((li) => li.dataset.key === 'pink').dispatch('click');
  assert.deepEqual(calls, [['setAccent', 'pink', logo]], 'same path and origin as the click-cycle');
  assert.deepEqual(marked(menu), ['pink'], 'the pick clears the custom state and takes the mark');
});

// ── Picking KEEPS the menu open ─────────────────────────────────────────
test('clicking a row applies the accent and CLOSES the menu (user decision)', () => {
  const { logo, wrap, menu, calls } = rig();
  wrap.dispatch('contextmenu', { preventDefault: () => {} });
  assert.equal(menu.hidden, false, 'open after the right-click');
  menu.children.find((li) => li.dataset.key === 'pink').dispatch('click');
  assert.deepEqual(calls, [['setAccent', 'pink', logo]],
    'the pick applied through the same path and origin');
  assert.deepEqual(marked(menu), ['pink'], 'the ✓ follows the pick');
  assert.ok(menu.hidden || menu.classList.contains('dd-closing'), 'a pick closes the menu');
  // The rows are built once — a pick must not re-render the list out from under the user.
  assert.equal(menu.children.length, ACCENTS.length);
});

test('the mark tracks the picked key, not app.accent — the swap writes it on a LATER beat', () => {
  const { wrap, menu } = rig();     // app.accent stays 'violet': the rig's setAccent is a spy,
  wrap.dispatch('contextmenu', { preventDefault: () => {} });   // like startViewTransition's
  menu.children.find((li) => li.dataset.key === 'grey').dispatch('click');   // deferred callback
  assert.deepEqual(marked(menu), ['grey'],
    'reading app.accent back here would re-mark the OLD preset');
});

test("a swap's synthetic mouseleave does not dismiss the LINGERING menu being recoloured", (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { menu, htmlEl, hover, pressAlt, releaseAlt } = rig();
  hover(true);
  pressAlt();
  menu.matches = (sel) => sel === ':hover';       // pointer rests inside the menu
  releaseAlt();                                   // → linger: rows stay clickable
  assert.equal(menu.hidden, false);

  htmlEl.classList.add('theme-instant');          // a pick's accent swap in flight
  menu.dispatch('mouseleave');                    // the transition's synthetic leave
  t.mock.timers.tick(LINGER_CLOSE_MS + 300);
  assert.equal(menu.hidden, false, 'the menu is still there when the wipe finishes');
  assert.ok(!menu.classList.contains('dd-closing'), 'and was never sent into its exit');

  // …but a REAL leave during the swap still closes it, once the swap is over.
  menu.matches = () => false;
  menu.dispatch('mouseleave');
  htmlEl.classList.remove('theme-instant');
  t.mock.timers.tick(500);                        // settle sees the swap over…
  t.mock.timers.tick(500);                        // …then its :hover re-check runs
  t.mock.timers.tick(LINGER_CLOSE_MS + 10);       // …then the linger grace
  assert.ok(menu.classList.contains('dd-closing') || menu.hidden, 'no pointer, no linger');
});

test('the menu dismisses on Escape and on an outside press — via its exit animation', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { wrap, menu, doc } = rig();
  wrap.dispatch('contextmenu', { preventDefault: () => {} });
  doc.dispatch('keydown', { key: 'Escape' });
  assert.ok(menu.classList.contains('dd-closing'));
  t.mock.timers.tick(260);   // no animationend (e.g. reduced motion) — the fallback timer hides it
  assert.equal(menu.hidden, true);

  wrap.dispatch('contextmenu', { preventDefault: () => {} });
  assert.equal(menu.hidden, false);
  wrap.contains = () => false;                    // press lands outside the wrap
  doc.dispatch('pointerdown', { target: {} });
  menu.dispatch('animationend');
  assert.equal(menu.hidden, true);
});

test('reopening mid-close aborts the exit — the stale animationend cannot hide the fresh menu', () => {
  const { wrap, menu, doc } = rig();
  wrap.dispatch('contextmenu', { preventDefault: () => {} });
  doc.dispatch('keydown', { key: 'Escape' });     // closing…
  wrap.dispatch('contextmenu', { preventDefault: () => {} });   // …reopened before it finished
  assert.ok(!menu.classList.contains('dd-closing'), 'exit state cleared');
  menu.dispatch('animationend');                  // the entrance animation finishing
  assert.equal(menu.hidden, false, 'the reopened menu stays open');
});

test("a ROW's animation cannot end the exit — only the menu's own pop-out does", (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { wrap, menu, doc } = rig();
  wrap.dispatch('contextmenu', { preventDefault: () => {} });
  doc.dispatch('keydown', { key: 'Escape' });
  assert.ok(menu.classList.contains('dd-closing'));
  // Every row runs the shared hover shimmer on its ::after and animationend BUBBLES to the <ul>, and the
  // pointer is over a row whenever the menu closes — so unfiltered it lands inside the exit.
  menu.dispatch('animationend', { target: menu.children[2] });
  assert.equal(menu.hidden, false, 'the pop-out is still playing');
  assert.ok(menu.classList.contains('dd-closing'));
  menu.dispatch('animationend');            // the menu's own exit finishing
  assert.equal(menu.hidden, true);
  t.mock.timers.tick(300);
  assert.equal(menu.hidden, true, 'and the fallback timer was cleared with it');
});

test('reduced motion: the menu shows and hides outright, with no exit to sit through', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const prev = globalThis.matchMedia;
  globalThis.matchMedia = () => ({ matches: true });
  try {
    const { wrap, menu, doc } = rig();
    wrap.dispatch('contextmenu', { preventDefault: () => {} });
    assert.equal(menu.hidden, false, 'it still opens — animations.css just drops the rise');
    doc.dispatch('keydown', { key: 'Escape' });
    assert.equal(menu.hidden, true, 'hidden at once, not after the fallback timer');
    assert.ok(!menu.classList.contains('dd-closing'), 'and no exit state is left behind');
  } finally { globalThis.matchMedia = prev; }
});
