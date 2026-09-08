// The logo accent menu's hover, held through the palette flood (src/lib/logoAccent.js).
// Every preview plays the flood — a View Transition that DROPS real :hover and re-applies
// it just after — so a resting row snapped back, slid in again and blinked its cursor
// once per preview. The hover is latched as a class, the replays frozen while a preview
// shows, and the cursor held on :root. Browser twin: browser/tests/accentPicker.test.js.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { wireLogoAccent } from '../src/lib/logoAccent.js';

// ── A page-lite, exactly the surface wireLogoAccent touches ──────────────────
const el = (tag = 'div') => {
  const classes = new Set();
  const attrs = new Map();
  const listeners = {};
  const node = {
    tagName: String(tag).toUpperCase(),
    hidden: false, innerHTML: '', textContent: '', value: '', type: '', tabIndex: 0,
    dataset: {}, children: [], parent: null, classes,
    get className() { return [...classes].join(' '); },
    set className(v) { classes.clear(); for (const c of String(v).split(/\s+/)) if (c) classes.add(c); },
    style: { cssText: '', setProperty() {}, removeProperty() {}, getPropertyValue: () => '' },
    classList: {
      add: (...c) => c.forEach((x) => classes.add(x)),
      remove: (...c) => c.forEach((x) => classes.delete(x)),
      contains: (c) => classes.has(c),
      toggle: (c, on) => { (on === undefined ? !classes.has(c) : on) ? classes.add(c) : classes.delete(c); },
    },
    setAttribute: (k, v) => attrs.set(k, v),
    getAttribute: (k) => (attrs.has(k) ? attrs.get(k) : null),
    appendChild(c) { c.parent = node; node.children.push(c); return c; },
    insertAdjacentElement() {},
    get childElementCount() { return node.children.length; },
    addEventListener(t, fn) { (listeners[t] ||= []).push(fn); },
    removeEventListener() {},
    dispatch(t, ev = {}) { for (const fn of [...(listeners[t] || [])]) fn({ target: node, ...ev }); },
    getBoundingClientRect: () => ({ left: 0, top: 0, right: 40, bottom: 40, width: 40, height: 40 }),
    querySelector: () => null,
    closest: () => null,
    matches: () => false,
  };
  return node;
};

const ACCENTS = [{ key: 'violet', label: 'Violet', hex: '#8b5cf6' },
                 { key: 'pink', label: 'Pink', hex: '#ec4899' },
                 { key: 'grass', label: 'Grass green', hex: '#22c55e' }];

const rig = () => {
  const root = el('html');
  const doc = el('#document');
  doc.documentElement = root;
  doc.createElement = el;
  globalThis.document = doc;
  // Reduced motion: the surface dust bails on the spot, so opening the menu is just the
  // list appearing — this suite is about the rows, not the pour.
  const calls = [];
  globalThis.window = {
    innerHeight: 800,
    matchMedia: () => ({ matches: true, addEventListener() {}, addListener() {} }),
    StencilAccent: {
      list: ACCENTS,
      get: () => 'violet',
      hexOf: (k) => ACCENTS.find((a) => a.key === k).hex,
      set: (k) => { calls.push(['set', k]); root.classList.add('theme-instant'); },
      setCustom: () => {},
      previewAccent: (k) => { calls.push(['preview', k]); root.classList.add('theme-instant'); },
      endAccentPreview: () => calls.push(['off']),
    },
  };
  const logo = el('img');
  const wrap = el('div');
  wrap.appendChild(logo);
  logo.closest = () => wrap;
  wireLogoAccent(logo);
  const menu = wrap.children.find((c) => c.classList.contains('logo-accent-menu'));
  wrap.dispatch('contextmenu', { preventDefault() {} });   // right-click opens the list
  return { root, menu, calls, logo,
           row: (k) => menu.children.find((r) => r.dataset.key === k) };
};

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
  const theme = readFileSync(new URL('../src/lib/theme.css', import.meta.url), 'utf8');
  const anims = readFileSync(new URL('../src/lib/animations.css', import.meta.url), 'utf8');
  assert.match(theme, /\.accent-dd-opt:hover, \.accent-dd-opt\.dd-hover \{[^}]*transform: translateX\(2px\)/,
    'the JS latch wears the same 2px slide as :hover — the flood drops :hover mid-preview');
  assert.match(anims, /\.accent-dd-menu\.dd-preview-hold \.accent-dd-opt::after \{ animation: none/,
    'and no hover motion can replay under it');
  assert.match(anims, /html\.dd-preview-cursor \{ cursor: pointer; \}/,
    'the hand is held on :root, the one ancestor the transition snapshot inherits from');
});
