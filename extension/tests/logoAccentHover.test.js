// The logo accent menu's hover, held through the palette flood (src/lib/logoAccent.js).
// Every preview plays the flood — a View Transition that DROPS real :hover and re-applies
// it just after — so a resting row snapped back, slid in again and blinked its cursor
// once per preview. The hover is latched as a class, the replays frozen while a preview
// shows, and the cursor held on :root. Browser twin: browser/tests/accentPicker.test.js.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { animationsCss, themeCss } from './helpers/sources.js';
import { wireLogoAccent } from '../src/lib/logoAccent.js';
import { installDom, stubEl as el } from './helpers/domStub.js';

const ACCENTS = [{ key: 'violet', label: 'Violet', hex: '#8b5cf6' },
                 { key: 'pink', label: 'Pink', hex: '#ec4899' },
                 { key: 'grass', label: 'Grass green', hex: '#22c55e' }];

const rig = () => {
  const root = el('html');
  const doc = el('#document');
  doc.documentElement = root;
  doc.createElement = el;
  // Reduced motion: the surface dust bails on the spot, so opening the menu is just the
  // list appearing — this suite is about the rows, not the pour.
  const calls = [];
  const winPress = [];   // the window-level capture listener the flood rescue installs
  const win = {
    addEventListener: (t, fn, capture) => { if (t === 'pointerdown' && capture) winPress.push(fn); },
    innerHeight: 800,
    matchMedia: () => ({ matches: true, addEventListener() {}, addListener() {} }),
    StencilAccent: {
      list: ACCENTS,
      get: () => 'violet',
      hexOf: (k) => ACCENTS.find((a) => a.key === k).hex,
      inkOn: () => '#ffffff',
      set: (k) => { calls.push(['set', k]); root.classList.add('theme-instant'); },
      setCustom: () => {},
      previewAccent: (k) => { calls.push(['preview', k]); root.classList.add('theme-instant'); },
      endAccentPreview: () => calls.push(['off']),
    },
  };
  installDom({ document: doc, window: win });
  const logo = el('img');
  const wrap = el('div');
  wrap.appendChild(logo);
  logo.closest = () => wrap;
  wireLogoAccent(logo);
  const menu = wrap.children.find((c) => c.classList.contains('logo-accent-menu'));
  wrap.dispatch('contextmenu', { preventDefault() {} });   // right-click opens the list
  return { root, menu, calls, logo, winPress,
           row: (k) => menu.children.find((r) => r.dataset.key === k) };
};

// ── The press the wipe swallows (browser accentPicker.test.js twin) ──────────
// A view transition's snapshot owns the document's hit test while it plays: a press on a
// row mid-wipe is delivered to <html>, so the row's own click never fires and the outside
// press check reads it as a dismissal — picking a colour while its own hover preview was
// still wiping closed the list and put the old accent back (user report). The rescue is a
// window-level capture listener that resolves the press against the rows' boxes.
const stacked = (menu) => menu.children.forEach((li, i) => {
  li.getBoundingClientRect = () => ({ left: 100, right: 280, top: 200 + i * 30,
                                      bottom: 230 + i * 30, width: 180, height: 30 });
});

test('a press over a row while the wipe owns the hit test still picks that row', (t) => {
  // Mocked, like every other pick here: the commit's afterSwap polls until the flood is
  // over, and this rig's `set` raises theme-instant with nothing to lower it.
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { menu, calls, winPress } = rig();
  stacked(menu);
  assert.equal(winPress.length, 1, 'exactly one window-level rescue');
  let prevented = false, stopped = false;
  winPress[0]({ target: {}, clientX: 150, clientY: 245,   // the middle row: pink
                preventDefault: () => { prevented = true; },
                stopImmediatePropagation: () => { stopped = true; } });
  // The COMMIT is first — the closing list's own revert follows it and is a no-op against
  // the real controller, which drops the preview snapshot the moment a pick lands.
  assert.deepEqual(calls[0], ['set', 'pink'], 'the row under the press is the one committed');
  assert.ok(!calls.slice(1).some(([k]) => k === 'set'), 'and nothing else is committed after it');
  assert.ok(prevented && stopped, 'and no dismissal may see the press');
});

test('…but a press that missed the rows is left alone, so an outside press still dismisses', () => {
  const { menu, calls, winPress } = rig();
  stacked(menu);
  let stopped = false;
  const ev = (x, y) => ({ target: {}, clientX: x, clientY: y, preventDefault: () => {},
                          stopImmediatePropagation: () => { stopped = true; } });
  winPress[0](ev(150, 100));    // above the list
  winPress[0](ev(600, 245));    // beside it
  assert.deepEqual(calls, [], 'nothing was picked');
  assert.equal(stopped, false, 'and the press travels on to the dismissal');
});

test('a hidden list rescues nothing — its rows are still in the DOM with stale boxes', () => {
  const { menu, calls, winPress } = rig();
  stacked(menu);
  menu.hidden = true;
  winPress[0]({ target: {}, clientX: 150, clientY: 245,
                preventDefault: () => {}, stopImmediatePropagation: () => {} });
  assert.deepEqual(calls, []);
});

const held = (root, menu) => [menu.classList.contains('dd-preview-hold'),
                              root.classList.contains('dd-preview-cursor')];

test('the resting row LATCHES its hover, and a preview holds the replays + the cursor', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { root, menu, calls, row } = rig();
  assert.equal(menu.children.length, ACCENTS.length, 'the list is the preset rows');
  row('pink').dispatch('pointerenter');
  assert.ok(row('pink').classList.contains('dd-hover'), 'the slide is a class, not just :hover');
  assert.deepEqual(held(root, menu), [false, false], 'nothing is held before a preview shows');

  t.mock.timers.tick(300);                    // the rested-intent delay
  assert.deepEqual(calls, [['preview', 'pink']], 'resting previews it');
  assert.deepEqual(held(root, menu), [true, true], 'the flood cannot replay the hover or blink the cursor');

  // The transition's own synthetic re-enter on the row already rested on changes nothing.
  row('pink').dispatch('pointerenter');
  t.mock.timers.tick(300);
  assert.deepEqual(calls, [['preview', 'pink']], 'no second preview of the same key');
  assert.deepEqual(held(root, menu), [true, true], 'and the hold survives it');

  // The cursor hold ends WITH the flood, on its own — only its snapshot steals the row's
  // hit test. The replay freeze stays for as long as the preview shows.
  root.classList.remove('theme-instant');
  t.mock.timers.tick(300);
  assert.deepEqual(held(root, menu), [true, false]);
  assert.ok(row('pink').classList.contains('dd-hover'), 'still the row being previewed');
});

test('hopping to another row lifts the hold — the new row plays its own hover once', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { root, menu, calls, row } = rig();
  row('pink').dispatch('pointerenter');
  t.mock.timers.tick(300);
  root.classList.remove('theme-instant');      // that flood finished
  row('grass').dispatch('pointerenter');       // a real hop (the pointer moved off pink)
  assert.deepEqual(held(root, menu), [false, false], 'the hop is a real hover — let it play');
  assert.ok(row('grass').classList.contains('dd-hover'), 'the latch moves with the pointer');
  assert.ok(!row('pink').classList.contains('dd-hover'), 'and only one row wears it');
  t.mock.timers.tick(300);
  assert.deepEqual(calls, [['preview', 'pink'], ['preview', 'grass']]);
  assert.deepEqual(held(root, menu), [true, true], 'the new preview holds again');
});

test('closing the menu drops the latch, the hold and the hand with the preview', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { root, menu, calls, row, logo } = rig();
  row('pink').dispatch('pointerenter');
  t.mock.timers.tick(300);
  root.classList.remove('theme-instant');
  logo.dispatch('click', { altKey: true });    // Alt+click toggles the open list shut
  assert.deepEqual(calls, [['preview', 'pink'], ['off']], 'the committed accent is back');
  assert.deepEqual(held(root, menu), [false, false]);
  assert.ok(!row('pink').classList.contains('dd-hover'), 'nothing held over to the next open');
});

test('a pick frees the cursor at once and holds the replays until the COMMIT flood is over', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { root, menu, calls, row } = rig();
  row('pink').dispatch('pointerenter');
  t.mock.timers.tick(300);
  row('pink').dispatch('click');               // commits — and floods again
  assert.ok(calls.some(([k, v]) => k === 'set' && v === 'pink'), 'the pick applied');
  assert.deepEqual(held(root, menu), [true, false], 'the cursor goes with the closing list');

  t.mock.timers.tick(300);                     // the commit flood is still running
  assert.deepEqual(held(root, menu), [true, false], 'the rows stay frozen under it');
  root.classList.remove('theme-instant');
  t.mock.timers.tick(300);
  assert.deepEqual(held(root, menu), [false, false], 'and are released once it settles');
  assert.ok(!row('pink').classList.contains('dd-hover'), 'the latch goes with it');
});

test('a row passing under the pointer as the list closes cannot start a new preview', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  const { root, calls, row } = rig();
  row('pink').dispatch('pointerenter');
  t.mock.timers.tick(300);
  root.classList.remove('theme-instant');
  row('pink').dispatch('click');
  // The list is leaving under the pointer: whatever it drags past must not paint —
  // nothing would revert it (a hidden list sends no pointerleave).
  row('grass').dispatch('pointerenter');
  t.mock.timers.tick(300);
  assert.ok(!calls.some(([k, v]) => k === 'preview' && v === 'grass'),
    'no preview nobody asked for');
});

// ── CSS contract pins ───────────────────────────────────────────────────────
test('the stylesheets latch the row slide and hold the hand across the flood', () => {
  const theme = themeCss();
  const anims = animationsCss();
  assert.match(theme, /\.accent-dd-opt:hover, \.accent-dd-opt\.dd-hover \{[^}]*transform: translateX\(2px\)/,
    'the JS latch wears the same 2px slide as :hover — the flood drops :hover mid-preview');
  assert.match(anims, /\.accent-dd-menu\.dd-preview-hold \.accent-dd-opt::after \{ animation: none/,
    'and no hover motion can replay under it');
  assert.match(anims, /html\.dd-preview-cursor \{ cursor: pointer; \}/,
    'the hand is held on :root, the one ancestor the transition snapshot inherits from');
});
